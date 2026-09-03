#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

class CSession;

struct LogicMessage {
	std::shared_ptr<CSession> session;
	std::int16_t id;
	std::string body;
};

enum class LogicSubmitResult {
	Accepted,
	Full,
	Closed,
};

class LogicDispatcher {
public:
	using Handler = std::function<bool(const LogicMessage&)>;

	explicit LogicDispatcher(Handler handler);
	~LogicDispatcher();

	LogicDispatcher(const LogicDispatcher&) = delete;
	LogicDispatcher& operator=(const LogicDispatcher&) = delete;

	LogicSubmitResult Submit(LogicMessage message);
	void Stop();

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};
