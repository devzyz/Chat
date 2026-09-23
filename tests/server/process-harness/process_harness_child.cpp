#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>

#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

std::atomic<bool> stop_requested{false};
std::atomic<bool> ignore_graceful{false};
HANDLE stop_event = nullptr;

/** 响应控制台关闭信号并唤醒测试服务器；忽略模式用于验证强制终止。 */
BOOL WINAPI HandleConsoleSignal(DWORD signal) {
	if (signal != CTRL_BREAK_EVENT && signal != CTRL_CLOSE_EVENT) {
		return FALSE;
	}
	if (!ignore_graceful.load()) {
		stop_requested.store(true);
		if (stop_event != nullptr) {
			SetEvent(stop_event);
		}
	}
	return TRUE;
}

/** 提取指定前缀的 ASCII 参数值，拒绝非 ASCII 内容。 */
std::string ArgumentValue(int argc, wchar_t** argv, const std::wstring& prefix) {
	for (int index = 1; index < argc; ++index) {
		const std::wstring argument(argv[index]);
		if (argument.rfind(prefix, 0) == 0) {
			const auto value = argument.substr(prefix.size());
			std::string result;
			result.reserve(value.size());
			for (const wchar_t character : value) {
				if (character < 0 || character > 0x7f) {
					throw std::invalid_argument("synthetic helper arguments must be ASCII");
				}
				result.push_back(static_cast<char>(character));
			}
			return result;
		}
	}
	return {};
}

/** 判断启动参数中是否包含指定完整开关。 */
bool HasArgument(int argc, wchar_t** argv, const std::wstring& expected) {
	for (int index = 1; index < argc; ++index) {
		if (std::wstring(argv[index]) == expected) {
			return true;
		}
	}
	return false;
}

/** 启动 loopback 测试端点，通过 PING 探针报告就绪并响应停止事件。 */
int RunServer(std::uint16_t port, bool late_output, bool emit_secret) {
	WSADATA data{};
	if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
		return 30;
	}
	SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listener == INVALID_SOCKET) {
		WSACleanup();
		return 30;
	}
	const BOOL exclusive = TRUE;
	setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
	sockaddr_in address{};
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address.sin_port = htons(port);
	if (bind(listener, reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR ||
		listen(listener, SOMAXCONN) == SOCKET_ERROR) {
		std::cerr << "synthetic port conflict" << std::endl;
		closesocket(listener);
		WSACleanup();
		return 31;
	}

	stop_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
	WSAEVENT socket_event = WSACreateEvent();
	WSAEventSelect(listener, socket_event, FD_ACCEPT | FD_CLOSE);
	HANDLE events[] = {stop_event, socket_event};
	std::cout << "synthetic server started" << std::endl;
	for (;;) {
		const auto selected = WaitForMultipleObjects(2, events, FALSE, INFINITE);
		if (selected == WAIT_OBJECT_0) {
			break;
		}
		if (selected != WAIT_OBJECT_0 + 1) {
			break;
		}
		WSANETWORKEVENTS network{};
		WSAEnumNetworkEvents(listener, socket_event, &network);
		if ((network.lNetworkEvents & FD_ACCEPT) != 0) {
			SOCKET client = accept(listener, nullptr, nullptr);
			if (client != INVALID_SOCKET) {
				char request[16]{};
				const auto received = recv(client, request, sizeof(request), 0);
				if (received > 0 && std::string(request, request + received) == "PING\n") {
					send(client, "READY\n", 6, 0);
				}
				shutdown(client, SD_BOTH);
				closesocket(client);
			}
		}
	}
	if (late_output) {
		std::cout << "synthetic late stdout" << std::endl;
		std::cerr << "synthetic late stderr" << std::endl;
	}
	if (emit_secret) {
		std::cout << "password=primary-secret" << std::endl;
		std::cerr << "token=cleanup-secret" << std::endl;
	}
	WSACloseEvent(socket_event);
	CloseHandle(stop_event);
	stop_event = nullptr;
	closesocket(listener);
	WSACleanup();
	return 0;
}

} // namespace

/** 解析故障模式和端口，注册控制台信号处理并运行指定测试子进程。 */
int wmain(int argc, wchar_t** argv) {
	SetConsoleCtrlHandler(HandleConsoleSignal, TRUE);
	ignore_graceful.store(HasArgument(argc, argv, L"--ignore-graceful"));
	const auto mode = ArgumentValue(argc, argv, L"--mode=");
	if (mode == "exit") {
		if (HasArgument(argc, argv, L"--emit-startup")) {
			std::cout << "synthetic stdout" << std::endl;
			std::cerr << "synthetic stderr" << std::endl;
		}
		const auto code = ArgumentValue(argc, argv, L"--exit-code=");
		return code.empty() ? 0 : std::stoi(code);
	}
	if (mode != "server") {
		std::cerr << "synthetic invalid mode" << std::endl;
		return 32;
	}
	const auto port = ArgumentValue(argc, argv, L"--port=");
	if (port.empty()) {
		return 32;
	}
	return RunServer(
		static_cast<std::uint16_t>(std::stoul(port)),
		HasArgument(argc, argv, L"--late-output"),
		HasArgument(argc, argv, L"--emit-secret"));
}
