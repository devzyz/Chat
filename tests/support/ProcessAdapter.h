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

struct AdapterEvidence {
    ProcessIdentity identity;
    std::optional<std::uint32_t> exit_code;
    std::string stdout_text;
    std::string stderr_text;
    bool pipes_closed = false;
};

} // namespace integration::internal
