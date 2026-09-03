#pragma once

#include "ChatSessionState.h"

#include <functional>
#include <string>

class SessionIdSource {
public:
	virtual ~SessionIdSource() = default;
	virtual std::string Next() = 0;
};

class SessionWriter {
public:
	using Completion = std::function<void(bool)>;
	virtual ~SessionWriter() = default;
	virtual void Write(SessionFrame frame, Completion completion) = 0;
	virtual void Close() = 0;
};

class SessionPresence {
public:
	virtual ~SessionPresence() = default;
	virtual void Register(int uid, const std::string& session_id) = 0;
	virtual void Cleanup(int uid, const std::string& session_id) = 0;
};
