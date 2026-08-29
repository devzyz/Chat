# 实例配置校验测试

## 被测代码与契约

- 生产入口：`scripts/chatserver-instances.ps1 -Task Start` 的启动前校验。
- 契约：实例名、日志名和监听端口全局唯一；端口比较前规范化；配置文件名安全且唯一；配置文件、参数和
  必填值缺失时在创建 Server 进程前失败。

## 用例、依赖与隔离

Domain 为 Architecture，Level 为 Component。`chatserver-instances.tests.ps1` 包含 9 个用例。每次运行创建 GUID 命名的临时 fixture/state 目录，使用
`cmd.exe` 作为不会真正启动 ChatServer 的占位可执行文件，并在 `finally` 中清理。它不访问网络、数据库或个人配置。
依赖 Windows PowerShell 5.1。

| Test ID | Contract |
| --- | --- |
| A02-VAL-01 | Duplicate `SelfServer.Name` is rejected. |
| A02-VAL-02 | Duplicate `Log.Name` is rejected. |
| A02-VAL-03 | Cross-type TCP/RPC port collision is rejected. |
| A02-VAL-04 | Ports are normalized before collision comparison. |
| A02-VAL-05 | Unsafe config basenames are rejected. |
| A02-VAL-06 | Duplicate config basenames are rejected. |
| A02-VAL-07 | Missing config files are rejected. |
| A02-VAL-08 | A missing config argument is rejected. |
| A02-VAL-09 | Missing required config values are rejected. |

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunScriptTests
powershell.exe -NoProfile -File .\tests\scripts\validation\chatserver-instances.tests.ps1
```

CI job 为 `static-check`。控制台保留逐 Test ID 的 PASS/FAIL，任一用例失败均返回非零退出码；统一入口把 9 个独立
JUnit `testcase` 写入 `build/test-results/script_component.xml`，报告创建或解析失败同样使 runner 失败。

## 已知缺口

- 未验证合法配置能够启动真实 ChatServer；真实二进制和端口属于 Integration 测试。
- 未覆盖跨批次生命周期状态，本部分由 `lifecycle` 模块负责。
