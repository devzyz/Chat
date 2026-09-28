#include <gtest/gtest.h>

#include "ProcessHarness.h"
#include "Win32ProcessAdapter.h"

#include <boost/asio.hpp>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

/** 定位与测试程序同目录的辅助子进程。 */
std::filesystem::path HelperExecutable() {
	std::vector<wchar_t> module_path(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
	if (length == 0 || length == module_path.size()) {
		throw std::runtime_error("unable to resolve process harness test executable");
	}
	module_path.resize(length);
	return std::filesystem::path(module_path.data()).parent_path() / "process_harness_child.exe";
}

/** 为当前运行建立辅助进程规格，限制输出证据并使用所属临时目录。 */
integration::ProcessSpec HelperSpec(
	const integration::RunContext& context,
	std::vector<std::wstring> arguments) {
	integration::ProcessSpec spec;
	spec.executable = HelperExecutable();
	spec.arguments = std::move(arguments);
	spec.working_directory = context.TempRoot();
	spec.evidence_limit = 4096;
	return spec;
}

/** 发送 PING 并核对 READY，连接或协议错误均返回未就绪。 */
bool ProbeReady(const integration::LoopbackPort& endpoint) {
	try {
		boost::asio::io_context io;
		boost::asio::ip::tcp::socket socket(io);
		socket.connect({boost::asio::ip::make_address(endpoint.address), endpoint.port});
		boost::asio::write(socket, boost::asio::buffer(std::string("PING\n")));
		boost::asio::streambuf response;
		boost::asio::read_until(socket, response, '\n');
		std::istream input(&response);
		std::string line;
		std::getline(input, line);
		return line == "READY";
	} catch (const std::exception&) {
		return false;
	}
}

// T09-PROC-06
/** 验证启动后立即退出仍保留有界输出、进程身份和真实退出码。 */
TEST(T09_PROC_ProcessHarness, StartupExitRetainsBoundedOutputIdentityAndExitCode) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	auto harness = integration::ProcessHarness::Start(
		*context,
		HelperSpec(*context, {L"--mode=exit", L"--exit-code=23", L"--emit-startup"}));

	EXPECT_FALSE(harness->WaitReady(/** 保持探针失败，让提前退出决定启动结果。 */ [] { return false; }, std::chrono::steady_clock::now() + 2s));
	const auto evidence = harness->CollectEvidence();

	EXPECT_NE(evidence.identity.pid, 0U);
	EXPECT_NE(evidence.identity.creation_time, 0U);
	ASSERT_TRUE(evidence.exit_code.has_value());
	EXPECT_EQ(*evidence.exit_code, 23U);
	EXPECT_NE(evidence.stdout_text.find("synthetic stdout"), std::string::npos);
	EXPECT_NE(evidence.stderr_text.find("synthetic stderr"), std::string::npos);
	EXPECT_LE(evidence.stdout_text.size(), 4096U);
	EXPECT_LE(evidence.stderr_text.size(), 4096U);
    auto racing = integration::ProcessHarness::Start(
        *context, HelperSpec(*context, {L"--mode=exit", L"--exit-code=23"}));
    bool first_probe = true;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    EXPECT_TRUE(racing->WaitReady(/** 把退出固定在首次探针返回旧结果之前，复现监督器的完成观察竞态。 */ [&] {
        if (first_probe) {
            first_probe = false;
            while (!racing->CollectEvidence().exit_code.has_value() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            return false;
        }
        return racing->CollectEvidence().exit_code.has_value();
    }, deadline));
    EXPECT_TRUE(racing->CollectEvidence().ready_probe_succeeded);
    EXPECT_TRUE(racing->Stop(deadline).Complete());
    EXPECT_FALSE(racing->CollectEvidence().escalated);
}

// T09-PROC-07
/** 验证进程存活不能代替成功的协议就绪探针。 */
TEST(T09_PROC_ProcessHarness, ReadinessRequiresSuccessfulProtocolProbe) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = context->ReserveLoopbackPort("ready");
	ASSERT_TRUE(context->ReleaseLoopbackPort("ready"));
	auto harness = integration::ProcessHarness::Start(
		*context,
		HelperSpec(*context, {L"--mode=server", L"--port=" + std::to_wstring(endpoint.port)}));

	EXPECT_FALSE(harness->WaitReady(/** 拒绝就绪以验证协议探针失败路径。 */ [] { return false; }, std::chrono::steady_clock::now() + 100ms));
	EXPECT_TRUE(harness->WaitReady(/** 通过真实 loopback 协议确认子进程就绪。 */ [&] { return ProbeReady(endpoint); }, std::chrono::steady_clock::now() + 2s));
	EXPECT_TRUE(harness->CollectEvidence().ready_probe_succeeded);
}

