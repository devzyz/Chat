#include "RunContext.h"

#include <boost/asio.hpp>

#include <Windows.h>

#include <array>
#include <iomanip>
#include <mutex>
#include <random>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace integration {
namespace {

bool IsBoundedFutureDeadline(RunDeadline deadline) {
	return deadline != RunDeadline{} && deadline != RunDeadline::max() &&
		deadline > std::chrono::steady_clock::now();
}

bool IsSafeName(const std::string& value) {
	if (value.empty()) {
		return false;
	}
	for (const auto character : value) {
		if (!((character >= 'a' && character <= 'z') ||
			  (character >= 'A' && character <= 'Z') ||
			  (character >= '0' && character <= '9') ||
			  character == '-' || character == '_')) {
			return false;
		}
	}
	return true;
}

std::mutex live_ids_mutex;
std::unordered_set<std::string> live_ids;

std::string GenerateRunId() {
	std::random_device random;
	for (int attempt = 0; attempt < 100; ++attempt) {
		std::array<std::uint32_t, 4> words{};
		for (auto& word : words) {
			word = random();
		}
		std::ostringstream value;
		value << std::hex << std::setfill('0');
		for (const auto word : words) {
			value << std::setw(8) << word;
		}
		const auto candidate = value.str();
		std::lock_guard<std::mutex> lock(live_ids_mutex);
		if (live_ids.insert(candidate).second) {
			return candidate;
		}
	}
	throw std::runtime_error("unable to allocate a unique run identity");
}

void ReleaseRunId(const std::string& run_id) noexcept {
	std::lock_guard<std::mutex> lock(live_ids_mutex);
	live_ids.erase(run_id);
}

std::string ProcessKey(ProcessIdentity identity) {
	return std::to_string(identity.pid) + ":" + std::to_string(identity.creation_time);
}

} // namespace

bool operator==(const ProcessIdentity& left, const ProcessIdentity& right) noexcept {
	return left.pid == right.pid && left.creation_time == right.creation_time;
}

CleanupStatus::CleanupStatus(bool complete, std::string detail)
	: complete_(complete), detail_(std::move(detail)) {
}

CleanupStatus CleanupStatus::Success(std::string detail) {
	return CleanupStatus(true, std::move(detail));
}

CleanupStatus CleanupStatus::Failure(std::string detail) {
	return CleanupStatus(false, std::move(detail));
}

bool CleanupStatus::Complete() const noexcept {
	return complete_;
}

const std::string& CleanupStatus::Detail() const noexcept {
	return detail_;
}

class RunContext::Impl {
public:
	struct LedgerEntry {
		std::string label;
		CleanupAction cleanup;
		bool executed = false;
	};

	explicit Impl(RunDeadline requested_deadline)
		: run_id(GenerateRunId()), deadline(requested_deadline) {
		try {
			temp_root = std::filesystem::temp_directory_path() / ("chat-process-harness-" + run_id);
			std::error_code error;
			if (!std::filesystem::create_directory(temp_root, error)) {
				throw std::runtime_error("refusing to adopt a pre-existing run temp root");
			}
			owned_paths.insert(temp_root.lexically_normal().wstring());
			ledger.push_back({"temp-root", [this] { return RemovePath(temp_root); }, false});
		} catch (...) {
			ReleaseRunId(run_id);
			throw;
		}
	}

	~Impl() {
		ReleaseRunId(run_id);
	}

	CleanupStatus RemovePath(const std::filesystem::path& path) {
		std::error_code error;
		std::filesystem::remove_all(path, error);
		if (error) {
			return CleanupStatus::Failure("unable to remove owned path: " + error.message());
		}
		owned_paths.erase(path.lexically_normal().wstring());
		return CleanupStatus::Success("owned path removed");
	}

	CleanupStatus ClosePort(const std::string& name) {
		const auto found = ports.find(name);
		if (found == ports.end()) {
			return CleanupStatus::Success("loopback port already released");
		}
		boost::system::error_code error;
		found->second->close(error);
		ports.erase(found);
		if (error) {
			return CleanupStatus::Failure("unable to release owned loopback port: " + error.message());
		}
		return CleanupStatus::Success("loopback port released");
	}

