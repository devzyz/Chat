#include "ResourceHttpServer.h"
#include "AvatarPng.h"
#include <boost/beast.hpp>
#include <json/json.h>
#include <array>
#include <charconv>
#include <optional>
#include <set>

namespace resource {
namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = net::ip::tcp;

static std::uint64_t Number(const std::string& text) {
    std::uint64_t value = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw Error(400, "invalid unsigned integer");
    return value;
}
static std::string Json(const Json::Value& value) {
    Json::StreamWriterBuilder writer; writer["indentation"] = "";
    return Json::writeString(writer, value);
}
static Json::Value Describe(const Metadata& metadata) {
    Json::Value value;
    value["resource_id"] = metadata.id; value["upload_id"] = metadata.id;
    value["owner"] = metadata.owner; value["name"] = metadata.name;
    value["media_type"] = metadata.media_type;
    // Decimal strings avoid loss of 64-bit precision in JSON clients.
    value["size"] = std::to_string(metadata.size); value["offset"] = std::to_string(metadata.offset);
    value["ready"] = metadata.ready; value["sha256"] = metadata.sha256;
    return value;
}

struct ResourceHttpServer::Impl : std::enable_shared_from_this<Impl> {
    struct Session;
    tcp::acceptor acceptor;
    net::thread_pool storage{1};
    ResourceStore& store;
    Authenticate authenticate;
    CanRead can_read;
    Publish publish;
    GetAvatar get_avatar;
    SetAvatar set_avatar;
    std::set<std::shared_ptr<Session>> sessions;
    bool stopping = false;
    Impl(net::io_context& context, const std::string& host, unsigned short port,
         ResourceStore& value, Authenticate auth, CanRead reader, Publish publisher,
         GetAvatar avatar_reader, SetAvatar avatar_writer)
        : acceptor(context, tcp::endpoint(net::ip::make_address(host), port)), store(value),
          authenticate(std::move(auth)), can_read(std::move(reader)), publish(std::move(publisher)),
          get_avatar(std::move(avatar_reader)), set_avatar(std::move(avatar_writer)) {}
    void Accept();
    void Stop();
};
struct ResourceHttpServer::Impl::Session : std::enable_shared_from_this<Session> {
    beast::tcp_stream stream;
    beast::flat_buffer buffer{8192};
    std::shared_ptr<Impl> server;
    std::optional<http::request_parser<http::buffer_body>> parser;
    std::array<char, ResourceStore::BUFFER_SIZE> block{};
    std::string control, id, route;
    int uid = 0;
    std::uint64_t offset = 0, remaining = 0;
    bool closed = false, keep_alive = false, patch = false;
    std::optional<http::response<http::empty_body>> download_header;
    std::optional<http::response_serializer<http::empty_body>> download_serializer;
    std::vector<char> outgoing;
    explicit Session(tcp::socket socket, std::shared_ptr<Impl> owner)
        : stream(std::move(socket)), server(std::move(owner)) {}
    void Close() {
        if (closed) return;
        closed = true;
        beast::error_code error;
        stream.socket().cancel(error); stream.socket().close(error);
        server->sessions.erase(shared_from_this());
    }
    template<class Work, class Done> void Run(Work work, Done done) {
        auto self = shared_from_this();
        net::post(server->storage, [self, work = std::move(work), done = std::move(done)]() mutable {
            Json::Value result; int status = 200;
            try { result = work(); }
            catch (const Error& error) { status = error.status; result["error"] = error.what(); }
            catch (...) { status = 500; result["error"] = "resource operation failed"; }
            net::post(self->stream.get_executor(), [self, done = std::move(done), result, status]() mutable {
                if (self->closed) return;
                if (status != 200) { self->keep_alive = false; self->Reply(status, result); }
                else done(result);
            });
        });
    }
    void Start() {
        if (closed) return;
        parser.emplace(); parser->header_limit(8192); parser->body_limit(1024 * 1024);
        control.clear(); id.clear(); patch = false; keep_alive = false;
        stream.expires_after(std::chrono::seconds(30));
        http::async_read_header(stream, buffer, *parser, [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (error == http::error::body_limit || error == http::error::header_limit) {
                Json::Value result; result["error"] = "request exceeds HTTP limits";
                self->Reply(413, result); return;
            }
            if (error) { self->Close(); return; }
            self->Headers();
        });
    }
    void Headers() {
        const auto& request = parser->get();
        keep_alive = request.keep_alive(); route = std::string(request.target());
        if (route == "/health" && request.method() == http::verb::get && parser->is_done()) {
            Json::Value value; value["status"] = "ok"; Reply(200, value); return;
        }
        try {
            const auto parsed_uid = Number(std::string(request["X-User-Id"]));
            if (!parsed_uid || parsed_uid > 2147483647) throw Error(401, "invalid user");
            uid = static_cast<int>(parsed_uid);
            const std::string authorization(request[http::field::authorization]);
            if (authorization.rfind("Bearer ", 0) != 0) throw Error(401, "token required");
            if (request.chunked()) throw Error(400, "Content-Length required");
            const auto length = parser->content_length();
            if (!length && !parser->is_done()) throw Error(411, "Content-Length required");
            if (route.rfind("/uploads/", 0) == 0) {
                id = route.substr(9);
                if (id.size() > 9 && id.substr(id.size() - 9) == "/complete") id.resize(id.size() - 9);
            } else if (route.rfind("/resources/", 0) == 0) id = route.substr(11);
            patch = request.method() == http::verb::patch;
            if (patch) offset = Number(std::string(request["Upload-Offset"]));
            const auto token = authorization.substr(7);
            Run([self = shared_from_this(), token, length] {
                if (!self->server->authenticate(self->uid, token)) throw Error(401, "authentication failed");
                if (self->patch) {
                    if (self->route != "/uploads/" + self->id) throw Error(404, "route not found");
                    auto metadata = self->server->store.Owned(self->id, self->uid);
                    if (metadata.ready || metadata.offset != self->offset) throw Error(409, "upload offset mismatch");
                    if (!length || *length > metadata.size - metadata.offset) throw Error(413, "invalid upload length");
                } else if (length && *length > 8192) throw Error(413, "metadata too large");
                return Json::Value();
            }, [self = shared_from_this()](const Json::Value&) { self->ReadBody(); });
        } catch (const Error& error) {
            keep_alive = false; Json::Value result; result["error"] = error.what(); Reply(error.status, result);
        }
    }
    void ReadBody() {
        if (parser->is_done()) { Dispatch(); return; }
        parser->get().body().data = block.data(); parser->get().body().size = block.size();
        stream.expires_after(std::chrono::seconds(30));
        http::async_read_some(stream, buffer, *parser, [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (self->closed) return;
            const auto count = self->block.size() - self->parser->get().body().size;
            if (error == http::error::need_buffer) error = {};
            if (count && self->patch) {
                // Persist any received prefix even when the peer disconnects mid-request.
                const auto at = self->offset;
                self->Run([self, count, at] {
                    return Describe(self->server->store.Append(self->id, self->uid, at, self->block.data(), count));
                }, [self, error, count](const Json::Value&) {
                    self->offset += count;
                    if (error) self->Close(); else self->ReadBody();
                });
            } else {
                if (error) { self->Close(); return; }
                self->control.append(self->block.data(), count);
                if (self->control.size() > 8192) {
                    self->keep_alive = false; Json::Value result; result["error"] = "metadata too large";
                    self->Reply(413, result); return;
                }
                self->ReadBody();
            }
        });
    }
    void Dispatch() {
        const auto method = parser->get().method();
        if (method == http::verb::get && route == "/resources/" + id) { Download(); return; }
        Run([self = shared_from_this(), method] {
            auto& store = self->server->store;
            if (self->route.rfind("/avatars/", 0) == 0) {
                const auto owner = Number(self->route.substr(9));
                if (owner == 0 || owner > 2147483647) throw Error(400, "invalid avatar owner");
                if (!self->server->get_avatar || !self->server->set_avatar)
                    throw Error(503, "avatar service unavailable");
                if (method == http::verb::get) {
                    const auto id = self->server->get_avatar(static_cast<int>(owner));
                    return id.empty() ? Json::Value(Json::objectValue) : Describe(store.Inspect(id));
                }
                if (method != http::verb::put) throw Error(405, "method not allowed");
                if (owner != self->uid) throw Error(403, "cannot change another user's avatar");
                Json::Value value; Json::CharReaderBuilder builder; std::string errors;
                std::istringstream input(self->control);
                if (!Json::parseFromStream(builder, input, &value, &errors) || !value.isObject() ||
                    !value["resource_id"].isString()) throw Error(400, "resource_id required");
                const auto metadata = store.Owned(value["resource_id"].asString(), self->uid);
                if (!metadata.ready) throw Error(409, "avatar upload incomplete");
                if (metadata.media_type != "image/png" || metadata.size > 1024 * 1024)
                    throw Error(415, "avatar must be PNG under 1 MiB");
                std::vector<char> bytes;
                for (std::uint64_t offset = 0; offset < metadata.size;) {
                    auto part = store.Read(metadata.id, offset, ResourceStore::BUFFER_SIZE);
                    if (part.empty()) throw Error(422, "avatar file truncated");
                    offset += part.size(); bytes.insert(bytes.end(), part.begin(), part.end());
                }
                ValidateAvatarPng(bytes);
                self->server->set_avatar(self->uid, metadata.id);
                return Describe(metadata);
            }
            if (self->patch) return Describe(store.Owned(self->id, self->uid));
            if (method == http::verb::post && self->route == "/uploads") {
                Json::Value value; Json::CharReaderBuilder builder; std::string errors;
                std::istringstream input(self->control);
                if (!Json::parseFromStream(builder, input, &value, &errors)) throw Error(400, "invalid JSON");
                return Describe(store.Create(self->uid, value["name"].asString(), value["media_type"].asString(),
                    Number(value["size"].asString()), value["sha256"].asString()));
            }
            if (method == http::verb::get && self->route == "/uploads/" + self->id)
                return Describe(store.Owned(self->id, self->uid));
            if (method == http::verb::post && self->route == "/uploads/" + self->id + "/complete") {
                const auto metadata = store.Complete(self->id, self->uid);
                if (self->server->publish) self->server->publish(metadata);
                return Describe(metadata);
            }
            throw Error(404, "route not found");
        }, [self = shared_from_this()](const Json::Value& value) { self->Reply(200, value); });
    }
    void Reply(int code, const Json::Value& value) {
        auto response = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(code), 11);
        response->set(http::field::content_type, "application/json");
        response->keep_alive(keep_alive); response->body() = Json(value); response->prepare_payload();
        stream.expires_after(std::chrono::seconds(30));
        http::async_write(stream, *response, [self = shared_from_this(), response](beast::error_code error, std::size_t) {
            if (error || !self->keep_alive || self->server->stopping) self->Close(); else self->Start();
        });
    }
    void Download() {
        const std::string range(parser->get()[http::field::range]);
        Run([self = shared_from_this(), range] {
            auto metadata = self->server->store.Inspect(self->id);
            if (!metadata.ready) throw Error(409, "upload not complete");
            if (metadata.owner != self->uid && (!self->server->can_read || !self->server->can_read(self->uid, metadata)))
                throw Error(403, "download forbidden");
            std::uint64_t at = 0;
            if (!range.empty()) {
                if (range.rfind("bytes=", 0) != 0 || range.back() != '-') throw Error(416, "only open ended ranges supported");
                at = Number(range.substr(6, range.size() - 7));
                if (at >= metadata.size) throw Error(416, "range outside resource");
            }
            auto result = Describe(metadata); result["start"] = std::to_string(at); return result;
        }, [self = shared_from_this(), range](const Json::Value& value) {
            self->offset = Number(value["start"].asString());
            const auto size = Number(value["size"].asString()); self->remaining = size - self->offset;
            self->download_header.emplace(range.empty() ? http::status::ok : http::status::partial_content, 11);
            auto& header = *self->download_header;
            header.set(http::field::content_type, value["media_type"].asString());
            header.set(http::field::accept_ranges, "bytes"); header.set(http::field::etag, "\"" + value["sha256"].asString() + "\"");
            if (!range.empty()) header.set(http::field::content_range, "bytes " + std::to_string(self->offset) + "-" + std::to_string(size - 1) + "/" + std::to_string(size));
            header.content_length(self->remaining); header.keep_alive(self->keep_alive);
            self->download_serializer.emplace(header);
            self->stream.expires_after(std::chrono::seconds(30));
            http::async_write_header(self->stream, *self->download_serializer, [self](beast::error_code error, std::size_t) {
                if (error) self->Close(); else self->WriteFile();
            });
        });
    }
    void WriteFile() {
        if (!remaining) {
            download_serializer.reset(); download_header.reset();
            if (keep_alive && !server->stopping) Start(); else Close();
            return;
        }
        auto self = shared_from_this();
        net::post(server->storage, [self] {
            std::vector<char> bytes; bool failed = false;
            try { bytes = self->server->store.Read(self->id, self->offset, ResourceStore::BUFFER_SIZE); }
            catch (...) { failed = true; }
            net::post(self->stream.get_executor(), [self, bytes = std::move(bytes), failed]() mutable {
                if (self->closed) return;
                if (failed || bytes.empty()) { self->Close(); return; }
                self->outgoing = std::move(bytes);
                self->stream.expires_after(std::chrono::seconds(30));
                net::async_write(self->stream, net::buffer(self->outgoing), [self](beast::error_code error, std::size_t count) {
                    if (error) { self->Close(); return; }
                    self->offset += count; self->remaining -= count; self->WriteFile();
                });
            });
        });
    }
};
void ResourceHttpServer::Impl::Accept() {
    if (stopping) return;
    acceptor.async_accept([self = shared_from_this()](beast::error_code error, tcp::socket socket) {
        if (!error && !self->stopping && self->sessions.size() < 32) {
            auto session = std::make_shared<Session>(std::move(socket), self);
            self->sessions.insert(session); session->Start();
        }
        if (!self->stopping) self->Accept();
    });
}
void ResourceHttpServer::Impl::Stop() {
    if (stopping) return;
    stopping = true; beast::error_code error; acceptor.close(error);
    auto pending = sessions;
    for (auto& session : pending) session->Close();
}
ResourceHttpServer::ResourceHttpServer(net::io_context& context, const std::string& host, unsigned short port,
    ResourceStore& store, Authenticate authenticate, CanRead can_read, Publish publish,
    GetAvatar get_avatar, SetAvatar set_avatar)
    : _impl(std::make_shared<Impl>(context, host, port, store, std::move(authenticate), std::move(can_read),
          std::move(publish), std::move(get_avatar), std::move(set_avatar))) {}
ResourceHttpServer::~ResourceHttpServer() { _impl->storage.join(); }
void ResourceHttpServer::Start() { _impl->Accept(); }
void ResourceHttpServer::Stop() { _impl->Stop(); }
unsigned short ResourceHttpServer::Port() const { return _impl->acceptor.local_endpoint().port(); }
}
