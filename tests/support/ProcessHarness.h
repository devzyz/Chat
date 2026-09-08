#pragma once

#include "RunContext.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace integration {

struct ProcessSpec {
	std::filesystem::path executable;
	std::vector<std::wstring> arguments;
	std::filesystem::path working_directory;
	std::size_t evidence_limit = 64 * 1024;
};

struct ProcessEvidence {
	ProcessIdentity identity;
	std::optional<std::uint32_t> exit_code;
	std::string stdout_text;
	std::string stderr_text;
	bool ready_probe_attempted = false;
	bool ready_probe_succeeded = false;
	bool graceful_stop_attempted = false;
	bool escalated = false;
	bool identity_mismatch_refused = false;
	bool pipe_readers_closed = false;
};

using ReadyProbe = std::function<bool()>;

class ProcessHarness {
public:
	static std::unique_ptr<ProcessHarness> Start(RunContext& context, ProcessSpec spec);

	~ProcessHarness();
	ProcessHarness(const ProcessHarness&) = delete;
	ProcessHarness& operator=(const ProcessHarness&) = delete;
	ProcessHarness(ProcessHarness&&) = delete;
	ProcessHarness& operator=(ProcessHarness&&) = delete;

	ProcessIdentity Identity() const;
	bool WaitReady(const ReadyProbe& probe, RunDeadline deadline);
	CleanupStatus Stop(RunDeadline graceful_deadline);
	ProcessEvidence CollectEvidence() const;

private:
	class Impl;
	explicit ProcessHarness(std::shared_ptr<Impl> impl);

	std::shared_ptr<Impl> impl_;
};

} // namespace integration
