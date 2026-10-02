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

远端框架合同与 Release 构建通过；[完整 smoke](https://github.com/devzyz/Chat/actions/runs/37012646624)
七个正式场景通过，但离线预热首次 HTTP 登录失败（9/10），整场按合同判失败。
Gate 在接受连接前创建六十秒期限，空闲超过期限后首个请求可能立即被关闭；这是静态确认的
高可信根因，尚未做独立缺陷复现，不在本轮修改生产实现。
[baseline](https://github.com/devzyz/Chat/actions/runs/37013592301) 已在相同提交 a192754 完成三轮，
21 个正式结果中 20 个通过，清理通过。第 1 轮离线预热及正式恢复各发生一次 TCP 断连，
与 smoke 的 HTTP 失败分开记录，整场判失败。完整指标与证据限制见 [测试报告](../测试.md)。
同提交的[普通 PR 回归](https://github.com/devzyz/Chat/actions/runs/37012645572)全部通过。
后续应修复 Gate 期限并定位离线登录断连；当前性能证据不能标记为通过或用于容量承诺。
本地只执行语法、格式检查，未启动业务服务、恢复依赖或运行压测；stress 未执行。

## 当前服务端缺陷修复（待远端验证）

目标已明确为先供个人使用、后续交付为局域网通信工具。任务分支 `fix/repo/server-lan-readiness`，
PR #30 已合并为 `2db7f55`，修复以该最新 develop 为基线。

- 已修改 Gate 接收后的请求期限、长资料登录分页及有界批量 ACK、MySQL 失败分类、改密联合条件、
  发布包版本化迁移入口与 Resource schema/就绪检查。
- 已补实例/会话租约、过期 Token、服务端随机盐口令派生、验证码原子消费/并发合并、输入验证、
  LAN 监听与内部 RPC 边界、资源预算及过载关闭诊断。
- 本地最终 Server 259 项、Varify 55 项、发布包合同 12 项通过；增量规范零违规，104 个相对文档路径有效。
  真实 MySQL/Redis/SMTP、双实例业务、包启动及性能基线安排 GitHub full/real_acceptance/performance；
  尚未完成的 run 不计为通过。本机没有恢复依赖或迁移个人数据库。

剩余边界：TLS、验证码用途隔离、Token 改密/退出撤销及续期、自动重连、引用感知资源清理、
一致备份恢复演练仍未实现；不能把本轮增量保护写成完整认证/容灾交付。
真实多机局域网、防火墙、人工四账号桌面走查与硬件故障无法由同机 GitHub 运行器替代，本轮按要求跳过。
