#pragma once

#include "ProcessAdapter.h"

#include <memory>

namespace integration::internal {

// Linux PID + /proc start ticks are checked before group signals. The leader
// remains waitable (WNOWAIT) until group cleanup, preventing PID/group reuse.
/** 拥有 Posix 子进程与输出管道，按完整进程身份执行有界控制。 */
class PosixProcessAdapter {
public:
    /** 创建空的平台进程实现。 */
    PosixProcessAdapter();
    /** 兜底清理仍拥有的子进程并释放平台句柄与读取资源；正常路径应先显式停止。 */
    ~PosixProcessAdapter();
    /** 禁止复制平台进程资源。 */
    PosixProcessAdapter(const PosixProcessAdapter&) = delete;
    /** 禁止复制赋值平台进程资源。 */
    PosixProcessAdapter& operator=(const PosixProcessAdapter&) = delete;

    /** 按指定程序及参数启动所属子进程，返回可校验的进程身份；失败抛异常。 */
    ProcessIdentity Start(const ProcessSpec& spec);
    /** 核对完整进程身份并查询是否仍在运行。 */
    bool IsRunning(ProcessIdentity expected) const;
    /** 等待所属进程退出至给定期限，期限到达返回假。 */
    bool WaitForExitUntil(RunDeadline deadline) const;
    /** 只向身份匹配的所属进程发出正常退出请求。 */
    bool SendGraceful(ProcessIdentity expected);
    /** 核对身份后在期限内强制终止，明确区分身份不匹配、已退出及终止失败。 */
    TerminationResult Terminate(ProcessIdentity expected, RunDeadline deadline);
    /** 在期限内结束管道读取并关闭所属资源，未完成返回假。 */
    bool ClosePipes(RunDeadline deadline);
    /** 返回进程退出及捕获输出证据，调用方负责公开前脱敏。 */
    AdapterEvidence CollectEvidence() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace integration::internal
