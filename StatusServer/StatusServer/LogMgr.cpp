#include "LogMgr.h"
#include "ConfigMgr.h"

bool LogMgr::InitLogMgr() {
    auto& config_mgr = ConfigMgr::GetInstance();
    auto log = config_mgr["Log"];
    if (!Init(log["Name"], log["LogDir"], log["MaxSizeMB"], log["MaxTotalFiles"],
            log["Level"], log["FlushLevel"])) {
        return false;
    }
    config_mgr.DumpLoadedConfig();
    return true;
}
