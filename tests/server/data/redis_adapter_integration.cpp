#include "../../../common/redis/RedisPool.h"

#include <chrono>
#include <cstdlib>
#include <future>
#include <condition_variable>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string Environment(const char* name) {
    const auto* value = std::getenv(name);
    Require(value != nullptr && value[0] != '\0', "missing integration environment");
    return value;
}

using Reply = std::unique_ptr<redisReply, decltype(&freeReplyObject)>;
struct Lease {
    chat_redis::RedisPool& pool;
    redisContext* connection;
    explicit Lease(chat_redis::RedisPool& owner) : pool(owner), connection(pool.Borrow()) {
        Require(connection != nullptr, "Redis borrow failed");
    }
    ~Lease() { pool.Return(connection); }
};

Reply Command(redisContext* connection, const char* command) {
    return Reply(static_cast<redisReply*>(redisCommand(connection, command)), &freeReplyObject);
}

long long ClientId(redisContext* connection) {
    auto reply = Command(connection, "CLIENT ID");
    Require(reply && reply->type == REDIS_REPLY_INTEGER, "client identity failed");
    return reply->integer;
}
}

int main(int argc, char** argv) {
    try {
        using namespace std::chrono_literals;
        if (argc == 2 && std::string(argv[1]) == "lifecycle") {
            chat_redis::RedisPool pool("127.0.0.1", 1, "", 0);
            const auto started = std::chrono::steady_clock::now();
            Require(pool.Borrow(30ms) == nullptr, "empty pool borrow succeeded");
            Require(std::chrono::steady_clock::now() - started < 500ms, "borrow deadline exceeded");
            auto borrower = std::async(std::launch::async, [&]() { return pool.Borrow(2s); });
            pool.Close();
            pool.Close();
            Require(borrower.wait_for(300ms) == std::future_status::ready, "close did not wake borrower");
            Require(borrower.get() == nullptr, "closed borrow succeeded");
            std::cout << "PASS lifecycle\n";
            return EXIT_SUCCESS;
        }
        Require(argc == 2, "missing scenario");
        const std::string scenario(argv[1]);
        const auto host = Environment("CHAT_REDIS_HOST");
        const auto port = std::stoi(Environment("CHAT_REDIS_PORT"));
        const auto password = Environment("CHAT_REDIS_PASSWORD");
        const auto prefix = Environment("CHAT_REDIS_PREFIX");
        Require(prefix.rfind("chat:", 0) == 0 && prefix.back() == ':', "invalid owned prefix");
        chat_redis::RedisPool pool(host, port, password, 1, 200ms);
        if (scenario == "reject") {
            chat_redis::RedisPool rejected(host, port, password + "-invalid", 1, 200ms);
            Require(rejected.Borrow(500ms) == nullptr, "invalid authentication accepted");
        } else if (scenario == "refused" || scenario == "handshake-timeout") {
            const auto started = std::chrono::steady_clock::now();
            Require(pool.Borrow(500ms) == nullptr, "refused endpoint accepted");
            Require(std::chrono::steady_clock::now() - started < 1s, "refusal was not bounded");
        } else if (scenario == "readwrite") {
            Lease lease(pool);
            const auto key = prefix + "native:ttl";
            const std::string value("native\0binary", 13);
            Reply set(static_cast<redisReply*>(redisCommand(lease.connection, "SET %b %b EX 1",
                key.data(), key.size(), value.data(), value.size())), &freeReplyObject);
            Require(set && set->type == REDIS_REPLY_STATUS, "atomic SET EX failed");
            Reply get(static_cast<redisReply*>(redisCommand(lease.connection, "GET %b", key.data(), key.size())), &freeReplyObject);
            Require(get && get->type == REDIS_REPLY_STRING && std::string(get->str, get->len) == value,
                "binary readback failed");
            Reply ttl(static_cast<redisReply*>(redisCommand(lease.connection, "PTTL %b", key.data(), key.size())), &freeReplyObject);
            Require(ttl && ttl->type == REDIS_REPLY_INTEGER && ttl->integer >= 0 && ttl->integer <= 1000,
                "TTL missing or out of range");
            std::mutex mutex;
            std::condition_variable changed;
            std::unique_lock<std::mutex> lock(mutex);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            bool expired = false;
            while (std::chrono::steady_clock::now() < deadline) {
                Reply probe(static_cast<redisReply*>(redisCommand(lease.connection, "GET %b", key.data(), key.size())), &freeReplyObject);
                Require(probe != nullptr, "expiry probe failed");
                if (probe->type == REDIS_REPLY_NIL) { expired = true; break; }
                changed.wait_for(lock, 10ms);
            }
            Require(expired, "key did not expire");
        } else if (scenario == "timeout") {
            {
                Lease lease(pool);
                const auto key = prefix + "native:empty-list";
                const auto started = std::chrono::steady_clock::now();
                Reply reply(static_cast<redisReply*>(redisCommand(lease.connection, "BLPOP %b 10",
                    key.data(), key.size())), &freeReplyObject);
                Require(!reply && lease.connection->err != 0, "command timeout did not invalidate connection");
                Require(std::chrono::steady_clock::now() - started < 1s, "command timeout exceeded bound");
            }
            Lease replacement(pool);
            Require(ClientId(replacement.connection) > 0, "timed out connection was reused");
        } else if (scenario == "reconnect") {
            long long old_id = 0;
            {
                Lease lease(pool);
                old_id = ClientId(lease.connection);
                chat_redis::RedisPool admin(host, port, password, 1, 200ms);
                Lease admin_lease(admin);
                Reply killed(static_cast<redisReply*>(redisCommand(admin_lease.connection,
                    "CLIENT KILL ID %lld", old_id)), &freeReplyObject);
                Require(killed && killed->type == REDIS_REPLY_INTEGER && killed->integer == 1,
                    "connection fault injection failed");
            }
            Lease replacement(pool);
            Require(ClientId(replacement.connection) != old_id, "dead idle connection reused");
        } else if (scenario == "restart") {
            { Lease initial(pool); Require(ClientId(initial.connection) > 0, "initial connection unavailable"); }
            std::cout << "READY\n" << std::flush;
            std::string signal;
            Require(static_cast<bool>(std::getline(std::cin, signal)) && signal == "RESUME", "restart handshake failed");
            Lease replacement(pool);
            const auto key = prefix + "native:restart";
            Reply set(static_cast<redisReply*>(redisCommand(replacement.connection, "SET %b resumed EX 30",
                key.data(), key.size())), &freeReplyObject);
            Require(set && set->type == REDIS_REPLY_STATUS, "same pool did not recover after service restart");
            Reply removed(static_cast<redisReply*>(redisCommand(replacement.connection, "DEL %b",
                key.data(), key.size())), &freeReplyObject);
            Require(removed && removed->type == REDIS_REPLY_INTEGER && removed->integer == 1, "restart key cleanup failed");
        } else {
            Require(false, "unknown scenario");
        }
        pool.Close();
        std::cout << "PASS " << scenario << '\n';
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        // Only our stable assertion messages are emitted, never hiredis connection details.
        std::cerr << "redis adapter integration failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