	CleanupStatus RunEntry(LedgerEntry& entry) noexcept {
		if (entry.executed) {
			return CleanupStatus::Success("cleanup already completed");
		}
		entry.executed = true;
		try {
			return entry.cleanup();
		} catch (const std::exception& error) {
			return CleanupStatus::Failure(std::string("cleanup threw: ") + error.what());
		} catch (...) {
			return CleanupStatus::Failure("cleanup threw an unknown exception");
		}
	}

	const std::string run_id;
	const RunDeadline deadline;
	std::filesystem::path temp_root;
	boost::asio::io_context io_context;
	std::unordered_map<std::string, std::unique_ptr<boost::asio::ip::tcp::acceptor>> ports;
	std::unordered_set<std::wstring> owned_paths;
	std::unordered_map<std::uint64_t, std::string> process_slots;
	std::unordered_map<std::string, ProcessIdentity> processes;
	std::unordered_map<std::string, std::size_t> process_ledger;
	std::uint64_t next_slot = 1;
	std::vector<LedgerEntry> ledger;
	std::optional<std::string> primary_failure;
	bool torn_down = false;
	RunOutcome outcome;
	mutable std::mutex mutex;
};

std::unique_ptr<RunContext> RunContext::Create(RunDeadline deadline) {
	if (!IsBoundedFutureDeadline(deadline)) {
		throw std::invalid_argument("run deadline must be finite, absolute, and in the future");
	}
	return std::unique_ptr<RunContext>(new RunContext(std::make_unique<Impl>(deadline)));
}

RunContext::RunContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {
}

RunContext::~RunContext() {
	Teardown();
}

const std::string& RunContext::RunId() const noexcept {
	return impl_->run_id;
}

RunDeadline RunContext::Deadline() const noexcept {
	return impl_->deadline;
}

std::string RunContext::SyntheticIdentity(const std::string& logical_name) const {
	if (!IsSafeName(logical_name)) {
		throw std::invalid_argument("synthetic identity name must be non-empty and portable");
	}
	return impl_->run_id + "-" + logical_name;
}

const std::filesystem::path& RunContext::TempRoot() const noexcept {
	return impl_->temp_root;
}

LoopbackPort RunContext::ReserveLoopbackPort(const std::string& name) {
	if (!IsSafeName(name)) {
		throw std::invalid_argument("loopback port name must be non-empty and portable");
	}
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->torn_down || impl_->ports.find(name) != impl_->ports.end()) {
		throw std::invalid_argument("loopback port name is already registered or the run ended");
	}

	auto acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(impl_->io_context);
	boost::system::error_code error;
	acceptor->open(boost::asio::ip::tcp::v4(), error);
	if (error) {
		throw std::runtime_error("unable to open loopback reservation: " + error.message());
	}
	const BOOL exclusive = TRUE;
	if (setsockopt(
			acceptor->native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
			reinterpret_cast<const char*>(&exclusive), sizeof(exclusive)) == SOCKET_ERROR) {
		throw std::runtime_error("unable to make loopback reservation exclusive");
	}
	acceptor->bind({boost::asio::ip::address_v4::loopback(), 0}, error);
	if (error) {
		throw std::runtime_error("unable to bind loopback reservation: " + error.message());
	}
	acceptor->listen(boost::asio::socket_base::max_listen_connections, error);
	if (error) {
		throw std::runtime_error("unable to listen on loopback reservation: " + error.message());
	}
	const auto endpoint = acceptor->local_endpoint();
	impl_->ports.emplace(name, std::move(acceptor));
	impl_->ledger.push_back({"port:" + name, [impl = impl_.get(), name] { return impl->ClosePort(name); }, false});
	return {endpoint.address().to_string(), endpoint.port()};
}

bool RunContext::ReleaseLoopbackPort(const std::string& name) {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	for (auto& entry : impl_->ledger) {
		if (entry.label == "port:" + name && !entry.executed) {
			return impl_->RunEntry(entry).Complete();
		}
	}
	return false;
}

