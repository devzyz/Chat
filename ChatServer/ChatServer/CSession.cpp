#include "CSession.h"
#include "SessionLifecycleCoordinator.h"
#include "UserSessionDirectory.h"
#include <boost/uuid.hpp>
#include <spdlog/spdlog.h>

CSession::CSession(boost::asio::io_context& io, std::shared_ptr<SessionLifecycleCoordinator> lifecycle,
    std::shared_ptr<UserSessionDirectory> directory, Submit submit)
    : _strand(boost::asio::make_strand(io)), _socket(io), _heartbeat(io),
      _lifecycle(std::move(lifecycle)), _directory(std::move(directory)), _submit(std::move(submit)),
      _id(boost::uuids::to_string(boost::uuids::random_generator()())) {}

boost::asio::ip::tcp::socket& CSession::Socket() { return _socket; }
void CSession::Start() {
    boost::asio::post(_strand, [self = shared_from_this()] {
        if (self->_state != SessionState::Created) return;
        self->_state = SessionState::Active;
        self->_last_activity = std::chrono::steady_clock::now();
        self->ReadHeader();
        self->ArmHeartbeat();
    });
}
void CSession::Inspect(std::function<void(SessionState, std::optional<SessionCloseReason>)> completion) {
    boost::asio::post(_strand, [self = shared_from_this(), completion = std::move(completion)] {
        completion(self->_state, self->_close_reason);
    });
}
void CSession::Close(SessionCloseReason reason) {
    boost::asio::post(_strand, [self = shared_from_this(), reason] { self->BeginClosing(reason); });
}
void CSession::BeginClosing(SessionCloseReason reason) {
    if (_state == SessionState::Closing) return;
    _state = SessionState::Closing;
    _close_reason = reason;
    const int uid = _authenticated_uid.exchange(0);
    _lifecycle->OnClosing(_id, uid ? uid : _binding_uid);
    _frames.clear(); // The in-flight write owns its buffer independently.
    boost::system::error_code ignored;
    _heartbeat.cancel();
    _socket.cancel(ignored);
    _socket.close(ignored);
    ReleaseIfIdle();
}
void CSession::ReleaseIfIdle() {
    if (_state == SessionState::Closing && _io_pending == 0 && !_binding && !_released) {
        _released = true;
        _lifecycle->ReleaseOwnership(_id);
    }
}
void CSession::ReadHeader() {
    if (_state != SessionState::Active) return;
    ++_io_pending;
    boost::asio::async_read(_socket, boost::asio::buffer(_header), boost::asio::bind_executor(_strand,
        [self = shared_from_this()](boost::system::error_code error, std::size_t) {
            --self->_io_pending;
            if (self->_state != SessionState::Active) { self->ReleaseIfIdle(); return; }
            if (error) {
                self->BeginClosing(error == boost::asio::error::eof ? SessionCloseReason::PeerClosed
                    : SessionCloseReason::ReadError);
                return;
            }
            auto header = ChatFrameCodec::DecodeValidatedHeader(self->_header.data(), MAX_LENGTH);
            if (!header) { self->BeginClosing(SessionCloseReason::ProtocolError); return; }
            self->ReadBody(header->message_id, header->body_length);
        }));
}
void CSession::ReadBody(std::uint16_t id, std::size_t length) {
    if (_state != SessionState::Active) return;
    _body.assign(length, '\0');
    if (!length) { OnFrame(id); return; }
    ++_io_pending;
    boost::asio::async_read(_socket, boost::asio::buffer(_body), boost::asio::bind_executor(_strand,
        [self = shared_from_this(), id](boost::system::error_code error, std::size_t) {
            --self->_io_pending;
            if (self->_state != SessionState::Active) { self->ReleaseIfIdle(); return; }
            if (error) {
                self->BeginClosing(error == boost::asio::error::eof ? SessionCloseReason::PeerClosed
                    : SessionCloseReason::ReadError);
                return;
            }
            self->OnFrame(id);
        }));
}
void CSession::OnFrame(std::uint16_t id) {
    _last_activity = std::chrono::steady_clock::now();
    try {
        const auto result = _submit({shared_from_this(), static_cast<std::int16_t>(id), std::move(_body)});
        if (result == LogicSubmitResult::Closed) { BeginClosing(SessionCloseReason::LogicUnavailable); return; }
        if (result == LogicSubmitResult::Full) SPDLOG_WARN("logic message rejected, reason=full, msg_id={}", id);
        ReadHeader();
    } catch (const std::exception& error) {
        SPDLOG_ERROR("session dispatch failed: {}", error.what());
        BeginClosing(SessionCloseReason::LogicUnavailable);
    }
}
void CSession::Send(const std::string& body, std::uint16_t id, SendCompletion completion) {
    Send({id, body}, std::move(completion));
}
void CSession::Send(SessionFrame frame, SendCompletion completion) {
    boost::asio::post(_strand, [self = shared_from_this(), frame = std::move(frame),
        completion = std::move(completion)]() mutable {
        auto result = SessionSendResult::NotActive;
        if (self->_state == SessionState::Active) {
            const auto limit = frame.message_id == MSG_LOAD_CHAT_MESSAGE_RSP ? MAX_HISTORY_BODY_LENGTH : MAX_LENGTH;
            if (frame.body.size() > limit) {
                self->BeginClosing(SessionCloseReason::ProtocolError);
            } else if (self->_frames.size() >= MAX_SENDQUE) {
                result = SessionSendResult::Full;
            } else {
                const auto header = ChatFrameCodec::EncodeHeader(frame.message_id,
                    static_cast<std::uint16_t>(frame.body.size()));
                auto buffer = std::make_shared<std::string>(reinterpret_cast<const char*>(header.data()), header.size());
                buffer->append(frame.body);
                self->_frames.push_back(std::move(buffer));
                result = SessionSendResult::Accepted;
                if (!self->_write_active) self->StartWrite();
            }
        }
        if (completion) completion(result); // Admission only; callback must not block or throw.
        else if (result != SessionSendResult::Accepted)
            SPDLOG_DEBUG("session send rejected, result={}", static_cast<int>(result));
    });
}
void CSession::StartWrite() {
    if (_state != SessionState::Active || _frames.empty()) return;
    _write_active = true;
    ++_io_pending;
    auto buffer = _frames.front();
    boost::asio::async_write(_socket, boost::asio::buffer(*buffer), boost::asio::bind_executor(_strand,
        [self = shared_from_this(), buffer](boost::system::error_code error, std::size_t) {
            --self->_io_pending;
            self->_write_active = false;
            if (self->_state != SessionState::Active) { self->ReleaseIfIdle(); return; }
            if (error) { self->BeginClosing(SessionCloseReason::WriteError); return; }
            self->_frames.pop_front();
            self->StartWrite();
        }));
}
void CSession::ArmHeartbeat() {
    if (_state != SessionState::Active) return;
    _heartbeat.expires_after(std::chrono::seconds(60));
    ++_io_pending;
    _heartbeat.async_wait(boost::asio::bind_executor(_strand,
        [self = shared_from_this()](boost::system::error_code error) {
            --self->_io_pending;
            if (self->_state != SessionState::Active) { self->ReleaseIfIdle(); return; }
            if (error) { self->BeginClosing(SessionCloseReason::LocalRequest); return; }
            if (std::chrono::steady_clock::now() - self->_last_activity >
                std::chrono::seconds(HEARTBEAT_TIME_INTERVAL)) {
                self->BeginClosing(SessionCloseReason::HeartbeatTimeout);
                return;
            }
            self->ArmHeartbeat();
        }));
}
void CSession::BindAuthenticatedUser(int uid, BindCompletion completion) {
    boost::asio::post(_strand, [self = shared_from_this(), uid, completion = std::move(completion)]() mutable {
        if (self->_state != SessionState::Active) { completion(SessionBindResult::NotActive); return; }
        if (uid <= 0 || self->_binding || self->_authenticated_uid.load()) {
            completion(SessionBindResult::AlreadyBound); return;
        }
        self->_binding = true;
        self->_binding_uid = uid;
        self->_lifecycle->OnAuthenticated(self, uid, std::move(completion));
    });
}
void CSession::FinishBinding(int uid, SessionBindResult result, BindCompletion completion,
    std::shared_ptr<std::atomic<bool>> cancelled, std::function<void(bool)> committed) {
    boost::asio::post(_strand, [self = shared_from_this(), uid, result, completion = std::move(completion),
        cancelled, committed = std::move(committed)]() mutable {
        self->_binding = false;
        if (self->_state != SessionState::Active || cancelled->load()) result = SessionBindResult::NotActive;
        if (result == SessionBindResult::Bound) {
            auto old = self->_directory->Register(uid, self->_id, self);
            self->_authenticated_uid.store(uid);
            if (old && old != self) old->Close(SessionCloseReason::Replaced);
        }
        committed(result == SessionBindResult::Bound);
        completion(result);
        self->ReleaseIfIdle();
    });
}
