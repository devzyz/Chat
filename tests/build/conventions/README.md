# 规范检查器回归

所属：Architecture / Unit（Git 格式与 C++/Qt/JS AST）、Integration（原生 PowerShell、
隔离 Git 历史与头文件契约）。测试入口显式注册 `test_conventions.py`，不依赖全仓发现。
临时 Git 提交仅发生在测试自建目录，不操作工作仓库的索引、分支或提交。

从仓库根目录首次准备检查工具，需要 Python 3.11+、Git 及 PowerShell 5.1 或 7：

```powershell
python -m pip install --disable-pip-version-check --no-deps --only-binary=:all: --target build/conventions/python -r scripts/conventions/requirements.txt
```

三项 Tree-sitter 工具固定版本，仅安装到忽略的 `build/conventions/python`，不修改应用依赖、
vcpkg 或全局 Python 环境。无兼容 wheel 时安装失败，不自动从源构建或换版本。
本地检查命令不隐式下载；Windows CI 的 static-check 显式准备相同工具。

```powershell
.\scripts\windows-local.ps1 -Task RunConventionTests
.\scripts\windows-local.ps1 -Task CheckConventions -ConventionBase origin/develop
.\scripts\windows-local.ps1 -Task AuditConventions
```

`CheckConventions` 默认比较 HEAD 与整个工作区，包含未暂存及未跟踪代码；指定基线可检查整个任务。
CI 从事件 JSON 读取 PR/push 的 SHA、来源分支及标题，使用共同祖先定位源码增量，
commit 范围排除目标分支已包含的提交及固定历史祖先。缺失对象、依赖或解析失败均返回非零。
标题 `edited` 事件会重跑。schedule/手动事件明确报告没有新增 Git 范围，仍运行检查器回归及既有全量流程。

`AuditConventions` 输出 `build/conventions/audit.json`，含每目录文件/对象数、每项路径/行号/原因、
解析失败及未支持文件清单。其退出 0 只表示审计文件生成成功，输出为 `AUDIT (not a pass)`，
不能当成零欠账或增量门禁通过。目录覆盖和欠账归属见 [治理计划](../../../docs/plans/CodeConventions.md)。

## 保护行为

- 合法/非法分支、普通/squash、真实 merge、revert、breaking 和发布标题；标题编辑、push 新分支、
  固定历史豁免、非法 PR 元数据不执行命令。
- C++ 重载、default/delete、类归属、直接头文件契约、歧义重载、Qt 信号槽/emit/foreach 与 Lambda。
- JS 具名函数、方法及匿名回调；PowerShell 原生函数/脚本块、帮助与 token 比较；待检查脚本从不执行。
- 注释/空白不扩大历史治理范围；新增或代码变化必须检查；错误语法与未知声明宏不能伪报通过。

本地与 CI 共用同一 Python 实现及 PowerShell 入口。任何断言失败或检查器非零退出都会使 static-check
失败，继而阻断现有 Windows 汇总；不新增悬空 Required Check，不计入业务 JUnit 报告数量。
语义、所有权和线程真实性仍需评审；支持边界见 [CI 治理](../../CI-GOVERNANCE.md#12-规范检查合同)。
