#ifndef LOGMGR_H
#define LOGMGR_H

#include "singleton.h"

#include <QString>
#include <QtGlobal>
#include <atomic>
#include <memory>
#include <string>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/spdlog.h>

class LogMgr : public Singleton<LogMgr>
{
    friend class Singleton<LogMgr>;

public:
    ~LogMgr();

    bool InitLogMgr();
    void Close();
    bool IsInitialized() const;

    static std::string ToUtf8(const QString& value);

private:
    LogMgr();

    static void HandleQtMessage(
        QtMsgType type,
        const QMessageLogContext& context,
        const QString& message);

    spdlog::level::level_enum GetLevel(
        const QString& level,
        spdlog::level::level_enum defaultLevel) const;

    std::atomic<bool> _closed;
    std::atomic<bool> _initialized;
    std::shared_ptr<spdlog::logger> _logger;
    QtMessageHandler _previousQtMessageHandler;
};

#endif // LOGMGR_H
