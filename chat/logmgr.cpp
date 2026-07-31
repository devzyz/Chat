#include "logmgr.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>

#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <stdexcept>

namespace {

constexpr int kDefaultMaxSizeMb = 10;
constexpr int kDefaultMaxTotalFiles = 10;
constexpr int kDefaultFlushIntervalSeconds = 3;

spdlog::level::level_enum ToSpdlogLevel(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return spdlog::level::debug;
    case QtInfoMsg:
        return spdlog::level::info;
    case QtWarningMsg:
        return spdlog::level::warn;
    case QtCriticalMsg:
        return spdlog::level::err;
    case QtFatalMsg:
        return spdlog::level::critical;
    }

    return spdlog::level::info;
}

} // namespace

LogMgr::LogMgr()
    : _closed(false),
      _initialized(false),
      _previousQtMessageHandler(nullptr)
{
}

LogMgr::~LogMgr()
{
    Close();
}

bool LogMgr::InitLogMgr()
{
    if (_initialized.load()) {
        return true;
    }

    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QString configPath = QDir(applicationDir).filePath("config.ini");
    QSettings settings(configPath, QSettings::IniFormat);

    const QString logName =
        settings.value("Log/Name", "chat").toString().trimmed();
    const QString logDirectoryName =
        settings.value("Log/Directory", "Logs").toString().trimmed();
    const int maxSizeMb =
        settings.value("Log/MaxSizeMB", kDefaultMaxSizeMb).toInt();
    const int maxTotalFiles =
        settings.value("Log/MaxTotalFiles", kDefaultMaxTotalFiles).toInt();
    const QString configuredLevel =
        settings.value("Log/Level", "auto").toString().trimmed();
    const QString flushLevel =
        settings.value("Log/FlushLevel", "warn").toString().trimmed();
    const int flushIntervalSeconds =
        settings.value(
                    "Log/FlushIntervalSeconds",
                    kDefaultFlushIntervalSeconds).toInt();

    try {
        if (logName.isEmpty() || logDirectoryName.isEmpty()) {
            throw std::invalid_argument("log name or directory is empty");
        }
        if (maxSizeMb <= 0 || maxTotalFiles <= 0 ||
            flushIntervalSeconds <= 0) {
            throw std::invalid_argument("numeric log configuration is invalid");
        }

        constexpr std::size_t bytesPerMb = 1024ULL * 1024ULL;
        const auto maxSize =
            static_cast<std::size_t>(maxSizeMb) * bytesPerMb;
        if (maxSize / bytesPerMb != static_cast<std::size_t>(maxSizeMb)) {
            throw std::overflow_error("log file size is too large");
        }

        QDir applicationDirectory(applicationDir);
        if (!applicationDirectory.mkpath(logDirectoryName)) {
            throw std::runtime_error("failed to create log directory");
        }

        const QString logDirectory =
            applicationDirectory.filePath(logDirectoryName);
        const QString logFile =
            QDir(logDirectory).filePath(logName + ".log");
        const std::string logFilePath =
            QFile::encodeName(QDir::toNativeSeparators(logFile)).toStdString();
        const auto rotatedFiles =
            static_cast<std::size_t>(maxTotalFiles - 1);

        _logger = spdlog::rotating_logger_mt(
            ToUtf8(logName),
            logFilePath,
            maxSize,
            rotatedFiles);
        _logger->set_level(
            GetLevel(configuredLevel, spdlog::level::info));
        _logger->flush_on(
            GetLevel(flushLevel, spdlog::level::warn));
        _logger->set_pattern(
            "[%Y-%m-%d %H:%M:%S.%e] [%n] [%l] "
            "[tid:%t] [%s:%#] %v");

        spdlog::set_default_logger(_logger);
        spdlog::flush_every(
            std::chrono::seconds(flushIntervalSeconds));

        _previousQtMessageHandler =
            qInstallMessageHandler(&LogMgr::HandleQtMessage);
        _closed.store(false);
        _initialized.store(true);

        SPDLOG_INFO(
            "logger initialized, file={}, max_size_mb={}, "
            "max_total_files={}, level={}, flush_level={}, "
            "flush_interval_seconds={}",
            logFilePath,
            maxSizeMb,
            maxTotalFiles,
            ToUtf8(configuredLevel),
            ToUtf8(flushLevel),
            flushIntervalSeconds);
        return true;
    }
    catch (const std::exception&) {
        if (_logger) {
            _logger->flush();
        }
        _logger.reset();
        spdlog::shutdown();
        _initialized.store(false);
        return false;
    }
}

void LogMgr::Close()
{
    if (_closed.exchange(true)) {
        return;
    }

    if (_initialized.exchange(false)) {
        qInstallMessageHandler(_previousQtMessageHandler);
    }

    if (_logger) {
        _logger->flush();
    }
    spdlog::shutdown();
    _logger.reset();
}

bool LogMgr::IsInitialized() const
{
    return _initialized.load();
}

std::string LogMgr::ToUtf8(const QString& value)
{
    const QByteArray utf8 = value.toUtf8();
    return std::string(utf8.constData(), static_cast<std::size_t>(utf8.size()));
}

spdlog::level::level_enum LogMgr::GetLevel(
    const QString& level,
    spdlog::level::level_enum defaultLevel) const
{
    const QString normalized = level.trimmed().toLower();
    if (normalized == "trace") return spdlog::level::trace;
    if (normalized == "debug") return spdlog::level::debug;
    if (normalized == "info") return spdlog::level::info;
    if (normalized == "warn" || normalized == "warning") {
        return spdlog::level::warn;
    }
    if (normalized == "error") return spdlog::level::err;
    if (normalized == "critical") return spdlog::level::critical;
    if (normalized == "off") return spdlog::level::off;
    if (normalized == "auto") {
#ifdef NDEBUG
        return spdlog::level::info;
#else
        return spdlog::level::debug;
#endif
    }

    return defaultLevel;
}

void LogMgr::HandleQtMessage(
    QtMsgType type,
    const QMessageLogContext& context,
    const QString& message)
{
    auto logger = spdlog::default_logger();
    if (!logger) {
        return;
    }

    const std::string formattedMessage = ToUtf8(message);
    const spdlog::source_loc sourceLocation(
        context.file ? context.file : "",
        context.line,
        context.function ? context.function : "");
    logger->log(
        sourceLocation,
        ToSpdlogLevel(type),
        "{}",
        formattedMessage);
    if (type == QtFatalMsg) {
        logger->flush();
        std::abort();
    }
}
