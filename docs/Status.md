# 项目当前状态

更新：2026-09-19。此页维护当前进度、下一步与验证边界；计划和阶段总结保留历史证据。
项目目标：先完成基本聊天功能，后续主要投入可测量的性能优化。

## 已合并基线

- PR #2～#7 已合入 develop；远端 develop 为 `2a706de802ff788ad8a6d62fdd8f395ae2d8f329`。
  本地主工作区保留功能修改，未切换或拉取；本地 develop 仍为 `719236e5cd7b80908a7b530f6e872b508fd9df2c`。
- PR #7 于北京时间 9 月 19 日 12:59 合并；[合并后 CI 35422743749](https://github.com/devzyz/Chat/actions/runs/35422743749)
  已启动，仍在运行，尚不能记为通过。
- 3B 传输集成、3C 当前版本真实依赖、3D 双实例消息与恢复已有验收记录。
- [合并后 CI 35337424539](https://github.com/devzyz/Chat/actions/runs/35337424539) 成功，
  Windows 静态检查、Server、Qt、VarifyServer 与 Regression checks 于北京时间 9 月 18 日 22:27 完成。
- 本轮依赖恢复 0 个包、编译 111 个包，安装约 3.3 小时；develop v3 缓存已保存约 1 GB。
  下一轮实际暖缓存复用仍待确认。Linux/full/release 本轮按 develop push 规则跳过。

## 本轮 CI 去重与旧方案清理（PR #7）

原工作分支 `fix/ci-redundancy`，基于合并前 develop `719236e`；本地分支已在合并后清理。
工作树 `Cache/worktrees/simplify-ci-regression` 保留在 detached HEAD `dbb7fcd`，构建产物与测试证据保留。

- 已提交并推送 `dbb7fcde69076190f5f82d3d6e744997e94aea90`，
  [PR #7](https://github.com/devzyz/Chat/pull/7) 已合入 develop，未发布。
- [CI 35360816538](https://github.com/devzyz/Chat/actions/runs/35360816538) 已完成此提交的验证；
  静态检查、Server、Qt、VarifyServer 与 Regression checks 全部通过。
  Qt 测试报告已上传，应用包生成和上传按预期跳过；本轮已读取 develop v3 缓存，
  实际 ABI 复用数量仍待核对最终日志。Linux/full/release 按 develop PR 规则跳过。

- Server 由 RunServerTests 一次构建生产与测试目标，移除之前的独立 BuildServers 调用。
- 静态 job 检查一次全仓测试注册；其成功后四个 CI 测试入口复用结果，测试与报告校验保持。
  本地入口默认检查，RunAllTests 在同一进程内缓存成功检查结果；跳过参数仅允许 CI 测试入口使用。
- phase3dEvidence.test.js 仅在 Linux 报告 job 执行一次；真实业务报告仍逐项校验。
- 普通 develop PR/push 不再生成或上传应用 ZIP；master、每周及手动全量保留打包，测试报告照常上传。
- 旧 3C-08 bootstrap CLI、实现及三个占位测试已移除；保留真实协议/schema 测试和 schema 标记。
  Linux 无用预检输出已删除，JSON PASS 校验保留；发布历史计划已明确标记为被替代，矩阵矛盾已修正。
- 本地验证：17 项 Node 定向回归、7 项发布包回归、8 项报告聚合测试、13 项脚本测试通过；
  actionlint、Linux 十项静态合同和十五项负向变异、Bash 语法与退役入口拒绝检查均通过。
  完整 Server/Qt 已由本次 PR CI 验证；合并提交的 push CI 仍需单独确认。
- 之前复核的 Linux 60 分钟总预算与各步骤预算协调问题仍待单独处理，本轮只修复确认的冗余项。

## 发布配置与尚未完成的验收

- 发布实现已在 develop：master push 全量通过后，组装同一次 CI 的 Windows 包，
  新 runner 下载冒烟，通过上传后字节回验，再公开 GitHub Release；没有第二次应用编译或人工审批。
- 已只读核对：Actions 已启用；master 严格要求 Regression checks 和 Full regression checks，
  管理员保护、禁止强推/删除保留。发布 job 显式声明 contents: write，未发现缺少 PAT 的配置依赖。
- 远端 master 仍为 `0622710`，尚未包含新工作流；当前没有 GitHub Release。
  因此“发布代码和权限已配置”不等于“master 已启用并成功发布”。
- 旧 release-uat / release-promotion 环境仍保留，新流程不引用它们，不会增加审批等待；未修改远端设置。
- 首次发布仍需：确认 VERSION，通过 develop→master PR 的全量检查后合并，再验证 master push
  的全量、包冒烟和实际发布。新 Linux 安装/v3 路径与首次发布均尚未完整托管验收。
- N-1 完整矩阵按当前 CI 政策暂缓；保留已有协议/schema 回归，不将缺失的跨版本验证记为通过。

## 当前分支

- `feature/avatar-resource-integration`，基于远端 develop `2a706de`（PR #7 合并提交）。
- 独立工作树：`Cache/worktrees/avatar-resource-integration`。
- 整合主工作区头像实现与资源传输实现，解决最新 develop 的消息提交、分页、客户端模块和测试注册冲突。
- 原主工作区及 `.worktrees/resource-stream-transfer` 保留为来源快照；无关文档整理、个人文件、
  `tests/auto`、旧 release 工作树修改不纳入本分支。后续头像和资源开发以本整合分支为准。
- 当前为本地整理，未推送、未创建 PR。PR #8 的 CI 工具链修改不属于本分支。

## 功能与兼容

- 头像裁剪、原子保存、安装目录下按环境/账号隔离、旧本地头像复制迁移、头像上传发布和双账号缓存。
- ResourceServer 提供流式上传、断点续传、Range 下载、摘要校验和资源权限；ChatServer 复用现有消息通道。
- 保留 develop 的已认证发送者、文本消息提交/重试、历史分页和客户端会话行为。
  资源消息同步写入 `chat_message.client_msg_uuid`，使重新登录后的历史保留身份。
- 数据库唯一迁移入口为 `schema/migrate.js`；版本 3 新增三张头像/资源表。
  版本 1/2 SQL 与校验和保持不变，完整 schema 指纹与 native 校验同步更新；不绕过结构验证。
- Qt 门禁在最新基线上新增 6 Unit / 4 Component，当前为 24 / 12 / 28；
  全仓聚合注册为 13 报告 / 357 项，资源专项仍有独立本地 runner。
- 合同与启用说明见 [Resources](Resources.md)、[ResourceServer](../ResourceServer/README.md)。

## 本次验证

- 客户端 Release 构建及 owning runner：24 Unit、12 Component、28 Integration 全部通过。
- Qt 资源专项 5 个业务场景通过（JUnit 另含 init/cleanup 两项）。
- 资源 Store 8 项、真实 HTTP 5 项、临时 MySQL 2 项，以及生产 Resource/双 Chat 流程通过。
- Server Unit 68 项、Gate/Status 各 2 项线程池回归通过。
- schema 静态合同 3 项、真实临时 MySQL 迁移回归 12 项通过。
- schema 2→3 专项升级通过：旧用户/消息保留，重复应用不改数据，删除头像表仍被结构验证拒绝。
- 初次旧 schema fixture 引发 native 校验失败；接入真实版本化迁移并补齐用户/会话 fixture 后通过。
  首次失败日志保留，不计为通过。
- 日志位于仓库主工作区 `Cache/avatar-resource-integration/`；本工作树报告位于
  `build/test-results/` 与 `build/resource/`。未运行全仓 357 项或远端 CI。

## 下一步与边界

1. 完成本整合分支的人工窗口与原生文件选择器验收，再按需要推送 PR。
2. Status/Redis 在资源流程中使用明确 fixture，尚不构成全真实依赖 E2E；8 GiB 实传与吞吐验证未完成。
3. 本次只迁移测试自建数据库；个人数据库、原运行产物和 vcpkg 安装树保持原样。
4. ResourceServer 正式 CI/发布纳入、真实 GUI 双客户端验收及性能基线仍需后续推进。
