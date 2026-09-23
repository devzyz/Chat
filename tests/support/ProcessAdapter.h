#pragma once

#include "ProcessHarness.h"

namespace integration::internal {

enum class TerminationResult {
    Terminated,
    AlreadyExited,
    IdentityMismatch,
    TimedOut,
    Failed,
};

/** 保存平台进程身份、退出状态、原始捕获输出及管道关闭状态；公开前由上层脱敏。 */
struct AdapterEvidence {
    ProcessIdentity identity;
    std::optional<std::uint32_t> exit_code;
    std::string stdout_text;
    std::string stderr_text;
    bool pipes_closed = false;
};

} // namespace integration::internal
