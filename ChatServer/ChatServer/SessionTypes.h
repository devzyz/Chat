#pragma once
#include <cstdint>
#include <functional>
#include <string>

using SessionId = std::string;
enum class SessionState { Created, Active, Closing };
enum class SessionCloseReason {
    ServerShutdown, PeerClosed, ReadError, WriteError, HeartbeatTimeout,
    ProtocolError, LogicUnavailable, Replaced, LocalRequest
};
enum class SessionSendResult { Accepted, Full, NotActive };
enum class SessionBindResult { Bound, NotActive, AlreadyBound, Unavailable };
/** @brief 持有消息 ID 与消息体的待发送帧，提交后不借用调用方缓冲。 */
struct SessionFrame {
    std::uint16_t message_id = 0;
    std::string body;
};
using SendCompletion = std::function<void(SessionSendResult)>;
using BindCompletion = std::function<void(SessionBindResult)>;
