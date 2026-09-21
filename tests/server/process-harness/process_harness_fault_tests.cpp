#include <gtest/gtest.h>

#include "ProcessHarness.h"

#include <boost/asio.hpp>

#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

namespace {

using namespace std::chrono_literals;

std::filesystem::path FaultHelperExecutable() {
	std::vector<wchar_t> module_path(32768, L'\0');
	const DWORD length = GetModuleFileNameW(nullptr, module_path.data(), static_cast<DWORD>(module_path.size()));
	if (length == 0 || length == module_path.size()) {
		throw std::runtime_error("unable to resolve process harness fault test executable");
	}
	module_path.resize(length);
	return std::filesystem::path(module_path.data()).parent_path() / "process_harness_child.exe";
}

integration::ProcessSpec FaultHelperSpec(
	const integration::RunContext& context,
	std::vector<std::wstring> arguments) {
	integration::ProcessSpec spec;
	spec.executable = FaultHelperExecutable();
	spec.arguments = std::move(arguments);
	spec.working_directory = context.TempRoot();
	spec.evidence_limit = 4096;
	return spec;
}

bool FaultProbeReady(const integration::LoopbackPort& endpoint) {
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

bool ProbeRefusedWithoutBlocking(const integration::LoopbackPort& endpoint) {
	SOCKET probe = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (probe == INVALID_SOCKET) return false;
	u_long nonblocking = 1;
	ioctlsocket(probe, FIONBIO, &nonblocking);
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(endpoint.port);
	connect(probe, reinterpret_cast<const sockaddr*>(&address), sizeof(address));
	closesocket(probe);
	return false;
}

bool CanBind(std::uint16_t port) {
	boost::asio::io_context io;
	boost::asio::ip::tcp::acceptor acceptor(io);
	boost::system::error_code error;
	acceptor.open(boost::asio::ip::tcp::v4(), error);
	if (error) return false;
	const BOOL exclusive = TRUE;
	setsockopt(
		acceptor.native_handle(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
		reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
	acceptor.bind({boost::asio::ip::address_v4::loopback(), port}, error);
	return !error;
}

// T09-PROC-11
TEST(T09_PROC_Faults, RefusalDeadlineAndPortConflictAreBoundedAndReleaseOwnership) {
	auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 6s);
	const auto child_endpoint = context->ReserveLoopbackPort("fault-child");
	ASSERT_TRUE(context->ReleaseLoopbackPort("fault-child"));
	const auto refused_endpoint = context->ReserveLoopbackPort("fault-refused");
	ASSERT_TRUE(context->ReleaseLoopbackPort("fault-refused"));
	auto running = integration::ProcessHarness::Start(
		*context,
		FaultHelperSpec(*context, {L"--mode=server", L"--port=" + std::to_wstring(child_endpoint.port)}));
	ASSERT_TRUE(running->WaitReady(
		[&] { return FaultProbeReady(child_endpoint); }, std::chrono::steady_clock::now() + 2s));

	const auto refusal_deadline = std::chrono::steady_clock::now() + 150ms;
	EXPECT_FALSE(running->WaitReady(
		[&] { return ProbeRefusedWithoutBlocking(refused_endpoint); }, refusal_deadline));
	EXPECT_LE(std::chrono::steady_clock::now(), refusal_deadline + 100ms);
	EXPECT_FALSE(running->WaitReady(
		[&] { return FaultProbeReady(child_endpoint); }, std::chrono::steady_clock::now() - 1ms));
	EXPECT_TRUE(running->Stop(std::chrono::steady_clock::now() + 2s).Complete());

	const auto conflict_endpoint = context->ReserveLoopbackPort("fault-conflict");
	auto conflicting = integration::ProcessHarness::Start(
		*context,
		FaultHelperSpec(*context, {L"--mode=server", L"--port=" + std::to_wstring(conflict_endpoint.port)}));
	EXPECT_FALSE(conflicting->WaitReady([] { return false; }, std::chrono::steady_clock::now() + 2s));
	const auto evidence = conflicting->CollectEvidence();
	ASSERT_TRUE(evidence.exit_code.has_value());
	EXPECT_EQ(*evidence.exit_code, 31U);
	EXPECT_NE(evidence.stderr_text.find("synthetic port conflict"), std::string::npos);
	EXPECT_TRUE(conflict_endpoint.port != 0);
	ASSERT_TRUE(context->ReleaseLoopbackPort("fault-conflict"));
	EXPECT_TRUE(CanBind(conflict_endpoint.port));
}

// T09-PROC-12
TEST(T09_PROC_Faults, LateOutputAndCleanupFailureRemainSeparateSanitizedAndResidueFree) {
	std::filesystem::path temp_root;
	std::uint16_t port = 0;
	{
		auto context = integration::RunContext::Create(std::chrono::steady_clock::now() + 6s);
		temp_root = context->TempRoot();
		const auto endpoint = context->ReserveLoopbackPort("late-output");
		port = endpoint.port;
		ASSERT_TRUE(context->ReleaseLoopbackPort("late-output"));
		auto harness = integration::ProcessHarness::Start(
			*context,
			FaultHelperSpec(*context, {
				L"--mode=server", L"--late-output", L"--emit-secret",
				L"--port=" + std::to_wstring(endpoint.port)}));
		ASSERT_TRUE(harness->WaitReady(
			[&] { return FaultProbeReady(endpoint); }, std::chrono::steady_clock::now() + 2s));
		ASSERT_TRUE(harness->Stop(std::chrono::steady_clock::now() + 2s).Complete());
		const auto evidence = harness->CollectEvidence();
		EXPECT_NE(evidence.stdout_text.find("synthetic late stdout"), std::string::npos);
		EXPECT_TRUE(evidence.pipe_readers_closed);
		EXPECT_EQ(evidence.stdout_text.find("primary-secret"), std::string::npos);
		EXPECT_EQ(evidence.stderr_text.find("cleanup-secret"), std::string::npos);
		EXPECT_NE(evidence.stdout_text.find("password=[REDACTED]"), std::string::npos);
		EXPECT_NE(evidence.stderr_text.find("token=[REDACTED]"), std::string::npos);

		const auto first = context->ReserveProcessSlot("fault-cleanup-first");
		const auto second = context->ReserveProcessSlot("fault-cleanup-second");
		context->CommitProcess(first, {7001, 701}, [] {
			return integration::CleanupStatus::Success("clean");
		});
		context->CommitProcess(second, {7002, 702}, [] {
			return integration::CleanupStatus::Failure("token=cleanup-secret");
		});
		context->RecordPrimaryFailure("password=primary-secret");
		const auto outcome = context->Teardown();
		EXPECT_FALSE(outcome.complete);
		EXPECT_NE(static_cast<int>(!outcome.complete), 0);
		ASSERT_TRUE(outcome.primary_failure.has_value());
		EXPECT_EQ(outcome.primary_failure->find("primary-secret"), std::string::npos);
		EXPECT_NE(outcome.primary_failure->find("password=[REDACTED]"), std::string::npos);
		ASSERT_EQ(outcome.cleanup_failures.size(), 1U);
		EXPECT_EQ(outcome.cleanup_failures.front().find("cleanup-secret"), std::string::npos);
		EXPECT_NE(outcome.cleanup_failures.front().find("token=[REDACTED]"), std::string::npos);
	}

	EXPECT_FALSE(std::filesystem::exists(temp_root));
	EXPECT_TRUE(CanBind(port));
}

} // namespace
