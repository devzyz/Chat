#include "ChatSessionState.h"
#include "ChatSessionStateInternal.h"
#include "Const.h"

#include <deque>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <utility>

struct ChatSessionState::Session {
	std::string id;
	std::shared_ptr<SessionWriter> writer;
	std::weak_ptr<Impl> owner;
	int uid = 0;
	bool registered = false;
	bool closed = false;
	bool write_active = false;
	std::uint64_t write_generation = 0;
	std::deque<SessionFrame> frames;
};

struct ChatSessionState::Impl : public std::enable_shared_from_this<Impl> {
	Impl(std::shared_ptr<SessionIdSource> source, std::shared_ptr<SessionPresence> session_presence)
		: id_source(std::move(source)), presence(std::move(session_presence)) {}

	std::mutex mutex;
	std::unordered_map<std::string, std::shared_ptr<Session>> sessions;
	std::unordered_map<int, std::shared_ptr<Session>> current;
	std::shared_ptr<SessionIdSource> id_source;
	std::shared_ptr<SessionPresence> presence;

	void StartWrite(const std::shared_ptr<Session>& session, SessionFrame frame, std::uint64_t generation) {
		std::weak_ptr<Impl> weak_self = shared_from_this();
		try {
			session->writer->Write(std::move(frame),
				[weak_self, session, generation](bool success) {
					if (const auto self = weak_self.lock()) {
						self->OnWrite(session, generation, success);
					}
				});
		}
		catch (...) {
			OnWrite(session, generation, false);
		}
	}

	void OnWrite(const std::shared_ptr<Session>& session, std::uint64_t generation, bool success) {
		SessionFrame next;
		std::uint64_t next_generation = 0;
		bool start_next = false;
		bool cleanup = false;
		bool close_writer = false;
		int cleanup_uid = 0;
		std::string cleanup_id;
		{
			std::lock_guard<std::mutex> lock(mutex);
			if (session->closed || !session->write_active || session->write_generation != generation) {
				return;
			}
			if (!success) {
				close_writer = CloseLocked(session, cleanup, cleanup_uid, cleanup_id);
			}
			else {
				session->frames.pop_front();
				if (session->frames.empty()) {
					session->write_active = false;
				}
				else {
					next = session->frames.front();
					next_generation = ++session->write_generation;
					start_next = true;
				}
			}
		}
		if (close_writer) {
			session->writer->Close();
		}
		if (cleanup) {
			presence->Cleanup(cleanup_uid, cleanup_id);
		}
		if (start_next) {
			StartWrite(session, std::move(next), next_generation);
		}
	}

	bool CloseLocked(const std::shared_ptr<Session>& session, bool& cleanup,
		int& cleanup_uid, std::string& cleanup_id) {
		if (session->closed) {
			return false;
		}
		session->closed = true;
		session->frames.clear();
		sessions.erase(session->id);
		if (!session->registered) {
			return true;
		}
		const auto found = current.find(session->uid);
		if (found == current.end() || found->second != session) {
			return true;
		}
		cleanup_uid = session->uid;
		cleanup_id = session->id;
		current.erase(found);
		cleanup = true;
		return true;
	}
};

ChatSessionState::Handle::Handle(std::shared_ptr<Session> session)
	: session_(std::move(session)) {}

ChatSessionState::Handle::operator bool() const noexcept {
	return static_cast<bool>(session_);
}

bool operator==(const ChatSessionState::Handle& left, const ChatSessionState::Handle& right) noexcept {
	return left.session_ == right.session_;
}

ChatSessionState::ChatSessionState(
	std::shared_ptr<SessionIdSource> id_source,
	std::shared_ptr<SessionPresence> presence)
	: impl_(std::make_shared<Impl>(std::move(id_source), std::move(presence))) {
	if (!impl_->id_source || !impl_->presence) {
		throw std::invalid_argument("chat session state dependencies must not be null");
	}
}

ChatSessionState::~ChatSessionState() = default;

ChatSessionState::Handle ChatSessionState::Create(std::shared_ptr<SessionWriter> writer) {
	if (!writer) {
		throw std::invalid_argument("session writer must not be null");
	}

	std::lock_guard<std::mutex> lock(impl_->mutex);
	for (;;) {
		auto id = impl_->id_source->Next();
		if (id.empty()) {
			throw std::runtime_error("session ID source returned an empty ID");
		}
		if (impl_->sessions.find(id) != impl_->sessions.end()) {
			continue;
		}
		auto session = std::make_shared<Session>();
		session->id = std::move(id);
		session->writer = std::move(writer);
		session->owner = impl_;
		impl_->sessions.emplace(session->id, session);
		return Handle(std::move(session));
	}
}

void ChatSessionState::RegisterCurrent(const Handle& handle, int uid) {
	if (!handle.session_ || handle.session_->owner.lock() != impl_) {
		throw std::invalid_argument("session handle does not belong to this state");
	}
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);

        if (uid <= 0 || handle.session_->closed
            || (handle.session_->registered && handle.session_->uid != uid)) {
            throw std::invalid_argument("cannot register closed or rebound session");
        }
		handle.session_->uid = uid;
		handle.session_->registered = true;
		impl_->current[uid] = handle.session_;
	}
	impl_->presence->Register(uid, handle.session_->id);
}

ChatSessionState::Handle ChatSessionState::FindCurrent(int uid) const {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const auto found = impl_->current.find(uid);
	if (found == impl_->current.end()) {
		return {};
	}
	return Handle(found->second);
}

int ChatSessionState::AuthenticatedUid(const Handle& handle) const {
    if (!handle.session_ || handle.session_->owner.lock() != impl_) {
        return 0;
    }
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const auto& session = handle.session_;
    if (session->closed || !session->registered) {
        return 0;
    }
    const auto current = impl_->current.find(session->uid);
    return current != impl_->current.end() && current->second == session ? session->uid : 0;
}

void ChatSessionState::Close(const Handle& handle) {
	if (!handle.session_ || handle.session_->owner.lock() != impl_) {
		return;
	}

	int cleanup_uid = 0;
	std::string cleanup_id;
	bool cleanup = false;
	bool close_writer = false;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		close_writer = impl_->CloseLocked(handle.session_, cleanup, cleanup_uid, cleanup_id);
	}
	if (close_writer) {
		handle.session_->writer->Close();
	}
	if (cleanup) {
		impl_->presence->Cleanup(cleanup_uid, cleanup_id);
	}
}

SessionSendResult ChatSessionState::Send(const Handle& handle, SessionFrame frame) {
	if (!handle.session_ || handle.session_->owner.lock() != impl_) {
		return SessionSendResult::Closed;
	}

	bool start_write = false;
	std::uint64_t generation = 0;
	SessionFrame first;
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		if (handle.session_->closed) {
			return SessionSendResult::Closed;
		}
		if (handle.session_->frames.size() >= MAX_SENDQUE) {
			return SessionSendResult::Full;
		}
		handle.session_->frames.push_back(std::move(frame));
		if (!handle.session_->write_active) {
			handle.session_->write_active = true;
			generation = ++handle.session_->write_generation;
			first = handle.session_->frames.front();
			start_write = true;
		}
	}
	if (start_write) {
		impl_->StartWrite(handle.session_, std::move(first), generation);
	}
	return SessionSendResult::Accepted;
}
