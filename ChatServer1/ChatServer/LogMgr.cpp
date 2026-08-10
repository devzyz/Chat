#include "LogMgr.h"
#include <filesystem>
#include "ConfigMgr.h"
#include <iostream>

LogMgr::LogMgr() : _b_stop(false) {

}

LogMgr::~LogMgr() {
    Close();
}

void LogMgr::Close() {
    if (_b_stop) return;
    // 如果logger存在，先flush,确保缓冲日志落盘
    if (_logger) {
        _logger->flush();
    }

    // 清理spdlog全局资源
    spdlog::shutdown();
}

spdlog::level::level_enum LogMgr::GetLevel(const std::string& level, spdlog::level::level_enum default_level) {
    if (level == "trace") {
        return spdlog::level::trace;
    }
    if (level == "debug") {
        return spdlog::level::debug;
    }
    if (level == "info") {
        return spdlog::level::info;
    }
    if (level == "warn") {
        return spdlog::level::warn;
    }
    if (level == "error") {
        return spdlog::level::err;
    }
    if (level == "critical") {
        return spdlog::level::critical;
    }

    return default_level;
}

bool LogMgr::InitLogMgr() {
    auto& config_mgr = ConfigMgr::GetInstance();
    auto log_config = config_mgr["Log"];

    // 当前日志所属的服务名
    auto log_name = log_config["Name"];
    // 日志根目录名
    auto log_dir = log_config["LogDir"];
    // 单个日志文件最大多少MB
    auto log_max_size_mb = log_config["MaxSizeMB"];
    // 允许最多存在多少日志文件
    auto log_max_total_files = log_config["MaxTotalFiles"];
    // 允许记录到日志中的等级
    auto log_level = log_config["Level"];
    // 需要马上记录到日志中的等级
    auto log_flush_level = log_config["FlushLevel"];

    // 获取每个文件最大能存多少字节
    const auto max_file_size = std::stoi(log_max_size_mb) * 1024 * 1024;
    // spdlog库要求的max_file为除去当前正在写的文件，还允许多少存在
    const auto rotated_files = std::stoi(log_max_total_files) - 1;

    try {
        // 拿到当前服务的输出目录，如果不存在则创建目录
        const std::filesystem::path service_log_dir =
            std::filesystem::path(log_dir) / log_name;
        std::filesystem::create_directories(service_log_dir);

        // 拿到要输出的当前日志文件
        const auto log_file =
            (service_log_dir / (log_name + ".txt")).string();

        // 创建多线程安全的logger
        _logger = spdlog::rotating_logger_mt(
            log_name,
            log_file,
            max_file_size,
            rotated_files);

        // 设置最低输出级别
        _logger->set_level(GetLevel(log_level, spdlog::level::info));

        // 设置flush级别
        _logger->flush_on(GetLevel(log_flush_level, spdlog::level::warn));

        // 格式化日志正文
        // [%Y-%m-%d %H:%M:%S.%e] 时间
        // [%n] logger名，其实就是当前的服务器名
        // [%l] 日志级别
        // [tid:%t] 线程ID
        // [%s:%#] 源文件名和行号
        // %v 日志正文
        _logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] [tid:%t] [%s:%#] %v");

        // 设置为当前服务的默认Logger
        spdlog::set_default_logger(_logger);

        // 周期性flush一次
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
    catch (std::exception& e) {
        SPDLOG_ERROR("LogMgr init failed, error={}", e.what());

        _logger.reset();
        return false;
    }
}
