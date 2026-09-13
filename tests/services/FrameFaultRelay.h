#pragma once

#include "ChatFrameCodec.h"
#include <boost/asio.hpp>
#include <json/json.h>
#include <fstream>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>

// One io_context owns both directions; each direction reads again only after its
// write completes. The production codec bounds frames, including malformed input.
class FrameFaultRelay {
    using Tcp = boost::asio::ip::tcp;
    struct Pair : std::enable_shared_from_this<Pair> {
        Pair(boost::asio::io_context& io, FrameFaultRelay& owner) : front(io), back(io), relay(owner) {}
        Tcp::socket front, back;
        FrameFaultRelay& relay;
        void Close() {
            boost::system::error_code ignored;
            front.close(ignored); back.close(ignored);
        }
        void Read(bool upstream) {
            auto self = shared_from_this();
            auto header = std::make_shared<ChatFrameCodec::HeaderBytes>();
            auto& source = upstream ? front : back;
            boost::asio::async_read(source, boost::asio::buffer(*header),
                [self, header, upstream](auto error, auto) {
                    if (error) { self->Close(); return; }
                    const auto decoded = ChatFrameCodec::DecodeValidatedHeader(header->data(), MAX_LENGTH);
                    if (!decoded) { self->relay.failed = true; self->Close(); return; }
                    auto body = std::make_shared<std::string>(decoded->body_length, '\0');
                    auto& source = upstream ? self->front : self->back;
                    boost::asio::async_read(source, boost::asio::buffer(*body),
                        [self, header, body, upstream, id = decoded->message_id](auto body_error, auto) {
                            if (body_error) { self->Close(); return; }
                            Json::Value value;
                            Json::Reader reader;
                            if (!reader.parse(*body, value)) { self->relay.failed = true; self->Close(); return; }
                            auto& relay = self->relay;
                            if (!upstream && id == MSG_TEXT_CHAT_MSG_RSP && !relay.dropped && value["error"].asInt() == 0) {
                                for (const auto& item : value["uuid_msgId"]) {
                                    if (item["msg_uuid"].asString() == relay.drop_uuid && item["message_id"].asInt64() > 0) {
                                        relay.dropped = true;
                                        relay.committed_id = item["message_id"].asInt64();
                                        relay.Report(false);
                                        self->Close();
                                        return;
                                    }
                                }
                            }
                            auto bytes = std::make_shared<std::string>(reinterpret_cast<const char*>(header->data()), header->size());
                            *bytes += *body;
                            if (!upstream && id == MSG_NOTIFY_CHAT_MSG_REQ && !relay.replayed &&
                                body->find(relay.replay_uuid) != std::string::npos) {
                                const std::string copy = *bytes;
                                *bytes += copy;
                                relay.replayed = true;
                                relay.Report(false);
                            }
                            auto& destination = upstream ? self->back : self->front;
                            boost::asio::async_write(destination, boost::asio::buffer(*bytes),
                                [self, bytes, upstream](auto write_error, auto) {
                                    if (write_error) self->Close(); else self->Read(upstream);
                                });
                        });
                });
        }
    };
public:
    FrameFaultRelay(unsigned short listen_port, unsigned short backend_port,
                    std::string drop, std::string replay, std::string report_path)
        : acceptor(io), timer(io), backend(backend_port), drop_uuid(std::move(drop)),
          replay_uuid(std::move(replay)), report(std::move(report_path)) {
        const Tcp::endpoint endpoint(boost::asio::ip::make_address("127.0.0.1"), listen_port);
        acceptor.open(endpoint.protocol());
        // The previous ChatServer can leave accepted connections in TIME_WAIT.
        // Reuse the released endpoint, while an active listener must still fail bind.
#ifndef _WIN32
        acceptor.set_option(Tcp::acceptor::reuse_address(true));
#endif
        boost::system::error_code bind_error;
        acceptor.bind(endpoint, bind_error);
        if (bind_error) {
            Json::Value failure;
            failure["ready"] = false;
            failure["complete"] = false;
            failure["startupStage"] = "bind";
            failure["errorCode"] = bind_error.value();
            std::ofstream output(report);
            output << Json::writeString(Json::StreamWriterBuilder{}, failure);
            throw boost::system::system_error(bind_error);
        }
        acceptor.listen();
    }
    int Run(const std::function<bool()>& stopped) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(420);
        Accept();
        std::function<void()> tick;
        tick = [&] {
            timer.expires_after(std::chrono::milliseconds(100));
            timer.async_wait([&](auto error) {
                if (error) return;
                if (stopped() || std::chrono::steady_clock::now() >= deadline) {
                    failed = failed || !stopped();
                    io.stop();
                } else tick();
            });
        };
        tick();
        Report(false);
        io.run();
        boost::system::error_code ignored;
        acceptor.close(ignored);
        for (auto& pair : pairs) if (auto live = pair.lock()) live->Close();
        Report(!failed);
        return failed ? 1 : 0;
    }
private:
    void Accept() {
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(), [](const auto& pair) { return pair.expired(); }), pairs.end());
        if (pairs.size() >= 64) { failed = true; io.stop(); return; }
        auto pair = std::make_shared<Pair>(io, *this);
        pairs.push_back(pair);
        acceptor.async_accept(pair->front, [this, pair](auto error) {
            if (error) return;
            pair->back.async_connect({boost::asio::ip::make_address("127.0.0.1"), backend}, [pair](auto connect_error) {
                if (connect_error) pair->Close(); else { pair->Read(true); pair->Read(false); }
            });
            Accept();
        });
    }
    void Report(bool complete) {
        Json::Value value;
        value["ready"] = true; value["complete"] = complete;
        value["droppedAck"] = dropped; value["replayedNotification"] = replayed;
        value["committedId"] = std::to_string(committed_id);
        std::ofstream output(report);
        output << Json::writeString(Json::StreamWriterBuilder{}, value);
        if (!output) failed = true;
    }
    boost::asio::io_context io;
    Tcp::acceptor acceptor;
    boost::asio::steady_timer timer;
    unsigned short backend;
    std::string drop_uuid, replay_uuid, report;
    std::vector<std::weak_ptr<Pair>> pairs;
    bool dropped = false, replayed = false, failed = false;
    Json::Int64 committed_id = 0;
};
