# 实例生命周期测试

## 被测代码与契约

- 生产入口：`scripts/chatserver-instances.ps1` 的 `Start`、`Status`、`Stop`。
- 契约：子进程启动失败必须有诊断并清理状态；陈旧 PID 记录不得误杀无关进程；状态查询不得把身份不匹配的
  PID 视为运行中；已经运行的实例与新批次冲突时必须立即失败。

## 用例、依赖与隔离

`chatserver-instances.tests.ps1` 包含 4 个用例。测试用当前 PowerShell 进程构造受控 PID 身份边界，并用临时
目录保存 config、stdout/stderr 和 state；teardown 只清理本次 GUID 目录。测试不会终止身份不匹配的进程，
也不连接外部服务。依赖 Windows PowerShell 5.1。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunScriptTests
powershell.exe -NoProfile -File .\tests\scripts\lifecycle\chatserver-instances.tests.ps1
```

CI job 为 `static-check`。结果写到控制台，任一用例失败均返回非零退出码；统一入口把本模块结果写入汇总报告 `build/test-results/script_unit.xml`。

## 已知缺口

- 多实例批量启动中途失败时的完整 rollback 尚未作为已确认契约固化。
- Stop 当前不是面向真实 Server 的优雅 shutdown 测试，真实进程退出路径属于 Integration 测试。
