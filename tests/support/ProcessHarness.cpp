#include "ProcessHarness.h"

#include "Win32ProcessAdapter.h"

#include <algorithm>
#include <chrono>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace integration {
namespace {

bool IsBoundedDeadline(RunDeadline deadline) {
	return deadline != RunDeadline{} && deadline != RunDeadline::max();
}

bool IsWithin(const std::filesystem::path& child, const std::filesystem::path& root) {
	const auto normalized_child = std::filesystem::weakly_canonical(child);
	const auto normalized_root = std::filesystem::weakly_canonical(root);
	auto child_part = normalized_child.begin();
	for (auto root_part = normalized_root.begin(); root_part != normalized_root.end(); ++root_part, ++child_part) {
		if (child_part == normalized_child.end() || *child_part != *root_part) {
			return false;
		}
	}
	return true;
}

} // namespace

class ProcessHarness::Impl {
public:
	Impl(RunContext& owner, ProcessIdentity process_identity, std::shared_ptr<internal::Win32ProcessAdapter> process_adapter)
		: context(owner), identity(process_identity), adapter(std::move(process_adapter)) {
	}

	CleanupStatus StopOwned(RunDeadline graceful_deadline) {
		std::lock_guard<std::mutex> lock(mutex);
		if (stop_result.has_value()) {
			return *stop_result;
		}
		if (!IsBoundedDeadline(graceful_deadline)) {
			stop_result = CleanupStatus::Failure("stop deadline is missing or unbounded");
			return *stop_result;
		}

		if (adapter->IsRunning(identity)) {
			graceful_stop_attempted = true;
			adapter->SendGraceful(identity);
			if (!adapter->WaitForExitUntil(graceful_deadline)) {
				escalated = true;
				const auto termination = adapter->Terminate(identity, context.Deadline());
				if (termination == internal::TerminationResult::IdentityMismatch) {
					identity_mismatch_refused = true;
					stop_result = CleanupStatus::Failure("refused to terminate a mismatched process identity");
					return *stop_result;
				}
				if (termination != internal::TerminationResult::Terminated &&
					termination != internal::TerminationResult::AlreadyExited) {
					stop_result = CleanupStatus::Failure("owned process did not terminate before its hard deadline");
					return *stop_result;
				}
			}
		}
		if (!adapter->ClosePipes(context.Deadline())) {
			stop_result = CleanupStatus::Failure("child pipe readers did not close before the hard deadline");
			return *stop_result;
		}
		stop_result = CleanupStatus::Success(escalated ? "owned process stopped by bounded escalation" : "owned process stopped gracefully");
		return *stop_result;
	}

	RunContext& context;
	ProcessIdentity identity;
	std::shared_ptr<internal::Win32ProcessAdapter> adapter;
	mutable std::mutex mutex;
	bool ready_probe_attempted = false;
	bool ready_probe_succeeded = false;
	bool graceful_stop_attempted = false;
	bool escalated = false;
	bool identity_mismatch_refused = false;
	std::optional<CleanupStatus> stop_result;
};

std::unique_ptr<ProcessHarness> ProcessHarness::Start(RunContext& context, ProcessSpec spec) {
	if (!IsBoundedDeadline(context.Deadline()) || std::chrono::steady_clock::now() >= context.Deadline()) {
		throw std::invalid_argument("process start requires a live owning run deadline");
	}
	if (!IsWithin(spec.working_directory, context.TempRoot())) {
		throw std::invalid_argument("process working directory must be inside its run-owned temp root");
	}
	const auto slot = context.ReserveProcessSlot("child-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	auto adapter = std::make_shared<internal::Win32ProcessAdapter>();
	const auto identity = adapter->Start(spec);
	auto impl = std::make_shared<Impl>(context, identity, adapter);
	try {
		context.CommitProcess(slot, identity, [impl] { return impl->StopOwned(impl->context.Deadline()); });
	} catch (...) {
		adapter->Terminate(identity, context.Deadline());
		adapter->ClosePipes(context.Deadline());
		throw;
	}
	return std::unique_ptr<ProcessHarness>(new ProcessHarness(std::move(impl)));
}

ProcessHarness::ProcessHarness(std::shared_ptr<Impl> impl) : impl_(std::move(impl)) {
}

ProcessHarness::~ProcessHarness() {
	impl_->StopOwned(impl_->context.Deadline());
	impl_->context.CleanupProcess(impl_->identity);
}

ProcessIdentity ProcessHarness::Identity() const {
	return impl_->identity;
}

bool ProcessHarness::WaitReady(const ReadyProbe& probe, RunDeadline deadline) {
	if (!probe || !IsBoundedDeadline(deadline) || deadline > impl_->context.Deadline()) {
		return false;
	}
	{
		std::lock_guard<std::mutex> lock(impl_->mutex);
		impl_->ready_probe_attempted = true;
	}
	while (std::chrono::steady_clock::now() < deadline) {
		if (probe()) {
			std::lock_guard<std::mutex> lock(impl_->mutex);
			impl_->ready_probe_succeeded = true;
			return true;
		}
		if (!impl_->adapter->IsRunning(impl_->identity)) {
			impl_->adapter->ClosePipes(deadline);
			return false;
		}
		const auto next_event_wait = std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(10));
		impl_->adapter->WaitForExitUntil(next_event_wait);
	}
	return false;
}

CleanupStatus ProcessHarness::Stop(RunDeadline graceful_deadline) {
	return impl_->StopOwned(graceful_deadline);
}

ProcessEvidence ProcessHarness::CollectEvidence() const {
	const auto adapter_evidence = impl_->adapter->CollectEvidence();
	std::lock_guard<std::mutex> lock(impl_->mutex);
	ProcessEvidence evidence;
	evidence.identity = adapter_evidence.identity;
	evidence.exit_code = adapter_evidence.exit_code;
	evidence.stdout_text = adapter_evidence.stdout_text;
	evidence.stderr_text = adapter_evidence.stderr_text;
	evidence.ready_probe_attempted = impl_->ready_probe_attempted;
	evidence.ready_probe_succeeded = impl_->ready_probe_succeeded;
	evidence.graceful_stop_attempted = impl_->graceful_stop_attempted;
	evidence.escalated = impl_->escalated;
	evidence.identity_mismatch_refused = impl_->identity_mismatch_refused;
	evidence.pipe_readers_closed = adapter_evidence.pipes_closed;
	return evidence;
}

} // namespace integration
