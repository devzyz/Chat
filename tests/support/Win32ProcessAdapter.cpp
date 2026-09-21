#include "Win32ProcessAdapter.h"

#define NOMINMAX
#include <Windows.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

namespace integration::internal {
namespace {

std::wstring QuoteArgument(const std::wstring& value) {
	std::wstring quoted = L"\"";
	std::size_t slashes = 0;
	for (const auto character : value) {
		if (character == L'\\') {
			++slashes;
			continue;
		}
		if (character == L'\"') {
			quoted.append(slashes * 2 + 1, L'\\');
			quoted.push_back(L'\"');
		} else {
			quoted.append(slashes, L'\\');
			quoted.push_back(character);
		}
		slashes = 0;
	}
	quoted.append(slashes * 2, L'\\');
	quoted.push_back(L'\"');
	return quoted;
}

DWORD RemainingMilliseconds(RunDeadline deadline) {
	const auto now = std::chrono::steady_clock::now();
	if (deadline <= now) {
		return 0;
	}
	const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - now).count();
	return static_cast<DWORD>((std::min)(remaining, static_cast<std::int64_t>((std::numeric_limits<DWORD>::max)() - 1)));
}

std::uint64_t FileTimeValue(const FILETIME& value) {
	ULARGE_INTEGER converted{};
	converted.LowPart = value.dwLowDateTime;
	converted.HighPart = value.dwHighDateTime;
	return converted.QuadPart;
}

} // namespace

class Win32ProcessAdapter::Impl {
public:
	~Impl() {
		if (process != nullptr) {
			const auto expected = identity;
			if (Matches(expected) && WaitForSingleObject(process, 0) == WAIT_TIMEOUT) {
				TerminateProcess(process, 97);
				WaitForSingleObject(process, 1000);
			}
		}
		ClosePipeReaders(std::chrono::steady_clock::now() + std::chrono::seconds(1));
		if (process_thread != nullptr) {
			CloseHandle(process_thread);
		}
		if (process != nullptr) {
			CloseHandle(process);
		}
		if (job != nullptr) {
			CloseHandle(job);
		}
	}

	bool Matches(ProcessIdentity expected) const {
		if (process == nullptr || expected.pid == 0 || expected.creation_time == 0 ||
			GetProcessId(process) != expected.pid) {
			return false;
		}
		FILETIME creation{}, exit{}, kernel{}, user{};
		return GetProcessTimes(process, &creation, &exit, &kernel, &user) &&
			FileTimeValue(creation) == expected.creation_time;
	}

	void ReadPipe(HANDLE pipe, std::string& output) {
		std::array<char, 4096> buffer{};
		for (;;) {
			DWORD read = 0;
			if (!ReadFile(pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr) || read == 0) {
				break;
			}
			std::lock_guard<std::mutex> lock(output_mutex);
			const auto available = output_limit > output.size() ? output_limit - output.size() : 0;
			output.append(buffer.data(), std::min<std::size_t>(available, read));
		}
	}

	bool JoinReader(std::thread& reader, RunDeadline deadline) {
		if (!reader.joinable()) {
			return true;
		}
		const auto wait_result = WaitForSingleObject(reader.native_handle(), RemainingMilliseconds(deadline));
		if (wait_result != WAIT_OBJECT_0) {
			CancelSynchronousIo(reader.native_handle());
			if (WaitForSingleObject(reader.native_handle(), RemainingMilliseconds(deadline)) != WAIT_OBJECT_0) {
				return false;
			}
		}
		reader.join();
		return true;
	}

	bool ClosePipeReaders(RunDeadline deadline) {
		if (pipes_closed) {
			return true;
		}
		const bool stdout_closed = JoinReader(stdout_reader, deadline);
		const bool stderr_closed = JoinReader(stderr_reader, deadline);
		if (stdout_closed && stdout_read != nullptr) {
			CloseHandle(stdout_read);
			stdout_read = nullptr;
		}
		if (stderr_closed && stderr_read != nullptr) {
			CloseHandle(stderr_read);
			stderr_read = nullptr;
		}
		pipes_closed = stdout_closed && stderr_closed;
		return pipes_closed;
	}

	HANDLE process = nullptr;
	HANDLE process_thread = nullptr;
	HANDLE job = nullptr;
	HANDLE stdout_read = nullptr;
	HANDLE stderr_read = nullptr;
	std::thread stdout_reader;
	std::thread stderr_reader;
	ProcessIdentity identity;
	std::size_t output_limit = 0;
	mutable std::mutex output_mutex;
	std::string stdout_text;
	std::string stderr_text;
	bool pipes_closed = false;
};

Win32ProcessAdapter::Win32ProcessAdapter() : impl_(std::make_unique<Impl>()) {
}

Win32ProcessAdapter::~Win32ProcessAdapter() = default;

