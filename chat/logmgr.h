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

/** @brief 管理客户端日志实例及 Qt 消息到日志级别的转换。 */
class LogMgr : public Singleton<LogMgr>
{
    friend class Singleton<LogMgr>;

public:
    /** @brief 调用 Close 恢复 Qt 消息处理器并关闭日志资源。 */
    ~LogMgr();

    /** @brief 初始化日志目标与级别，并注册 Qt 消息转发处理器。 */
    bool InitLogMgr();
    /** @brief 关闭日志输出并释放日志运行资源。 */
    void Close();
    /** @brief 查询日志管理器是否已完成初始化。 */
    bool IsInitialized() const;

    /** @brief 将 Qt 文本转换为 UTF-8 标准字符串供日志使用。 */
    static std::string ToUtf8(const QString& value);

private:
    /** @brief 初始化对象，用于管理客户端日志实例及 Qt 消息到日志级别的转换。 */
    LogMgr();

    /** @brief 将 Qt 框架消息按类型转发到客户端日志。 */
    static void HandleQtMessage(
        QtMsgType type,
        const QMessageLogContext& context,
        const QString& message);

    /** @brief 将配置级别转换为日志库级别。 */
    spdlog::level::level_enum GetLevel(
        const QString& level,
        spdlog::level::level_enum defaultLevel) const;

    std::atomic<bool> _closed;
    std::atomic<bool> _initialized;
    std::shared_ptr<spdlog::logger> _logger;
    QtMessageHandler _previousQtMessageHandler;
};

#endif // LOGMGR_H
