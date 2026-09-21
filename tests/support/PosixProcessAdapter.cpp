#include "PosixProcessAdapter.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <codecvt>
#include <fstream>
#include <locale>
#include <mutex>
#include <sstream>
#include <stdexcept>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace integration::internal {
namespace {

class FileDescriptor {
public:
    ~FileDescriptor() { Reset(); }
    FileDescriptor() = default;
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int Get() const { return _value; }
    void Reset(int value = -1) {
        if (_value >= 0) close(_value);
        _value = value;
    }
private:
    int _value = -1;
};

void MakePipe(FileDescriptor& reader, FileDescriptor& writer) {
    int descriptors[2] = {-1, -1};
    if (pipe2(descriptors, O_CLOEXEC) != 0) throw std::runtime_error("unable to create child pipe");
    reader.Reset(descriptors[0]);
    writer.Reset(descriptors[1]);
    if (fcntl(reader.Get(), F_SETFL, O_NONBLOCK) < 0) {
        throw std::runtime_error("unable to make child pipe nonblocking");
    }
}

std::uint64_t StartTicks(pid_t pid) {
    std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(input, line)) return 0;
    const auto closing = line.rfind(')');
    if (closing == std::string::npos) return 0;
    std::istringstream fields(line.substr(closing + 2));
    std::string field;
    for (int number = 3; number <= 21; ++number) {
        if (!(fields >> field)) return 0;
    }
    std::uint64_t ticks = 0;
    fields >> ticks;
    return ticks;
}

} // namespace

class PosixProcessAdapter::Impl {
public:
    ~Impl() {
        // No PID adoption: even exceptional startup/cleanup owns only this child.
        if (pid > 0 && !reaped) {
            if (Matches(identity)) kill(-pid, SIGKILL);
            else kill(pid, SIGKILL); // unreaped direct child cannot be recycled
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
            while (!ObserveExit() && std::chrono::steady_clock::now() < deadline) Pump(deadline);
            if (exited) {
                int status = 0;
                while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
            }
        }
    }

    bool Matches(ProcessIdentity expected) const {
        return !reaped && pid > 0 && expected.pid == static_cast<std::uint32_t>(pid) &&
            expected.creation_time != 0 && expected.creation_time == identity.creation_time &&
            StartTicks(pid) == expected.creation_time && getpgid(pid) == pid;
    }

    bool ObserveExit() {
        if (exited) return true;
        if (pid <= 0) return false;
        siginfo_t info{};
        int result = 0;
        do { result = waitid(P_PID, static_cast<id_t>(pid), &info, WEXITED | WNOHANG | WNOWAIT); }
        while (result < 0 && errno == EINTR);
        if (result == 0 && info.si_pid == pid) {
            exited = true;
            exit_code = static_cast<std::uint32_t>(info.si_code == CLD_EXITED ? info.si_status : 128 + info.si_status);
        }
        return exited;
    }

    void Drain(FileDescriptor& descriptor, std::string& output) {
        std::array<char, 4096> buffer{};
        // Bound each iteration even if a child writes continuously.
        for (int attempt = 0; descriptor.Get() >= 0 && attempt < 16; ++attempt) {
            const auto count = read(descriptor.Get(), buffer.data(), buffer.size());
            if (count > 0) {
                output.append(buffer.data(), std::min(limit - output.size(), static_cast<std::size_t>(count)));
            } else if (count == 0) {
                descriptor.Reset();
            } else if (errno != EINTR) {
                if (errno != EAGAIN && errno != EWOULDBLOCK) read_failed = true;
                break;
            }
        }
    }

    void Pump(RunDeadline deadline) {
        Drain(stdout_read, stdout_text);
        Drain(stderr_read, stderr_text);
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        pollfd descriptors[2] = {{stdout_read.Get(), POLLIN, 0}, {stderr_read.Get(), POLLIN, 0}};
        poll(descriptors, 2, static_cast<int>(std::clamp<std::int64_t>(remaining, 0, 10)));
    }

    pid_t pid = -1;
    ProcessIdentity identity;
    FileDescriptor stdout_read;
    FileDescriptor stderr_read;
    std::size_t limit = 0;
    std::string stdout_text;
    std::string stderr_text;
    std::optional<std::uint32_t> exit_code;
    bool exited = false;
    bool reaped = false;
    bool read_failed = false;
    mutable std::mutex mutex;
};

PosixProcessAdapter::PosixProcessAdapter() : _impl(std::make_unique<Impl>()) {}
PosixProcessAdapter::~PosixProcessAdapter() = default;