std::filesystem::path RunContext::CreateOwnedDirectory(const std::filesystem::path& relative_path) {
	if (relative_path.empty() || relative_path.is_absolute()) {
		throw std::invalid_argument("owned directory must be a non-empty relative path");
	}
	for (const auto& component : relative_path) {
		if (component == "." || component == "..") {
			throw std::invalid_argument("owned directory cannot escape the run temp root");
		}
	}
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->torn_down) {
		throw std::logic_error("cannot add resources after teardown");
	}
	const auto path = (impl_->temp_root / relative_path).lexically_normal();
	std::error_code error;
	if (!std::filesystem::create_directory(path, error)) {
		throw std::invalid_argument("refusing to adopt a pre-existing or invalid owned directory");
	}
	impl_->owned_paths.insert(path.wstring());
	impl_->ledger.push_back({"path:" + path.string(), [impl = impl_.get(), path] { return impl->RemovePath(path); }, false});
	return path;
}

bool RunContext::CleanupOwnedPath(const std::filesystem::path& path) {
	const auto normalized = path.lexically_normal();
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->owned_paths.find(normalized.wstring()) == impl_->owned_paths.end()) {
		return false;
	}
	for (auto& entry : impl_->ledger) {
		if (entry.label == "path:" + normalized.string() && !entry.executed) {
			return impl_->RunEntry(entry).Complete();
		}
	}
	return false;
}

ProcessSlot RunContext::ReserveProcessSlot(const std::string& name) {
	if (!IsSafeName(name)) {
		throw std::invalid_argument("process slot name must be non-empty and portable");
	}
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->torn_down) {
		throw std::logic_error("cannot reserve a process after teardown");
	}
	for (const auto& slot : impl_->process_slots) {
		if (slot.second == name) {
			throw std::invalid_argument("process slot name is already reserved");
		}
	}
	const ProcessSlot slot{impl_->next_slot++};
	impl_->process_slots.emplace(slot.value, name);
	return slot;
}

void RunContext::CommitProcess(ProcessSlot slot, ProcessIdentity identity, CleanupAction cleanup) {
	if (identity.pid == 0 || identity.creation_time == 0 || !cleanup) {
		throw std::invalid_argument("owned process requires PID, creation time, and cleanup");
	}
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const auto reserved = impl_->process_slots.find(slot.value);
	if (impl_->torn_down || reserved == impl_->process_slots.end()) {
		throw std::invalid_argument("process ownership slot is not reserved by this run");
	}
	const auto key = ProcessKey(identity);
	if (impl_->processes.find(key) != impl_->processes.end()) {
		throw std::invalid_argument("process identity is already registered");
	}
	impl_->process_slots.erase(reserved);
	impl_->processes.emplace(key, identity);
	impl_->ledger.push_back({"process:" + key, std::move(cleanup), false});
	impl_->process_ledger.emplace(key, impl_->ledger.size() - 1);
}

bool RunContext::IsOwnedProcess(ProcessIdentity identity) const {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	return impl_->processes.find(ProcessKey(identity)) != impl_->processes.end();
}

bool RunContext::CleanupProcess(ProcessIdentity identity) {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	const auto key = ProcessKey(identity);
	const auto found = impl_->process_ledger.find(key);
	if (found == impl_->process_ledger.end()) {
		return false;
	}
	auto& entry = impl_->ledger[found->second];
	if (entry.executed) {
		return false;
	}
	const auto status = impl_->RunEntry(entry);
	if (status.Complete()) {
		impl_->processes.erase(key);
	}
	return status.Complete();
}

void RunContext::RecordPrimaryFailure(std::string failure) {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (!impl_->primary_failure.has_value()) {
		impl_->primary_failure = std::move(failure);
	}
}

RunOutcome RunContext::Teardown() noexcept {
	std::lock_guard<std::mutex> lock(impl_->mutex);
	if (impl_->torn_down) {
		return impl_->outcome;
	}
	impl_->torn_down = true;
	impl_->outcome.primary_failure = impl_->primary_failure;
	for (auto entry = impl_->ledger.rbegin(); entry != impl_->ledger.rend(); ++entry) {
		const auto status = impl_->RunEntry(*entry);
		if (!status.Complete()) {
			impl_->outcome.cleanup_failures.push_back(status.Detail());
		}
	}
	impl_->processes.clear();
	impl_->outcome.complete = !impl_->outcome.primary_failure.has_value() &&
		impl_->outcome.cleanup_failures.empty();
	return impl_->outcome;
}

} // namespace integration
