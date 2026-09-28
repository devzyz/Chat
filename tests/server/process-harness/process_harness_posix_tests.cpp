#include "ProcessHarness.h"

#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

/** 验证 POSIX 子进程就绪、忽略温和终止后的升级关闭，以及临时根目录清理。 */
int main() {
    using namespace std::chrono_literals;
    try {
        {
            auto race_context = integration::RunContext::Create(std::chrono::steady_clock::now() + 3s);
            integration::ProcessSpec exit_spec;
            exit_spec.executable = "/bin/sh";
            exit_spec.arguments = {L"-c", L"exit 23"};
            exit_spec.working_directory = race_context->TempRoot();
            auto exited = integration::ProcessHarness::Start(*race_context, exit_spec);
            const auto deadline = std::chrono::steady_clock::now() + 2s;
            bool first_probe = true;
            if (!exited->WaitReady(/** 固定退出发生在首次探针旧结果与存活检查之间，保护最终完成观察。 */ [&] {
                if (first_probe) {
                    first_probe = false;
                    while (!exited->CollectEvidence().exit_code.has_value() && std::chrono::steady_clock::now() < deadline) {
                        std::this_thread::yield();
                    }
                    return false;
                }
                return exited->CollectEvidence().exit_code.has_value();
            }, deadline) || exited->CollectEvidence().exit_code != 23 ||
                !exited->Stop(deadline).Complete() || !race_context->Teardown().complete) {
                throw std::runtime_error("completion probe lost final exit evidence");
            }
        }
        auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 10s);
        const auto root = context->TempRoot();
        integration::ProcessSpec spec;
        spec.executable = "/bin/sh";
        spec.arguments = {L"-c", L"trap '' TERM; printf ready; while :; do :; done"};
        spec.working_directory = root;
        auto harness = integration::ProcessHarness::Start(*context, spec);
        if (!harness->WaitReady(/** 以子进程输出 ready 作为本场景的就绪条件。 */ [&] { return harness->CollectEvidence().stdout_text == "ready"; },
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
