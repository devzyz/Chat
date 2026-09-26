#pragma once
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace chat::logging {
/** @brief 持有服务滚动日志器；关闭时仅刷新，保留全局注册表供静态析构使用。 */
class RotatingLog {
public:
    /** @brief 初始化未关闭状态，日志器由 Init 显式配置。 */
    RotatingLog();
    /** @brief 幂等刷新日志器并释放自身引用，最终注册表清理由 spdlog 负责。 */
    ~RotatingLog();
    /** @brief 幂等刷新已有日志器，不注销或关闭 spdlog 注册表。 */
    void Close();
    /** @brief 按配置建立滚动文件日志器及全局默认入口；转换或创建失败返回 false。 */
    bool Init(const std::string& name, const std::string& directory,
        const std::string& max_size_mb, const std::string& max_total_files,
        const std::string& level, const std::string& flush_level);
private:
    /** @brief 将配置字符串映射为日志级别，未知值使用默认级别。 */
    spdlog::level::level_enum GetLevel(const std::string& level, spdlog::level::level_enum default_level);
    std::atomic<bool> _b_stop;
    std::shared_ptr<spdlog::logger> _logger;
};

/** @brief 初始化未关闭状态，日志器由 Init 显式配置。 */
inline RotatingLog::RotatingLog() : _b_stop(false) {
}

/** @brief 幂等刷新日志器，保留注册表供静态析构使用。 */
inline RotatingLog::~RotatingLog() {
	Close();
}

/** @brief 幂等刷新已有日志器，不注销或关闭 spdlog 注册表。 */
inline void RotatingLog::Close() {
	if (_b_stop.exchange(true)) {
		return;
	}
	if (_logger) {
		_logger->flush();
	}
	// Static service destructors may still log after Close(). The spdlog
	// registry owns final teardown; application close only flushes its sink.
}

/** @brief 将配置字符串映射为日志级别，未知值使用默认级别。 */
inline spdlog::level::level_enum RotatingLog::GetLevel(
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

/** @brief 按配置创建滚动日志器及默认入口，转换或创建失败返回 false。 */
inline bool RotatingLog::Init(
    const std::string& log_name, const std::string& log_dir,
    const std::string& log_max_size_mb, const std::string& log_max_total_files,
    const std::string& log_level, const std::string& log_flush_level) {

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

} // namespace chat::logging
