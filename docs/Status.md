# 项目当前状态

更新：2026-10-02。目标是先完成基础聊天功能及必要正确性，再安排性能优化。
本页只维护当前状态、下一步与证据边界；此前实施过程、失败复现和修复日志见
[开发历史](history/DevelopmentHistory.md#basic-chat-20261002)。
历史中的分支、测试数量和待办仅代表记录时点。

## 已合并功能基线

`develop` 为 `37d6c5e`，[PR #28](https://github.com/devzyz/Chat/pull/28) 已于 2026-10-02 合并，
包含此前 PR #25～#27 的提醒、输入与搜索修复、CI 精简，以及本轮草稿、资料与好友管理。

| 范围 | 已实现与边界 |
| --- | --- |
| 账号与好友 | 注册、登录、重置密码、头像、本人资料、备注、好友申请/接受/拒绝/双向删除/重新添加、退出及切换账号 |
| 私聊与消息可靠性 | 文字、图片/视频/文件、UUID 幂等、持久化 outbox、送达/已读、离线补拉和重登恢复；自动重连未实现 |
| 群管理 | 建群、增删成员、退出、转让、改名/解散、代次与历史边界、群资源授权；20 人群、2 秒补拉，无群已读回执 |
| 桌面交互 | 本地历史/搜索、未读提醒、最新消息摘要排序、消息复制、按会话保留草稿/附件/光标/撤销、统一退出确认 |
| 存储与部署 | MySQL 007、SQLite 6；升级先备份并按 [Data](Data.md#operational-entry) 显式迁移，同步部署 Gate/Chat/Resource |

跨进程草稿、系统通知、拉黑、撤回、通话、多设备和性能专项未实现，不属于已完成基础功能的承诺。
当前使用手动服务地址和单账号单在线会话；本轮没有迁移个人数据库或恢复本机依赖。

## 基线验证边界

- [PR #28 快速 CI](https://github.com/devzyz/Chat/actions/runs/36930239519) 已通过 Windows 静态、Server、Qt、Varify 与回归汇总；Linux/完整真实环境未执行。
- [合并后快速 CI](https://github.com/devzyz/Chat/actions/runs/36971377967) 已通过；它验证基线提交，不代表本轮尚未提交的维护修改。
- 最近功能轮本地 Qt 74 项、Server 258 项、脚本 13 项通过；真实 MySQL/双 Chat TCP、Qt/SQLite 重启、资源流程与迁移有独立日志。
  Redis/Status 或认证替身的边界见 [历史证据](history/DevelopmentHistory.md#basic-chat-20261002)，不能当作全生产组合验收。
- PR #24 有 Windows/Linux/真实服务 47 项及 Qt 控件、资源存储的历史全量证据；不能替代最新代码的全量验收。
- 当前默认报告注册为 14 份/399 项，唯一数量来源为 `scripts/windows-local.ps1`。
  报告审计、实际执行和远端 CI 分开记录；人工四账号桌面验收尚未完成。

## 当前维护：冗余与文档复核

任务分支：`refactor/repo/prune-stale-maintenance`，基于上述 develop。

- 报告预期数量收敛到 runner 的单一注册表，各工具链读取同一项；Qt 另与实际 CMake 注册计数比对。
  移除重复总数和扫描源码找数字的判断，保留历史最低覆盖、具体合同名、失败/跳过、清理及敏感信息门禁。
- 修正资源部署版本、Status 依赖、群聊能力、未落盘提交所有权与资源发送操作说明；当前页归档多轮“当前工作”。
- 远端 9 个已合并任务分支与本地 8 个未占用旧分支已清理；PR #26 的 squash 分支先确认文件树与合入提交完全一致。
  删除前原 SHA 存于本地 `build/maintenance-branches-before.json`；清理结束时远端仅保留 develop/master。
- 本地 `origin/HEAD` 对齐实际默认分支 develop，清理已删除分支的跟踪配置和 66 条旧编辑器合并基准缓存。
  原值保存在 `build/maintenance-git-cache-before.json`。本地忽略的 AGENTS.md 补齐资源入口并修正架构链接说明。
- 保留 4 个已登记工作树及其分支；`design/message-state-control` 有未提交与未跟踪文档。
  `docs/local-maintenance-20260922` 有独有提交，也保留。未清理运行产物、数据、依赖或 `tests/auto/`。
- 完全相同的源码仅找到服务 Asio/日志薄适配，它们已委托 common 实现并绑定各自配置/单例，保留独立发布边界。
  跟踪树中未发现 DLL/EXE/OBJ/PDB/ZIP/log 产物；历史计划有追溯用途，不因体积大删除。

本轮实际验证：

- `RunAllTests -Configuration Release`：14 份报告/399 项通过、零失败/跳过，含 Server/Qt 正式构建；
  `build/maintenance-all-tests-final.log`。首次缺工具路径、随后沙箱 SDK 读取受限均未记为通过；补齐现有路径并获准执行后成功，未恢复依赖。
- CI/报告合同 25/25：`build/maintenance-ci-tests.log`；新报告回归在旧实现因重复总数产生 RED，
  `build/maintenance-reports-red.log` → `build/maintenance-reports-green.log`；追加非法 XML 回归后再次通过。
- 增量规范零违规：`build/maintenance-conventions.log`；现有 actionlint 验证 workflow 通过。
- 163 个相对文档链接及锚点、历史记录完整归档和 `git diff --check` 通过。
  这些是本地维护验证；本轮没有重跑 Linux/真实数据库专项或人工桌面验收。远端结果以维护 PR 的实际 CI 为准。

## 下一步

1. 完成本轮维护 PR 的 CI 与评审，再确认最新合并版本的完整真实环境验收。
2. 主体功能完成后进行四账号桌面走查：登录、好友、私聊/群聊、附件、断网重登、历史与账号切换。
3. 首版交付前核实迁移、安装和发布流程；当前未发布 GitHub Release，master 仍是旧发布基线，保留该长期分支。
4. 后续按 [能力缺口](plans/CapabilityGaps.md) 安排认证、实例失效和恢复演练，按
   [工程简化](plans/EngineeringSimplification.md) 逐步处理大文件与源码文本耦合；本轮不改消息/权限保护语义。

## 当前性能测试实施

新增 GitHub 手动 performance 模式及 smoke/baseline/stress 档位，入口见
[性能测试](../tests/services/performance/README.md)。代码基于已合并 PR #29；后续 Qt UI 提交
4bde00d 已静态核对，原分支保留，不混入本次服务端实现。

已完成首次远端框架合同与 Release 构建；[首次 smoke](https://github.com/devzyz/Chat/actions/runs/37007934357)
登录场景通过，但业务准备发现六十秒实例连接数发布延迟，未完成整套测试，不记为性能通过。
正在通过真实发布等待与重新登录修正双实例负载准备，并补充取消、原始证据和技能身份校验。
本地只执行语法、格式检查，未启动业务服务、恢复依赖或运行压测。最终基线待远端真实结果补充。
