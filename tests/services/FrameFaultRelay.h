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
/** 在所属 loopback 端点转发 Chat 帧，按 UUID 注入 ACK 丢弃与通知重放；事件循环单线程驱动。 */
class FrameFaultRelay {
    using Tcp = boost::asio::ip::tcp;
    /** 持有一组前后端连接，异步操作保持自身存活；所属中继须活过所有回调。 */
    struct Pair : std::enable_shared_from_this<Pair> {
        /** 创建前后端套接字并借用所属中继。 */
        Pair(boost::asio::io_context& io, FrameFaultRelay& owner) : front(io), back(io), relay(owner) {}
        Tcp::socket front, back;
        FrameFaultRelay& relay;
        /** 关闭本会话两端连接，忽略重复关闭错误。 */
        void Close() {
            boost::system::error_code ignored;
            front.close(ignored); back.close(ignored);
        }
        /** 读取并验证完整帧后执行故障策略，再继续当前方向转发。 */
        void Read(bool upstream) {
            auto self = shared_from_this();
            auto header = std::make_shared<ChatFrameCodec::HeaderBytes>();
            auto& source = upstream ? front : back;
            boost::asio::async_read(source, boost::asio::buffer(*header),
                /** 核对帧头与长度，错误时关闭会话，否则继续读取载荷。 */ [self, header, upstream](auto error, auto) {
                    if (error) { self->Close(); return; }
                    const auto decoded = ChatFrameCodec::DecodeValidatedHeader(header->data(), MAX_LENGTH);
                    if (!decoded) { self->relay.failed = true; self->Close(); return; }
                    auto body = std::make_shared<std::string>(decoded->body_length, '\0');
                    auto& source = upstream ? self->front : self->back;
                    boost::asio::async_read(source, boost::asio::buffer(*body),
                        /** 解析消息并按目标 UUID 丢弃 ACK 或重放通知，然后转发完整字节。 */ [self, header, body, upstream, id = decoded->message_id](auto body_error, auto) {
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
                                /** 转发失败时关闭会话，成功时继续读取同方向下一帧。 */ [self, bytes, upstream](auto write_error, auto) {
                                    if (write_error) self->Close(); else self->Read(upstream);
                                });
                        });
                });
        }
    };
public:
    /** 绑定所属端点，保存后端、故障 UUID 和报告路径。 */
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
    /** 运行中继直到停止信号或总截止时间，关闭连接并写最终证据。 */
    int Run(const std::function<bool()>& stopped) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(420);
        Accept();
        std::function<void()> tick;
        tick = /** 安排下一次停止和总截止时间检查。 */ [&] {
            timer.expires_after(std::chrono::milliseconds(100));
            timer.async_wait(/** 停止或超时时结束事件循环，否则继续安排检查。 */ [&](auto error) {
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
    /** 限制并发会话数并异步接收下一条前端连接。 */
    void Accept() {
        pairs.erase(std::remove_if(pairs.begin(), pairs.end(), /** 识别已释放的会话以清理弱引用记录。 */ [](const auto& pair) { return pair.expired(); }), pairs.end());
        if (pairs.size() >= 64) { failed = true; io.stop(); return; }
        auto pair = std::make_shared<Pair>(io, *this);
        pairs.push_back(pair);
        acceptor.async_accept(pair->front, /** 接收成功后连接后端，并立即继续接受下一会话。 */ [this, pair](auto error) {
            if (error) return;
            pair->back.async_connect({boost::asio::ip::make_address("127.0.0.1"), backend}, /** 后端连接成功后启动双向读取，失败则关闭会话。 */ [pair](auto connect_error) {
                if (connect_error) pair->Close(); else { pair->Read(true); pair->Read(false); }
            });
            Accept();
        });
    }
    /** 写入就绪、故障命中和提交标识证据，写入失败标记整体失败。 */
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
