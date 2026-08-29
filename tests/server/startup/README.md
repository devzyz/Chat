# 启动与配置路径测试

## 被测代码与当前契约

本 Module 通过真实发布 EXE 保护三套生产入口：

- Chat：`ChatServer.cpp`、`ConfigMgr.cpp`、`CServer.cpp`，S01-CLI-01..02、S01-CFG-01..03、S01-GRPC-CFG-01、S01-BIND-01..02。
- Gate：`GateServer.cpp`、`ConfigMgr.cpp`、`CServer.cpp`，S02-CLI-01..02、S02-CFG-01..06、S02-GRPC-CFG-01、S02-BIND-01、S02-LIFE-01。
- Status：`StatusServer.cpp`、`ConfigMgr.cpp`，S03-CLI-01..02、S03-CFG-01..06、S03-BIND-01、S03-LIFE-01。

Gate 与 Status 的配置来源均为 CLI `--config`、`CHAT_CONFIG`、cwd `config.ini`。缺少 CLI 值、未知参数、缺文件、非规范或越界端口、缺少关键 endpoint 都必须在监听、线程或外部 Adapter 访问前非零退出。Gate HTTP acceptor 或 Status gRPC `BuildAndStart()` 失败不得报告协议 ready。

## 用例类型、隔离与清理

Domain 为 Architecture，Level 为 Integration。`startup_config_tests.cpp` 包含 8 个 Chat case；`gate_status_startup_tests.cpp` 包含 21 个 Gate/Status case。新增边界用例通过真实 EXE 证明显式 gRPC 超时配置在监听前按 100..60000 ms 拒绝越界值。真实 socket、gRPC channel 和子进程不会因为只用 loopback 而降级为 Unit/Component。

每例创建唯一临时目录并分别捕获 stdout/stderr。Gate ready 使用真实 `GET /get_test` HTTP 200/body 探针；Status ready 使用真实 gRPC channel handshake，不调用会访问 Redis 的业务 RPC。配置中的 Redis、MySQL、Varify 与其他 service endpoint 都是不可用 loopback 占位，fail-fast 和 ready/stop 路径不连接它们，也不读取仓库开发配置。

进程 startup 硬上限为 15 秒，stop 硬上限为 5 秒。Windows graceful-stop 使用 `CREATE_NEW_PROCESS_GROUP` 创建本测试独占的 console process group，再只向该 PID/group 发送 `CTRL_BREAK_EVENT`；生产 `boost::asio::signal_set` 在 Windows 注册 `SIGBREAK`，与 `SIGINT`/`SIGTERM` 进入同一停止路径。该机制要求测试进程与子进程共享 console，验证的是当前 Windows console 等价终止路径；它不声称覆盖 Windows Service Control Manager，Linux 的 SIGINT/SIGTERM 行为仍需目标平台验证。

兜底终止前同时核验测试持有的 process handle、PID 和 executable image path。析构只清理本例已核验进程和本例唯一 tempdir；不枚举或终止其他同名进程。端口由 RAII acceptor 管理，并在失败与正常停止后验证可立即重新 bind。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_integration_tests.exe --gtest_filter=StartupConfigTests.*:ProductionExecutables/*
```

统一 runner 构建并 app-local deploy Gate、Status、Chat 和 Integration test EXE，要求 `server_integration.xml` 精确 34 case（29 startup + 1 C++→Node protocol loopback + 4 Gate production gRPC client loopback）。CI 复用 `servers-release` job 的同一依赖恢复，并由现有 report artifact 路径保留失败诊断。

## 已知缺口

- 本 Module 不调用真实 Redis/MySQL/SMTP，也不证明其 Adapter 的连接、恢复或关闭；这些仍属于 disposable Integration 环境。
- Gate/Status 的测试证明本地 listener ready 与 graceful stop，不证明跨四个发布单元的业务 E2E 或部署编排。
- Windows `CTRL_BREAK_EVENT` 等价路径已覆盖；Windows Service 控制事件和 Linux signal 行为不在当前 Windows runner 的声明范围。