ProcessIdentity Win32ProcessAdapter::Start(const ProcessSpec& spec) {
	if (impl_->process != nullptr) {
		throw std::logic_error("process adapter can start only one child");
	}
	if (!std::filesystem::is_regular_file(spec.executable) ||
		!std::filesystem::is_directory(spec.working_directory) || spec.evidence_limit == 0) {
		throw std::invalid_argument("process specification requires an executable, working directory, and evidence bound");
	}

	SECURITY_ATTRIBUTES security{};
	security.nLength = sizeof(security);
	security.bInheritHandle = TRUE;
	HANDLE stdout_write = nullptr;
	HANDLE stderr_write = nullptr;
	if (!CreatePipe(&impl_->stdout_read, &stdout_write, &security, 0) ||
		!CreatePipe(&impl_->stderr_read, &stderr_write, &security, 0) ||
		!SetHandleInformation(impl_->stdout_read, HANDLE_FLAG_INHERIT, 0) ||
		!SetHandleInformation(impl_->stderr_read, HANDLE_FLAG_INHERIT, 0)) {
		if (stdout_write != nullptr) CloseHandle(stdout_write);
		if (stderr_write != nullptr) CloseHandle(stderr_write);
		throw std::runtime_error("unable to create bounded child output pipes");
	}

	std::wstring command = QuoteArgument(std::filesystem::absolute(spec.executable).wstring());
	for (const auto& argument : spec.arguments) {
		command += L" " + QuoteArgument(argument);
	}
	std::vector<wchar_t> mutable_command(command.begin(), command.end());
	mutable_command.push_back(L'\0');
	STARTUPINFOW startup{};
	startup.cb = sizeof(startup);
	startup.dwFlags = STARTF_USESTDHANDLES;
	startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.hStdOutput = stdout_write;
	startup.hStdError = stderr_write;
	PROCESS_INFORMATION process{};
	const auto executable = std::filesystem::absolute(spec.executable).wstring();
	const BOOL created = CreateProcessW(
		executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
		CREATE_NEW_PROCESS_GROUP | CREATE_SUSPENDED, nullptr,
		std::filesystem::absolute(spec.working_directory).c_str(), &startup, &process);
	CloseHandle(stdout_write);
	CloseHandle(stderr_write);
	if (!created) {
		throw std::runtime_error("CreateProcessW failed with error " + std::to_string(GetLastError()));
	}

	impl_->process = process.hProcess;
	impl_->process_thread = process.hThread;
	impl_->job = CreateJobObjectW(nullptr, nullptr);
	JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
	limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
	if (impl_->job == nullptr ||
		!SetInformationJobObject(impl_->job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
		!AssignProcessToJobObject(impl_->job, impl_->process)) {
		TerminateProcess(impl_->process, 96);
		WaitForSingleObject(impl_->process, 1000);
		throw std::runtime_error("unable to place owned child in its Win32 job");
	}

	FILETIME creation{}, exit{}, kernel{}, user{};
	if (!GetProcessTimes(impl_->process, &creation, &exit, &kernel, &user)) {
		TerminateProcess(impl_->process, 96);
		WaitForSingleObject(impl_->process, 1000);
		throw std::runtime_error("unable to capture owned child creation identity");
	}
	impl_->identity = {process.dwProcessId, FileTimeValue(creation)};
	impl_->output_limit = spec.evidence_limit;
	impl_->stdout_reader = std::thread([impl = impl_.get()] { impl->ReadPipe(impl->stdout_read, impl->stdout_text); });
	impl_->stderr_reader = std::thread([impl = impl_.get()] { impl->ReadPipe(impl->stderr_read, impl->stderr_text); });
	if (ResumeThread(impl_->process_thread) == static_cast<DWORD>(-1)) {
		TerminateProcess(impl_->process, 96);
		WaitForSingleObject(impl_->process, 1000);
		throw std::runtime_error("unable to resume owned child process");
	}
	return impl_->identity;
}

bool Win32ProcessAdapter::IsRunning(ProcessIdentity expected) const {
	return impl_->Matches(expected) && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT;
}

bool Win32ProcessAdapter::WaitForExitUntil(RunDeadline deadline) const {
	return impl_->process != nullptr &&
		WaitForSingleObject(impl_->process, RemainingMilliseconds(deadline)) == WAIT_OBJECT_0;
}

bool Win32ProcessAdapter::SendGraceful(ProcessIdentity expected) {
	return IsRunning(expected) && GenerateConsoleCtrlEvent(CTRL_BREAK_EVENT, expected.pid) != FALSE;
}

TerminationResult Win32ProcessAdapter::Terminate(ProcessIdentity expected, RunDeadline deadline) {
	if (!impl_->Matches(expected)) {
		return TerminationResult::IdentityMismatch;
	}
	if (WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0) {
		return TerminationResult::AlreadyExited;
	}
	if (!TerminateProcess(impl_->process, 95)) {
		return TerminationResult::Failed;
	}
	return WaitForExitUntil(deadline) ? TerminationResult::Terminated : TerminationResult::TimedOut;
}

bool Win32ProcessAdapter::ClosePipes(RunDeadline deadline) {
	if (impl_->process != nullptr && WaitForSingleObject(impl_->process, 0) == WAIT_TIMEOUT) {
		return false;
	}
	return impl_->ClosePipeReaders(deadline);
}

AdapterEvidence Win32ProcessAdapter::CollectEvidence() const {
	AdapterEvidence evidence;
	evidence.identity = impl_->identity;
	if (impl_->process != nullptr && WaitForSingleObject(impl_->process, 0) == WAIT_OBJECT_0) {
		DWORD exit_code = 0;
		if (GetExitCodeProcess(impl_->process, &exit_code)) {
			evidence.exit_code = exit_code;
		}
	}
	{
		std::lock_guard<std::mutex> lock(impl_->output_mutex);
		evidence.stdout_text = impl_->stdout_text;
		evidence.stderr_text = impl_->stderr_text;
	}
	evidence.pipes_closed = impl_->pipes_closed;
	return evidence;
}

} // namespace integration::internal
