#pragma once
#include "Singleton.h"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <atomic>

class LogMgr : public Singleton<LogMgr>
{
	friend class Singleton<LogMgr>;
public:
	~LogMgr();

	void Close();
	bool InitLogMgr();
private:
	LogMgr();

	spdlog::level::level_enum GetLevel(const std::string& level, spdlog::level::level_enum default_level);

	std::atomic<bool> _b_stop;
	std::shared_ptr<spdlog::logger> _logger;
};
