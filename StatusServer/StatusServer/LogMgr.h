#pragma once
#include "Singleton.h"
#include "../../common/logging/RotatingLog.h"

/** @brief 将本服务配置适配到共享滚动日志器，关闭时仅刷新日志。 */
class LogMgr : public Singleton<LogMgr>, public chat::logging::RotatingLog {
    friend class Singleton<LogMgr>;
public:
    /** @brief 从 ConfigMgr 初始化日志，成功后记录脱敏配置，创建失败返回 false。 */
    bool InitLogMgr();
private:
    /** @brief 构造服务单例，不打开日志文件。 */
    LogMgr() = default;
};