// T09-PROC-08
/** 验证正常停止使用温和信号，并在截止时间内关闭输出管道。 */
TEST(T09_PROC_ProcessHarness, StopUsesGracefulSignalAndClosesPipesByDeadline) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = context->ReserveLoopbackPort("graceful");
	ASSERT_TRUE(context->ReleaseLoopbackPort("graceful"));
	auto harness = integration::ProcessHarness::Start(
		*context,
		HelperSpec(*context, {L"--mode=server", L"--port=" + std::to_wstring(endpoint.port)}));
	ASSERT_TRUE(harness->WaitReady(/** 等待辅助进程通过协议就绪检查。 */ [&] { return ProbeReady(endpoint); }, std::chrono::steady_clock::now() + 2s));

	const auto stopped = harness->Stop(std::chrono::steady_clock::now() + 2s);
	const auto evidence = harness->CollectEvidence();

	EXPECT_TRUE(stopped.Complete()) << stopped.Detail();
	EXPECT_TRUE(evidence.graceful_stop_attempted);
	EXPECT_FALSE(evidence.escalated);
	EXPECT_TRUE(evidence.pipe_readers_closed);
	ASSERT_TRUE(evidence.exit_code.has_value());
	EXPECT_EQ(*evidence.exit_code, 0U);
}

// T09-PROC-09
/** 验证仅在温和停止期限耗尽后升级为强制终止。 */
TEST(T09_PROC_ProcessHarness, StopEscalatesOnlyAfterGracefulDeadline) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = context->ReserveLoopbackPort("escalate");
	ASSERT_TRUE(context->ReleaseLoopbackPort("escalate"));
	auto harness = integration::ProcessHarness::Start(
		*context,
		HelperSpec(*context, {
			L"--mode=server", L"--ignore-graceful", L"--port=" + std::to_wstring(endpoint.port)}));
	ASSERT_TRUE(harness->WaitReady(/** 确认忽略温和信号的辅助进程已就绪。 */ [&] { return ProbeReady(endpoint); }, std::chrono::steady_clock::now() + 2s));

	const auto stopped = harness->Stop(std::chrono::steady_clock::now() + 250ms);
	const auto evidence = harness->CollectEvidence();

	EXPECT_TRUE(stopped.Complete()) << stopped.Detail();
	EXPECT_TRUE(evidence.graceful_stop_attempted);
	EXPECT_TRUE(evidence.escalated);
	EXPECT_TRUE(evidence.pipe_readers_closed);
}

// T09-PROC-10
/** 验证 Win32 适配器拒绝创建时间不符的进程身份，避免误操作复用 PID。 */
TEST(T09_PROC_ProcessHarness, Win32AdapterRefusesMismatchedCreationIdentity) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 5s);
	const auto endpoint = context->ReserveLoopbackPort("identity");
	ASSERT_TRUE(context->ReleaseLoopbackPort("identity"));
	integration::internal::Win32ProcessAdapter adapter;
	const auto identity = adapter.Start(
		HelperSpec(*context, {L"--mode=server", L"--port=" + std::to_wstring(endpoint.port)}));
	const integration::ProcessIdentity stale{identity.pid, identity.creation_time + 1};

	EXPECT_EQ(
		adapter.Terminate(stale, std::chrono::steady_clock::now() + 500ms),
		integration::internal::TerminationResult::IdentityMismatch);
	EXPECT_TRUE(adapter.IsRunning(identity));
	EXPECT_EQ(
		adapter.Terminate(identity, std::chrono::steady_clock::now() + 2s),
		integration::internal::TerminationResult::Terminated);
	EXPECT_TRUE(adapter.ClosePipes(std::chrono::steady_clock::now() + 2s));
}

} // namespace
