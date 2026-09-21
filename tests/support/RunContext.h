#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace integration {

using RunDeadline = std::chrono::steady_clock::time_point;

struct LoopbackPort {
	std::string address;
	std::uint16_t port = 0;
};

struct ProcessIdentity {
	std::uint32_t pid = 0;
	std::uint64_t creation_time = 0;
};

bool operator==(const ProcessIdentity& left, const ProcessIdentity& right) noexcept;

struct ProcessSlot {
	std::uint64_t value = 0;
};

class CleanupStatus {
public:
	static CleanupStatus Success(std::string detail = {});
	static CleanupStatus Failure(std::string detail);

	bool Complete() const noexcept;
	const std::string& Detail() const noexcept;

private:
	CleanupStatus(bool complete, std::string detail);

	bool complete_ = false;
	std::string detail_;
};

struct RunOutcome {
	bool complete = false;
	std::optional<std::string> primary_failure;
	std::vector<std::string> cleanup_failures;
};

using CleanupAction = std::function<CleanupStatus()>;

class RunContext {
public:
	static std::unique_ptr<RunContext> Create(RunDeadline deadline);

	~RunContext();
	RunContext(const RunContext&) = delete;
	RunContext& operator=(const RunContext&) = delete;
	RunContext(RunContext&&) = delete;
	RunContext& operator=(RunContext&&) = delete;

	const std::string& RunId() const noexcept;
	RunDeadline Deadline() const noexcept;
	std::string SyntheticIdentity(const std::string& logical_name) const;
	const std::filesystem::path& TempRoot() const noexcept;

	LoopbackPort ReserveLoopbackPort(const std::string& name);
	bool ReleaseLoopbackPort(const std::string& name);
	std::filesystem::path CreateOwnedDirectory(const std::filesystem::path& relative_path);
	bool CleanupOwnedPath(const std::filesystem::path& path);

	ProcessSlot ReserveProcessSlot(const std::string& name);
	void CommitProcess(ProcessSlot slot, ProcessIdentity identity, CleanupAction cleanup);
	bool IsOwnedProcess(ProcessIdentity identity) const;
	bool CleanupProcess(ProcessIdentity identity);

	void RecordPrimaryFailure(std::string failure);
	RunOutcome Teardown() noexcept;

private:
	class Impl;
	explicit RunContext(std::unique_ptr<Impl> impl);

	std::unique_ptr<Impl> impl_;
};

} // namespace integration
