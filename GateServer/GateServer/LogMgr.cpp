#include "LogMgr.h"
#include <filesystem>
#include "ConfigMgr.h"

LogMgr::LogMgr() : _b_stop(false) {
}

LogMgr::~LogMgr() {
	Close();
}

void LogMgr::Close() {
	if (_b_stop.exchange(true)) {
		return;
	}
	if (_logger) {
		_logger->flush();
	}
	// Static service destructors may still log after Close(). The spdlog
	// registry owns final teardown; application close only flushes its sink.
}

spdlog::level::level_enum LogMgr::GetLevel(
	const std::string& level,
	spdlog::level::level_enum default_level) {
	if (level == "trace") return spdlog::level::trace;
	if (level == "debug") return spdlog::level::debug;
	if (level == "info") return spdlog::level::info;
	if (level == "warn") return spdlog::level::warn;
	if (level == "error") return spdlog::level::err;
	if (level == "critical") return spdlog::level::critical;
	return default_level;
}

bool LogMgr::InitLogMgr() {
	auto& config_mgr = ConfigMgr::GetInstance();
	auto log_config = config_mgr["Log"];

	const auto log_name = log_config["Name"];
	const auto log_dir = log_config["LogDir"];
	const auto log_max_size_mb = log_config["MaxSizeMB"];
	const auto log_max_total_files = log_config["MaxTotalFiles"];
	const auto log_level = log_config["Level"];
	const auto log_flush_level = log_config["FlushLevel"];

	try {
		const auto max_file_size =
			static_cast<std::size_t>(std::stoull(log_max_size_mb)) * 1024 * 1024;
		const auto max_total_files = std::stoull(log_max_total_files);
		if (log_name.empty() || log_dir.empty() || max_file_size == 0 || max_total_files == 0) {
			throw std::invalid_argument("invalid [Log] configuration");
		}
		const auto rotated_files = static_cast<std::size_t>(max_total_files - 1);
		const std::filesystem::path service_log_dir =
			std::filesystem::path(log_dir) / log_name;
		std::filesystem::create_directories(service_log_dir);
		const auto log_file =
			(service_log_dir / (log_name + ".txt")).string();

		_logger = spdlog::rotating_logger_mt(
			log_name,
			log_file,
			max_file_size,
			rotated_files);
		_logger->set_level(GetLevel(log_level, spdlog::level::info));
		_logger->flush_on(GetLevel(log_flush_level, spdlog::level::warn));
		_logger->set_pattern(
			"[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid:%t] [%s:%#] %v");
		spdlog::set_default_logger(_logger);
		spdlog::flush_every(std::chrono::seconds(3));

		config_mgr.DumpLoadedConfig();
		SPDLOG_INFO(
			"logger initialized, file={}, max_size_mb={}, max_total_files={}, rotated_files={}",
			log_file,
			log_max_size_mb,
			log_max_total_files,
			rotated_files);
		return true;
	}
	catch (const std::exception& e) {
		SPDLOG_ERROR("LogMgr init failed, error={}", e.what());
		_logger.reset();
		return false;
	}
}
