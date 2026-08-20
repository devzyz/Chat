# 启动与配置路径测试

## 被测代码与当前契约

- 生产入口：`ChatServer/ChatServer/ChatServer.cpp`、`ConfigMgr.cpp`、`CServer.cpp`。
- `S01-CLI-01..02`：`--config` 缺少路径及未知参数必须显示用法并非零退出。
- `S01-CFG-01..03`：配置来源优先级为显式 `--config`、`CHAT_CONFIG`、工作目录 `config.ini`。
- `S01-BIND-01..02`：TCP 或 gRPC 本地端口绑定失败必须在外部依赖登记前非零退出；gRPC 绑定失败后，
  本次已绑定的 TCP 端口必须随进程退出释放。

ChatServer 配置字段、端口范围和 peer 校验已由 `tests/server/config` 覆盖，本模块不重复这些断言。

## 用例类型、隔离与清理

`startup_config_tests.cpp` 包含 7 个 Windows Component 测试。每个用例创建唯一临时工作目录，通过
`CreateProcessW` 黑盒启动当前配置对应的 `ChatServer.exe`，并分别捕获 stdout/stderr。配置优先级用三份
故意畸形且名称可区分的临时 INI 观察实际选择路径；TCP 冲突用例占用 wildcard 临时端口，gRPC 冲突用例占用
loopback 临时端口。配置中的 Redis、MySQL 和 Status endpoint 均为不可用的本地占位，且执行路径在访问这些依赖前失败。测试不访问真实 Redis、
MySQL、SMTP 或公网，不使用仓库开发配置。

每个子进程有 10 秒硬超时；超时进程会被终止。fixture 析构时只清理本次唯一临时目录，端口由 RAII acceptor
释放。失败信息保留退出码、stderr 和相关端口；JUnit 合并报告保存在稳定路径。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=StartupConfigTests.*
```

The unified runner builds the production and test executables and then invokes the pinned
vcpkg `z-applocal` deployment for both before launching black-box startup cases. This keeps
a fresh or relinked binary from failing only because its runtime DLLs are absent.

统一入口会先构建 `ChatServer` 与 `ServerUnitTests`。CI 复用 `servers-release` job 已恢复的 vcpkg 环境，报告为
`build/test-results/server_unit.xml`，并在失败时由现有 `if: always()` artifact step 上传。

## 已知缺口

- GateServer、StatusServer 当前只在参数恰好为 `--config <path>` 时应用覆盖；未知参数和缺失路径会静默回退，
  且其 ConfigMgr 没有 ChatServer 等价的必填字段与端口校验。本模块不把这些差异伪装成已通过契约。
- StatusServer 当前未检查 `BuildAndStart()` 返回值，GateServer 的端口转换仍使用 `atoi`；相应 fail-fast/释放
  行为要在生产代码明确实现后再增加 required 测试。
- 本模块不验证 Redis/MySQL/gRPC 远端依赖策略、ready 探针、信号优雅退出或共享状态清理；这些属于可重复
  Integration 环境建立后的服务生命周期测试。
