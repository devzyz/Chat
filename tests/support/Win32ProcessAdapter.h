#pragma once

#include "ProcessAdapter.h"

#include <memory>

namespace integration::internal {

class Win32ProcessAdapter {
public:
	Win32ProcessAdapter();
	~Win32ProcessAdapter();
	Win32ProcessAdapter(const Win32ProcessAdapter&) = delete;
	Win32ProcessAdapter& operator=(const Win32ProcessAdapter&) = delete;

	ProcessIdentity Start(const ProcessSpec& spec);
	bool IsRunning(ProcessIdentity expected) const;
	bool WaitForExitUntil(RunDeadline deadline) const;
	bool SendGraceful(ProcessIdentity expected);
	TerminationResult Terminate(ProcessIdentity expected, RunDeadline deadline);
	bool ClosePipes(RunDeadline deadline);
	AdapterEvidence CollectEvidence() const;

private:
	class Impl;
	std::unique_ptr<Impl> impl_;
};

} // namespace integration::internal
