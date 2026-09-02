#pragma once

#include <cstdint>
#include <memory>
#include <string>

class SessionIdSource;
class SessionPresence;
class SessionWriter;

struct SessionFrame {
	std::uint16_t message_id = 0;
	std::string body;
};

enum class SessionSendResult {
	Accepted,
	Full,
	Closed,
};

class ChatSessionState {
	struct Session;
	struct Impl;
public:
	class Handle {
		friend class ChatSessionState;
	public:
		Handle() = default;
		explicit operator bool() const noexcept;
		friend bool operator==(const Handle& left, const Handle& right) noexcept;
		friend bool operator!=(const Handle& left, const Handle& right) noexcept {
			return !(left == right);
		}
	private:
		explicit Handle(std::shared_ptr<Session> session);
		std::shared_ptr<Session> session_;
	};

	ChatSessionState(
		std::shared_ptr<SessionIdSource> id_source,
		std::shared_ptr<SessionPresence> presence);
	~ChatSessionState();

	ChatSessionState(const ChatSessionState&) = delete;
	ChatSessionState& operator=(const ChatSessionState&) = delete;

	Handle Create(std::shared_ptr<SessionWriter> writer);
	void RegisterCurrent(const Handle& handle, int uid);
	Handle FindCurrent(int uid) const;
	void Close(const Handle& handle);
	SessionSendResult Send(const Handle& handle, SessionFrame frame);

private:
	std::shared_ptr<Impl> impl_;
};