ProcessIdentity PosixProcessAdapter::Start(const ProcessSpec& spec) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (_impl->pid > 0) throw std::logic_error("process adapter can start only one child");
    if (!std::filesystem::is_regular_file(spec.executable) ||
        !std::filesystem::is_directory(spec.working_directory) || spec.evidence_limit == 0) {
        throw std::invalid_argument("child executable, directory and evidence bound are required");
    }
    const auto executable = std::filesystem::absolute(spec.executable).string();
    const auto directory = std::filesystem::absolute(spec.working_directory).string();
    std::vector<std::string> arguments{executable};
    std::wstring_convert<std::codecvt_utf8<wchar_t>> converter;
    for (const auto& argument : spec.arguments) {
        if (argument.find(L'\0') != std::wstring::npos) throw std::invalid_argument("NUL in child argument");
        arguments.push_back(converter.to_bytes(argument));
    }
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);
    FileDescriptor stdout_write;
    FileDescriptor stderr_write;
    MakePipe(_impl->stdout_read, stdout_write);
    MakePipe(_impl->stderr_read, stderr_write);
    _impl->limit = spec.evidence_limit;
    const pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("unable to fork owned child");
    if (pid == 0) {
        // Only async-signal-safe calls between fork and exec; arguments are prepared above.
        if (setpgid(0, 0) != 0 || chdir(directory.c_str()) != 0 ||
            dup2(stdout_write.Get(), STDOUT_FILENO) < 0 || dup2(stderr_write.Get(), STDERR_FILENO) < 0) _exit(126);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    _impl->pid = pid;
    stdout_write.Reset();
    stderr_write.Reset();
    // Both sides establish the group; EACCES means the child already exec'd.
    if (setpgid(pid, pid) != 0 && errno != EACCES) throw std::runtime_error("unable to own child process group");
    _impl->identity = {static_cast<std::uint32_t>(pid), StartTicks(pid)};
    if (!_impl->Matches(_impl->identity)) throw std::runtime_error("unable to capture child creation identity");
    return _impl->identity;
}

bool PosixProcessAdapter::IsRunning(ProcessIdentity expected) const {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->Matches(expected) && !_impl->ObserveExit();
}

bool PosixProcessAdapter::WaitForExitUntil(RunDeadline deadline) const {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    do {
        _impl->Drain(_impl->stdout_read, _impl->stdout_text);
        _impl->Drain(_impl->stderr_read, _impl->stderr_text);
        if (_impl->ObserveExit()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        _impl->Pump(deadline);
    } while (true);
}

bool PosixProcessAdapter::SendGraceful(ProcessIdentity expected) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    return _impl->Matches(expected) && !_impl->ObserveExit() && kill(-_impl->pid, SIGTERM) == 0;
}

TerminationResult PosixProcessAdapter::Terminate(ProcessIdentity expected, RunDeadline deadline) {
    {
        std::lock_guard<std::mutex> lock(_impl->mutex);
        if (!_impl->Matches(expected)) return TerminationResult::IdentityMismatch;
        if (_impl->ObserveExit()) return TerminationResult::AlreadyExited;
        if (kill(-_impl->pid, SIGKILL) != 0) return TerminationResult::Failed;
    }
    return WaitForExitUntil(deadline) ? TerminationResult::Terminated : TerminationResult::TimedOut;
}

bool PosixProcessAdapter::ClosePipes(RunDeadline deadline) {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    if (!_impl->ObserveExit()) return false;
    if (!_impl->reaped) {
        // Keep the leader waitable until all group signalling is done: a stale
        // process group ID must never reach an unrelated child after PID reuse.
        if (!_impl->Matches(_impl->identity)) return false;
        if (kill(-_impl->pid, SIGKILL) != 0 && errno != ESRCH) return false;
        int status = 0;
        pid_t result = 0;
        do { result = waitpid(_impl->pid, &status, WNOHANG); } while (result < 0 && errno == EINTR);
        if (result != _impl->pid) return false;
        _impl->reaped = true;
    }
    while (_impl->stdout_read.Get() >= 0 || _impl->stderr_read.Get() >= 0) {
        _impl->Drain(_impl->stdout_read, _impl->stdout_text);
        _impl->Drain(_impl->stderr_read, _impl->stderr_text);
        if (_impl->stdout_read.Get() < 0 && _impl->stderr_read.Get() < 0) break;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        _impl->Pump(deadline);
    }
    return !_impl->read_failed;
}

AdapterEvidence PosixProcessAdapter::CollectEvidence() const {
    std::lock_guard<std::mutex> lock(_impl->mutex);
    _impl->Drain(_impl->stdout_read, _impl->stdout_text);
    _impl->Drain(_impl->stderr_read, _impl->stderr_text);
    _impl->ObserveExit();
    return {_impl->identity, _impl->exit_code, _impl->stdout_text, _impl->stderr_text,
        _impl->reaped && _impl->stdout_read.Get() < 0 && _impl->stderr_read.Get() < 0};
}

} // namespace integration::internal
