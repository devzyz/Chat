# PowerShell 测试

本目录测试 `scripts/chatserver-instances.ps1` 的公开命令行为，按配置校验与进程生命周期拆分：

- [validation](validation/README.md)：启动前的配置集合校验。
- [lifecycle](lifecycle/README.md)：启动失败、PID 身份、状态与冲突处理。

从仓库根目录运行：

```powershell
.\scripts\windows-local.ps1 -Task RunScriptTests
```

统一入口依次运行两个模块并传播非零退出码。CI 的 `static-check` job 执行该入口；当前轻量 runner
输出逐项 PASS/FAIL，尚未生成 JUnit。新增脚本测试必须放入对应模块目录并更新统一入口，不能只依赖测试发现通配符。
