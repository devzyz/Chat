#include "ProcessHarness.h"

#include <chrono>
#include <iostream>
#include <stdexcept>

int main() {
    using namespace std::chrono_literals;
    try {
        auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 10s);
        const auto root = context->TempRoot();
        integration::ProcessSpec spec;
        spec.executable = "/bin/sh";
        spec.arguments = {L"-c", L"trap '' TERM; printf ready; while :; do :; done"};
        spec.working_directory = root;
        auto harness = integration::ProcessHarness::Start(*context, spec);
        if (!harness->WaitReady([&] { return harness->CollectEvidence().stdout_text == "ready"; },
                std::chrono::steady_clock::now() + 2s)) {
            throw std::runtime_error("harness did not observe child handshake");
        }
        auto stale = harness->Identity();
        ++stale.creation_time;
        if (context->CleanupProcess(stale)) throw std::runtime_error("context accepted stale identity");
        if (!harness->Stop(std::chrono::steady_clock::now() + 50ms).Complete()) {
            throw std::runtime_error("harness escalation failed");
        }
        const auto evidence = harness->CollectEvidence();
        if (!evidence.escalated || !evidence.pipe_readers_closed || evidence.exit_code != 137) {
            throw std::runtime_error("harness lost escalation/exit evidence");
        }
        harness.reset();
        const auto outcome = context->Teardown();
        if (!outcome.complete || std::filesystem::exists(root)) {
            throw std::runtime_error("run-owned resource teardown failed");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
