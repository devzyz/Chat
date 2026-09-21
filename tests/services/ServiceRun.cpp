#include "ProcessHarness.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

int main(int argc, char** argv) {
    if (argc != 4) {
        std::cerr << "usage: service_run <node> <coordinator> <evidence-root>\n";
        return 64;
    }
    const auto evidence_root = std::filesystem::absolute(argv[3]);
    std::filesystem::create_directories(evidence_root);
    bool complete = false;
    try {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(680);
        auto context = integration::RunContext::Create(deadline);
        if (setenv("CHAT_SERVICE_RUN_ID", context->RunId().c_str(), 1) != 0 ||
            setenv("CHAT_SERVICE_EVIDENCE_ROOT", evidence_root.string().c_str(), 1) != 0) {
            throw std::runtime_error("service environment injection failed");
        }
        integration::ProcessSpec spec;
        spec.executable = std::filesystem::absolute(argv[1]);
        spec.arguments = {std::filesystem::absolute(argv[2]).wstring()};
        spec.working_directory = context->TempRoot();
        auto child = integration::ProcessHarness::Start(*context, std::move(spec));
        // Here the probe observes completion, not protocol readiness. Dependency
        // readiness is checked by the coordinator before writing any run data.
        child->WaitReady([&] {
            return child->CollectEvidence().exit_code.has_value();
        }, deadline - std::chrono::seconds(20));
        const auto evidence = child->CollectEvidence();
        if (!evidence.exit_code || *evidence.exit_code != 0) {
            context->RecordPrimaryFailure("service coordinator failed or timed out");
        }
        const auto result = context->Teardown();
        complete = result.complete;
    } catch (const std::exception&) {
        // Child logs are deliberately not echoed: evidence owns safe diagnostics.
        std::cerr << "service RunContext failed\n";
    }
    std::ofstream report(evidence_root / "process-teardown.json");
    report << "{\"complete\":" << (complete ? "true" : "false") << "}\n";
    report.close();
    return complete && report ? EXIT_SUCCESS : EXIT_FAILURE;
}
