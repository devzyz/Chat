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

/** @brief 将完整十进制文本转换为非负整数，非法或溢出时抛出资源请求错误。 */
static std::uint64_t Number(const std::string& text) {
    std::uint64_t value = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size())
        throw Error(400, "invalid unsigned integer");
    return value;
}
/** @brief 把 JSON 响应值编码为无多余缩进的字节文本。 */
static std::string Json(const Json::Value& value) {
    Json::StreamWriterBuilder writer; writer["indentation"] = "";
    return Json::writeString(writer, value);
}
/** @brief 把资源元数据转换为 HTTP JSON 描述，大小和偏移按协议编码。 */
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

/** @brief 拥有外层服务的运行状态和异步操作所需资源，生命周期由外层实现约束。 */
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
    /** @brief 初始化Impl，拥有外层服务的运行状态和异步操作所需资源，生命周期由外层实现约束。 */
    Impl(net::io_context& context, const std::string& host, unsigned short port,
         ResourceStore& value, Authenticate auth, CanRead reader, Publish publisher,
         GetAvatar avatar_reader, SetAvatar avatar_writer)
        : acceptor(context, tcp::endpoint(net::ip::make_address(host), port)), store(value),
          authenticate(std::move(auth)), can_read(std::move(reader)), publish(std::move(publisher)),
          get_avatar(std::move(avatar_reader)), set_avatar(std::move(avatar_writer)) {}
    /** @brief 安排下一次资源 HTTP 连接接收，并在运行状态与会话数限制内启动会话。 */
    void Accept();
    /** @brief 停止接收新工作并关闭当前服务的监听或执行器；后续销毁由所属生命周期流程负责。 */
    void Stop();
};
/** @brief 拥有一次资源 HTTP 连接及流式读写状态，异步回调以共享引用保持自身存活。 */
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
    /** @brief 初始化Session，拥有一次资源 HTTP 连接及流式读写状态，异步回调以共享引用保持自身存活。 */
    explicit Session(tcp::socket socket, std::shared_ptr<Impl> owner)
        : stream(std::move(socket)), server(std::move(owner)) {}
    /** @brief 幂等标记连接关闭，取消 socket 操作并从服务的会话集合移除自身。 */
    void Close() {
        if (closed) return;
        closed = true;
        beast::error_code error;
        stream.socket().cancel(error); stream.socket().close(error);
        server->sessions.erase(shared_from_this());
    }
    /** @brief 执行队列中的工作并把结果交回所属执行器，遵守停止状态与错误映射。 */
    template<class Work, class Done> void Run(Work work, Done done) {
        auto self = shared_from_this();
        net::post(server->storage, /** @brief 在工作执行器运行资源动作并捕获业务或存储错误。 */ [self, work = std::move(work), done = std::move(done)]() mutable {
            Json::Value result; int status = 200;
            try { result = work(); }
            catch (const Error& error) { status = error.status; result["error"] = error.what(); }
            catch (...) { status = 500; result["error"] = "resource operation failed"; }
            net::post(self->stream.get_executor(), /** @brief 把工作结果交回连接执行器，关闭会话不再发送响应。 */ [self, done = std::move(done), result, status]() mutable {
                if (self->closed) return;
                if (status != 200) { self->keep_alive = false; self->Reply(status, result); }
                else done(result);
            });
        });
    }
    /** @brief 重置单次请求状态，在既有连接上有界读取 HTTP 头；首次及 keep-alive 后续请求共用此入口。 */
    void Start() {
        if (closed) return;
        parser.emplace(); parser->header_limit(8192); parser->body_limit(1024 * 1024);
        control.clear(); id.clear(); patch = false; keep_alive = false;
        stream.expires_after(std::chrono::seconds(30));
        http::async_read_header(stream, buffer, *parser, /** @brief 完成 HTTP 头读取后检查大小限制及连接错误。 */ [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (error == http::error::body_limit || error == http::error::header_limit) {
                Json::Value result; result["error"] = "request exceeds HTTP limits";
                self->Reply(413, result); return;
            }
            if (error) { self->Close(); return; }
            self->Headers();
        });
    }
    /** @brief 核对 HTTP 方法、路由、认证和请求长度，在处理正文前完成资源前置校验。 */
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
            Run(/** @brief 在工作执行器核对认证身份、上传偏移和请求长度。 */ [self = shared_from_this(), token, length] {
                if (!self->server->authenticate(self->uid, token)) throw Error(401, "authentication failed");
                if (self->patch) {
                    if (self->route != "/uploads/" + self->id) throw Error(404, "route not found");
                    auto metadata = self->server->store.Owned(self->id, self->uid);
                    if (metadata.ready || metadata.offset != self->offset) throw Error(409, "upload offset mismatch");
                    if (!length || *length > metadata.size - metadata.offset) throw Error(413, "invalid upload length");
                } else if (length && *length > 8192) throw Error(413, "metadata too large");
                return Json::Value();
            }, /** @brief 前置认证及上传校验成功后开始读取正文。 */ [self = shared_from_this()](const Json::Value&) { self->ReadBody(); });
        } catch (const Error& error) {
            keep_alive = false; Json::Value result; result["error"] = error.what(); Reply(error.status, result);
        }
    }
    /** @brief 分块读取请求正文，上传块落盘完成后才继续读取后续块。 */
    void ReadBody() {
        if (parser->is_done()) { Dispatch(); return; }
        parser->get().body().data = block.data(); parser->get().body().size = block.size();
        stream.expires_after(std::chrono::seconds(30));
        http::async_read_some(stream, buffer, *parser, /** @brief 读取上传块后先检查关闭与错误状态，再安排文件追加。 */ [self = shared_from_this()](beast::error_code error, std::size_t) {
            if (self->closed) return;
            const auto count = self->block.size() - self->parser->get().body().size;
            if (error == http::error::need_buffer) error = {};
            if (count && self->patch) {
                // Persist any received prefix even when the peer disconnects mid-request.
                const auto at = self->offset;
                self->Run(/** @brief 在资源执行器按预期偏移追加本次上传块。 */ [self, count, at] {
                    return Describe(self->server->store.Append(self->id, self->uid, at, self->block.data(), count));
                }, /** @brief 追加成功后推进已接受偏移，读错误则关闭连接。 */ [self, error, count](const Json::Value&) {
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
    /** @brief 按资源路由执行业务动作，将结果或资源错误转换为 HTTP 响应。 */
    void Dispatch() {
        const auto method = parser->get().method();
        if (method == http::verb::get && route == "/resources/" + id) { Download(); return; }
        Run(/** @brief 执行头像查询、发布或资源上传状态相关路由。 */ [self = shared_from_this(), method] {
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
        }, /** @brief 业务路由完成后发送成功 JSON 响应。 */ [self = shared_from_this()](const Json::Value& value) { self->Reply(200, value); });
    }
    /** @brief 异步写出 JSON 状态响应，完成后按连接策略继续请求或关闭 socket。 */
    void Reply(int code, const Json::Value& value) {
        auto response = std::make_shared<http::response<http::string_body>>(static_cast<http::status>(code), 11);
        response->set(http::field::content_type, "application/json");
        response->keep_alive(keep_alive); response->body() = Json(value); response->prepare_payload();
        stream.expires_after(std::chrono::seconds(30));
        http::async_write(stream, *response, /** @brief JSON 响应写完后按 keep-alive 与停服状态复用或关闭连接。 */ [self = shared_from_this(), response](beast::error_code error, std::size_t) {
            if (error || !self->keep_alive || self->server->stopping) self->Close(); else self->Start();
        });
    }
    /** @brief 校验资源就绪、读取权限与 Range 后发送下载响应头及文件块。 */
    void Download() {
        const std::string range(parser->get()[http::field::range]);
        Run(/** @brief 核验下载资源就绪、读取权限及范围边界。 */ [self = shared_from_this(), range] {
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
        }, /** @brief 按已验证范围构造下载头和流式发送状态。 */ [self = shared_from_this(), range](const Json::Value& value) {
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
            http::async_write_header(self->stream, *self->download_serializer, /** @brief 响应头写完后开始发送文件块，失败关闭连接。 */ [self](beast::error_code error, std::size_t) {
                if (error) self->Close(); else self->WriteFile();
            });
        });
    }
    /** @brief 在工作执行器读取下一块文件，写入 socket 成功后推进偏移，失败关闭连接。 */
    void WriteFile() {
        if (!remaining) {
            download_serializer.reset(); download_header.reset();
            if (keep_alive && !server->stopping) Start(); else Close();
            return;
        }
        auto self = shared_from_this();
        net::post(server->storage, /** @brief 在工作执行器读取下一个有界文件块并捕获读取错误。 */ [self] {
            std::vector<char> bytes; bool failed = false;
            try { bytes = self->server->store.Read(self->id, self->offset, ResourceStore::BUFFER_SIZE); }
            catch (...) { failed = true; }
            net::post(self->stream.get_executor(), /** @brief 在连接执行器接收文件块，已关闭或读失败时停止发送。 */ [self, bytes = std::move(bytes), failed]() mutable {
                if (self->closed) return;
                if (failed || bytes.empty()) { self->Close(); return; }
                self->outgoing = std::move(bytes);
                self->stream.expires_after(std::chrono::seconds(30));
                net::async_write(self->stream, net::buffer(self->outgoing), /** @brief 成功写出文件块后推进偏移及剩余字节数。 */ [self](beast::error_code error, std::size_t count) {
                    if (error) { self->Close(); return; }
                    self->offset += count; self->remaining -= count; self->WriteFile();
                });
            });
        });
    }
};
/** @brief 安排下一次资源 HTTP 连接接收，并在运行状态与会话数限制内启动会话。 */
void ResourceHttpServer::Impl::Accept() {
    if (stopping) return;
    acceptor.async_accept(/** @brief 接受资源 HTTP 连接并落实并发会话上限，随后继续监听。 */ [self = shared_from_this()](beast::error_code error, tcp::socket socket) {
        if (!error && !self->stopping && self->sessions.size() < 32) {
            auto session = std::make_shared<Session>(std::move(socket), self);
            self->sessions.insert(session); session->Start();
        }
        if (!self->stopping) self->Accept();
    });
}
/** @brief 停止接收新工作并关闭当前服务的监听或执行器；后续销毁由所属生命周期流程负责。 */
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
