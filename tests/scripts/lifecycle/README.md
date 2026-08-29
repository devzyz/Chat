# 实例生命周期测试

## 被测代码与契约

- 生产入口：`scripts/chatserver-instances.ps1` 的 `Start`、`Status`、`Stop`。
- 契约：子进程启动失败必须有诊断并清理状态；陈旧 PID 记录不得误杀无关进程；状态查询不得把身份不匹配的
  PID 视为运行中；已经运行的实例与新批次冲突时必须立即失败。

## 用例、依赖与隔离

Domain 为 Architecture，Level 为 Integration。`chatserver-instances.tests.ps1` 包含 4 个用例。测试用当前 PowerShell 进程构造受控 PID 身份边界，并用临时
目录保存 config、stdout/stderr 和 state；teardown 只清理本次 GUID 目录。测试不会终止身份不匹配的进程，
也不连接外部服务。依赖 Windows PowerShell 5.1。

| Test ID | Contract |
| --- | --- |
| A02-LIFE-01 | A child startup failure reports diagnostics and leaves no state. |
| A02-LIFE-02 | Status treats a reused PID with a different start time as stopped. |
| A02-LIFE-03 | Stop never terminates a process whose recorded identity is stale. |
| A02-LIFE-04 | Start rejects conflicts from an independently running instance. |

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunScriptTests
powershell.exe -NoProfile -File .\tests\scripts\lifecycle\chatserver-instances.tests.ps1
```

CI job 为 `static-check`。控制台保留逐 Test ID 的 PASS/FAIL，任一用例失败均返回非零退出码；统一入口把 4 个独立
JUnit `testcase` 写入 `build/test-results/script_integration.xml`，报告创建或解析失败同样使 runner 失败。

## 已知缺口

- 多实例批量启动中途失败时的完整 rollback 尚未作为已确认契约固化。
- Stop 当前不是面向真实 Server 的优雅 shutdown 测试，真实进程退出路径属于 Integration 测试。
