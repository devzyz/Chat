#include "PosixProcessAdapter.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using namespace std::chrono_literals;

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

integration::ProcessSpec Spec(std::wstring command) {
    integration::ProcessSpec spec;
    spec.executable = "/bin/sh";
    spec.arguments = {L"-c", std::move(command)};
    spec.working_directory = std::filesystem::temp_directory_path();
    spec.evidence_limit = 128;
    return spec;
}

void Reaped(std::uint32_t pid) {
    int status = 0;
    errno = 0;
    Require(waitpid(static_cast<pid_t>(pid), &status, WNOHANG) == -1 && errno == ECHILD,
        "owned child was not reaped");
}
}

int main(int argc, char** argv) {
    try {
        Require(argc == 2, "one case name is required");
        const std::string name = argv[1];
        integration::internal::PosixProcessAdapter adapter;
        const auto deadline = std::chrono::steady_clock::now() + 5s;
        if (name == "exit-output") {
            const auto id = adapter.Start(Spec(L"printf hello; printf failure >&2; exit 23"));
            Require(adapter.WaitForExitUntil(deadline), "exit timed out");
            Require(adapter.ClosePipes(deadline), "pipes not closed");
            const auto evidence = adapter.CollectEvidence();
            Require(evidence.exit_code == 23 && evidence.stdout_text == "hello" &&
                evidence.stderr_text == "failure", "exit/output not retained");
            Reaped(id.pid);
        } else if (name == "graceful" || name == "stale-identity" || name == "timeout") {
            const auto id = adapter.Start(Spec(name == "timeout"
                ? L"trap '' TERM; printf ready; while :; do :; done"
                : L"trap 'exit 0' TERM; printf ready; while :; do :; done"));
            while (adapter.CollectEvidence().stdout_text != "ready") {
                Require(std::chrono::steady_clock::now() < deadline, "child did not initialize signal handler");
                Require(!adapter.WaitForExitUntil(std::chrono::steady_clock::now() + 10ms), "child exited early");
            }
            if (name == "stale-identity") {
                auto stale = id;
                ++stale.creation_time;
                Require(!adapter.SendGraceful(stale), "stale graceful identity accepted");
                Require(adapter.Terminate(stale, deadline) == integration::internal::TerminationResult::IdentityMismatch,
                    "stale termination identity accepted");
                Require(adapter.IsRunning(id), "stale identity killed owned child");
            }
            Require(adapter.SendGraceful(id), "SIGTERM failed");
            if (name == "timeout") {
                Require(!adapter.WaitForExitUntil(std::chrono::steady_clock::now() + 50ms), "ignored TERM exited");
                Require(adapter.Terminate(id, deadline) == integration::internal::TerminationResult::Terminated,
                    "SIGKILL escalation failed");
            } else {
                Require(adapter.WaitForExitUntil(deadline), "graceful exit timed out");
            }
            Require(adapter.ClosePipes(deadline), "shutdown not reaped/drained");
            Require(adapter.ClosePipes(deadline), "shutdown not idempotent");
            Reaped(id.pid);
        } else if (name == "bounded-output") {
            const auto id = adapter.Start(Spec(L"i=0; while [ $i -lt 10000 ]; do printf abcdefghijklmnop; printf stderr >&2; i=$((i+1)); done"));
            Require(adapter.WaitForExitUntil(deadline), "full pipe deadlocked child");
            Require(adapter.ClosePipes(deadline), "output cleanup failed");
            const auto evidence = adapter.CollectEvidence();
            Require(evidence.stdout_text.size() == 128 && evidence.stderr_text.size() == 128,
                "evidence bounds not enforced");
            Reaped(id.pid);
        } else if (name == "descendant") {
            const auto id = adapter.Start(Spec(L"sleep 30 & printf spawned; exit 0"));
            Require(adapter.WaitForExitUntil(deadline), "leader did not exit");
            Require(adapter.ClosePipes(deadline), "descendant kept output pipe open");
            Reaped(id.pid);
        } else if (name == "invalid-exec") {
            auto spec = Spec(L"exit 0");
            spec.executable = "/definitely-not-a-chat-executable";
            bool rejected = false;
            try { adapter.Start(spec); } catch (const std::exception&) { rejected = true; }
            Require(rejected, "invalid executable accepted");
        } else {
            throw std::runtime_error("unknown case");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
