#pragma once

#include "ProcessAdapter.h"

#include <memory>

namespace integration::internal {

// Linux PID + /proc start ticks are checked before group signals. The leader
// remains waitable (WNOWAIT) until group cleanup, preventing PID/group reuse.
class PosixProcessAdapter {
public:
    PosixProcessAdapter();
    ~PosixProcessAdapter();
    PosixProcessAdapter(const PosixProcessAdapter&) = delete;
    PosixProcessAdapter& operator=(const PosixProcessAdapter&) = delete;

    ProcessIdentity Start(const ProcessSpec& spec);
    bool IsRunning(ProcessIdentity expected) const;
    bool WaitForExitUntil(RunDeadline deadline) const;
    bool SendGraceful(ProcessIdentity expected);
    TerminationResult Terminate(ProcessIdentity expected, RunDeadline deadline);
    bool ClosePipes(RunDeadline deadline);
    AdapterEvidence CollectEvidence() const;

private:
    class Impl;
    std::unique_ptr<Impl> _impl;
};

} // namespace integration::internal
