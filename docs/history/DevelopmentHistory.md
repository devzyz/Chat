# 开发阶段历史记录

归档批次：2026-09-26、2026-10-02。以下内容从 docs/Status.md 的前序记录迁移，保留当时的分支、计数、
测试结果、未完成事项和远端快照；只调整标题级别与相对链接。日志路径相对于仓库根目录。
它们不是当前状态，也不是本次重新验证的结果。
文中的“当前”“最新”“下一步”“仍在运行”均指原记录时点；旧命令和旧授权不构成本次执行授权。
当前进度、下一步与验证边界只见 [Status](../Status.md)。本归档不继续维护现状。

## 前序记录：N9 历史余项整改完成

- 在 `chore/repo/incremental-conventions` 完成首次 N9 盘点回挂的命名与注释余项；
  最新 `origin/develop` 仍为 `ac282b4`，继续更新 [PR #16](https://github.com/devzyz/Chat/pull/16)。
  首次 2599 条诊断及 48 个解析失败的历史表保留在 [治理计划](../plans/CodeConventions.md#n9-全目录审计与余项归属)。
- 最新全目录审计：411 个支持语言文件、5160 个对象，**0 条诊断、0 个解析失败**。
  267 个非支持语言或非代码文件仍显式交给所属校验与评审；零诊断只代表公布的检查合同，
  不等于所有语言和业务语义都已由机器证明。明细位于 `build/conventions/audit-final.json`，可用 `AuditConventions` 重建。
- N2/N3：完成 Server 自有连接池/锁接口与 Qt 旧 UI、信号槽、单例接口的内部改名，调用和元对象引用同步。
  Qt 自动槽根据同版本 `.ui` 对象保留；gRPC、SDK 及既有协议字段不改名。
  schema JS 迁移对象统一为 lowerCamelCase，全部调用同步。
- N4/N5/N6：补齐生产、shared、Qt/Server/Varify 测试、进程工具和多服务协调器的中文职责说明。
  对所有权、事务、关闭、回调与重载逐类核对；不将 Status 遗留 MySQL 池描述为已有超时、健康检查或线程回收保证。
- N8：支持 QTest/GTest、WINAPI、花括号默认参数、转换运算符及同文件权威声明，未知语法继续失败。
  Qt 自动槽上下文绑定被检查的根目录与提交，避免工作树 UI 干扰 PR 检查；框架例外仍检查注释。
  凭据形状检查按文件比较新增/删除赋值多重集，原样赋值添加注释不误报，新增值或副本仍阻断。
- 规范复核发现的 15 组注释语义问题已修正并定向复查；需求复核发现的租约说明和 Qt UI 版本隔离问题已关闭。
  自动审计不替代这些语义核查，也不据此宣称修复未改动的历史业务缺陷。
- 本地实际通过：Gate/Status/Chat 生产编译及 Server 250 项；Qt 生产编译及 65 项；
  Resource 生产构建；Varify 54 项；PowerShell 脚本 13 项；CI/工具链/schema Node 检查 22 项；
  多服务协调器/报告/拓扑自测 26 项；规范检查器 33 项。
  原生测试之后的追加说明通过 token 核对；未重复执行与纯注释无关的全量业务场景。
- 相对本轮开始的 `8611c51`：228 个 C++ 文件在显式改名与删除冗余 stop 别名外 token 一致，
  56 个 JS 文件在五个迁移 API 改名外 AST 一致，3 个 PowerShell 脚本非注释 token 一致。
  统一入口的命名接线和凭据 diff 检查有独立回归。日志与对比明细位于 `build/conventions/`。
- 相对 `origin/develop` 的增量规范检查、测试注册、UTF-8、文档链接及 `git diff --check` 通过。
  依赖使用本机已有安装，未恢复 vcpkg，未修改生成代码、第三方或 `tests/auto/`。
- 验证边界：本轮未重跑 Docker 真实 Redis/Mailpit、完整 E2E 或人工桌面验收。
  PR 原提交 `8611c51` 的快速 CI 已通过，但不作为本次新提交的 CI 证据；更新后以新提交检查为准。

## 前序记录：N5～N9 规范实施与首次目录审计

- 按最新要求在 N4 工作分支继续完成 [N5～N9 本轮范围](../plans/CodeConventions.md#n5n8-核查范围)，
  初次完成时按要求保留为本地修改。用户现已授权提交 PR；原 PR #15 已合并，
  新分支 `chore/repo/incremental-conventions` 基于最新 develop `ac282b4`，其文件树与既有 `00bea3d` 一致。
- N5：补齐 Qt 顶层生产类、声明及无独立声明的辅助函数/回调，核实 `recordAt` 借用有效期、
  账号代失效、SQLite 工作线程退出顺序等；修正旧参数注释及 `appendMessage` 返回值说明。
  生产范围注释诊断为 0，旧 UI 命名和测试欠账单列 N9，不改 Qt 行为。
- N6：JS 内部接口改为 `getRedis`、`queryRedis`、`close`、`sendMail`，调用及测试同步，
  外部 `GetVarifyCode` 不变；补生产 JSDoc 和 `scripts/` PowerShell 帮助/回调说明。
- N7/N8：实现本地与 CI 共用 Git/源码增量检查，接入 Windows static-check 及既有汇总，
  PR 标题编辑触发复查。固定历史祖先、真实 merge、revert、squash、发布版本及错误退出均有回归。
  支持与不支持的语法、人工评审责任见 [CI 治理](../../tests/CI-GOVERNANCE.md#12-规范检查合同)。
- N9：覆盖全部自有适用目录，逐目录归回原工作包，清单见 [审计表](../plans/CodeConventions.md#n9-全目录审计与余项归属)。
  记录 2599 条历史诊断、48 个解析失败文件及 267 个非支持语言/非代码文件；它们尚未逐项整改，
  不把清单生成成功称为全仓零欠账。明细可由 `AuditConventions` 重建至 `build/conventions/audit.json`。
- 本地执行通过：规范检查器 23 项（含公开 CLI/PowerShell 失败传播），Varify 33 Unit + 21 Integration，
  脚本 9 Component + 4 Integration，CI/工具链/缓存治理 19 项，GitHub 下载器合同及 4 份工作流 actionlint。
  验证码测试首次发现注释插入造成的 JS return 换行问题，修复后 54 项重跑通过；没有弱化断言。
- 相对 `origin/develop` 的本地增量规范入口通过（0 违规）；UTF-8、39 个本地文档链接/锚点和
  `git diff --check` 通过。完整审计曾把类外 operator 当成普通函数，补回归修正后得到上列最终计数。
- 103 个 C++ 文件相对 HEAD 的非注释 token 完全一致；12 个 JS 文件的 AST 除四个明确改名外保持一致，
  7 个 PowerShell 脚本非注释 token 未变，统一入口新增规范任务另由回归验证。
  只在 `build/conventions/python` 准备检查工具，在 `build/conventions/actionlint` 准备校验器；未恢复 vcpkg。
  本轮未重新编译 Qt/Server，未重跑真实 Redis/Mailpit/Docker/E2E，也未声称未提交代码已有远端 CI 结果。
- 下一步：提交独立 develop PR 并等待远端 CI；本地结果不代替远端检查。N9 回挂历史项按所属模块继续拆批治理。

## 前序记录：N4 Server 契约注释

- 本轮基于最新远端 develop `aa154435af656985a885971b70e54e1ee9f8f546`，工作区原先干净；
  已快进核对后创建 `docs/repo/server-contracts`，按计划每批独立提交 PR。
- 完成 [N4 四组接口链](../plans/CodeConventions.md#n4-接口链与核查边界) 的注释治理：会话与生命周期、
  文本提交与存储、Gate/Status 路由及 ResourceStore。补齐类职责、返回值、失败语义、线程和借用有效期。
- 明确 `Register` 返回存活旧会话但不关闭它；`Send` 回调只确认入队；Server 停止回调不代替
  生命周期存储任务的 `Drain`。记录 UUID 重试、提交结果未知、部分文件写入后查询偏移等边界。
- 对照生产实现及所属测试 README 核查；25 个 C++ 文件相对基线的非注释 token 序列完全一致，
  包含字符串字面量。UTF-8、文档本地链接及 `git diff --check` 检查通过。
  本轮为纯注释与文档变更，未重新运行编译或业务回归，不复用历史测试结果声称本轮通过。
- N5 Qt 注释、N6 Node/脚本、N7 Git 自动门禁、N8 语法感知增量检查及 N9 全目录收口仍待完成。
  本次接口范围之外的 Server 历史项纳入 N9 审计；未宣称全仓注释覆盖或自动门禁已启用。
  远端 CI 结果以本次 PR 实际检查为准。

## 前序记录：命名与注释规范化 N0～N3

- 分支 `refactor/repo/core-naming` 基于已同步的 develop `2307999`，N0 规范、N1 注释、N2 内部改名、N3 Qt 核心命名分别提交，本轮汇总一个 develop PR。
- N1：精简 TcpMgr、Gate/Chat LogicSystem 的空模板与失效注释，纠正 ACK/已读和会话绑定说明；核心作用通常一行表达。
- N2：修正 GetUesr/backanme 标识符，明确回执处理、HTTP 路由注册和消息回调名称；声明、实现、调用及静态接线检查同步更新。
- N3：统一 UserMgr、TcpMgr、AuthFlowCoordinator 的方法/信号/槽命名，同步 LoginDialog 登录通知、生产调用、测试及当前合同引用；补齐受影响接口的简短职责说明。
- 本地验证通过：生产 Server/Qt 编译，Server 250 项、Qt 65 项、脚本 13 项回归；附件页面生命周期专项 1 项通过。无失败或跳过，日志见 `build/code-conventions/`，公开报告见 `build/test-results/`。
- 对比 develop 的 56 个 C++ 文件通过显式改名映射后的 token 序列核对；未改变控制流、常量、协议字段或字符串值。文档链接、编码及 diff 检查通过。
- Server 验证曾因缺少环境路径及沙箱 SDK 访问失败，指定既有工具路径并取得所需执行权限后通过；附件独立运行补齐 CTest 同等 Qt 插件路径后通过。未恢复依赖或修改业务逻辑。
- [批次计划](../plans/CodeConventions.md) 下一步为 N4 Server 契约注释及 N5 Qt 其余注释；其余旧 UI 命名在后续治理/收口中处理。N7/N8 自动门禁尚未实现，可按依赖另行安排；远端快速 CI 结果以本次 PR 的实际检查为准。

## 前序记录：消息状态控制

- 从最新远端 develop `34caf1e` 同步后创建 `feat/message-state-control`；前序修复 PR #12 已合入。
- 实现持久化发送批次/attempt/有界重试，区分 Queued、Sending、Uncertain、Failed、Sent、Delivered、Read。
  ACK 只确认 Sent；接收者落盘确认 Delivered，真实前台气泡连续可见 500ms 才产生 Read 意图。
- 新增能力协商、1029～1033 回执协议、跨 ChatServer 提示、独立 revision 补拉与单调合并。
  SQLite schema 2 保留升级快照；MySQL migration 004 与 Gate/Chat/Resource 校验合同同步。
  方案、兼容性与部署步骤见 [MessageStates](../MessageStates.md)。
- 本地公开 Server runner 250 项、Qt runner 65 项、脚本 runner 13 项通过。
  Qt 附件组件 5 个业务方法通过；protobuf 兼容和生成漂移检查通过。
- 独立临时 MySQL 的 12 项迁移合同和 5 项消息/回执持久化合同通过；双生产 ChatServer 的
  跨实例 Delivered/Read、权限拒绝、重登补拉，以及真实 Qt/SQLite 回执往返与进程恢复通过。
  日志见 `build/message-state-*.log`、`build/message-sync-integration.log`，报告见 `build/test-results/`。
- Status/Redis 在上述补充联调中仍为显式替身；未宣称生产 Redis 或人工桌面全流程验收。
  本轮没有迁移个人数据库、恢复 vcpkg 或重跑未改动的 Varify 回归；注册总数以 runner 为准。
- 下一步：提交 develop PR 并触发快速 CI，远端结果以当前提交的 Actions 检查为准。

## 前序记录：连接与输入错误修复

- 分支 `fix/storage-input-hardening` 基于 develop `1f7e2fe`。按本轮要求保留依赖版本、
  数据库结构、消息协议和既有状态机设计；本次不做总体优化方案或认证流程改造。
- Gate/Chat/ResourceCatalog 共用有界 MySQL 连接池：锁外检查连接、借出前替换失效连接、
  归还时回滚未完成事务，清理失败则丢弃。修复私聊双方顺序及异常导致的部分提交。
- 修复 Gate 非法 URL 编码导致断言退出、Chat 处理器异常逃出线程，以及 Gate/Status
  Redis RPUSH/HGET 等返回值误判和字符串截断。
- 本地公开 Server runner：75 Unit / 58 Component / 107 Integration / 4 Chat gRPC /
  Gate 与 Status 各 2 项，共 248 项通过；脚本公开 runner 13 项通过。
- 独立临时 MySQL 的 4 项测试通过，包括主动断开池连接后恢复、交换双方顺序复用私聊、
  中途注入 SQL 错误后无孤立会话；只操作测试自建数据库。ResourceServer 生产构建通过。
- 测试注册和 diff 检查通过。日志位于 `build/hardening/`，报告位于 `build/test-results/`
  与 `build/resource/catalog.xml`。Qt、完整资源消息流及真实 Redis E2E 本轮未重新执行。
  全仓注册为 13 报告 / 380 项，不等于本地全仓执行通过。
- 下一步：向 develop 提交 PR，触发快速 CI；远端结果以 PR 当前提交检查为准，尚未计为通过。

## 前序记录：本地消息持久化与增量同步

- PR #9 已合入 develop；本次从 `eed758a37932357a749afc8f0929402b89dc25bf` 创建
  `feature/local-message-sync` 独立工作树。主工作区已有修改保留，未用旧工作区覆盖远端代码。
- 按账号隔离 SQLite、先落盘再发送、本地历史分页、上线与实时通知后的增量补拉、事务游标和重启恢复已接入。
  复用现有 `chat_message.client_msg_uuid`，没有新增 MySQL 迁移；流程与部署见 [MessageStorage](../MessageStorage.md)。
- 需求/规范复核发现的问题已修复：UUID 身份冲突不能静默跳过、满队列账号切换不能丢失打开操作、
  旧历史非法请求保留错误回包、事务对象不可复制且回滚失败关闭连接。
- 客户端公开 runner：24 Unit / 13 Component / 28 Integration 通过；资源 Qt 的 5 个业务场景通过。
  脚本公开 runner：9 Component / 4 Integration 通过。新增同步专项：3 个真实 MySQL 测试、
  双 ChatServer TCP 流程及 Qt/SQLite 进程重启增量验证通过。
- 同步专项首次移植遗漏测试 UID 计数器，补齐测试数据后重跑通过；没有弱化生产 schema 校验。
  测试仅迁移临时 MySQL，未修改个人运行库、依赖安装树或原工作区运行产物。
- 本次聚合注册为 13 份报告 / 358 项；未声称执行全仓聚合、完整 GUI 或生产 Status/Redis E2E。
  报告保存在本工作树 `build/test-results/`、`build/message-sync/`。远端 PR CI 以当前提交检查为准。
- 下一步：完成 PR 检查；实际启用时先更新全部 ChatServer，再部署带 QSQLITE 的客户端。

以下为前序阶段和头像资源功能的历史记录，数量与证据按原阶段保留。

## 已合并基线

- PR #2～#8 已合入 develop；远端 develop 为 `4ae014c90e5084b1d0bc152de87b748d4f3d7673`。
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

- `feature/avatar-resource-integration`，已无冲突合并远端 develop `4ae014c`（PR #8 合并提交）。
- 独立工作树：`Cache/worktrees/avatar-resource-integration`。
- 整合主工作区头像实现与资源传输实现，解决最新 develop 的消息提交、分页、客户端模块和测试注册冲突。
- 原主工作区及 `.worktrees/resource-stream-transfer` 保留为来源快照；无关文档整理、个人文件、
  `tests/auto`、旧 release 工作树修改不纳入本分支。后续头像和资源开发以本整合分支为准。
- 本轮按要求将头像与资源变更提交至 develop PR；PR #8 的工具链修改已作为合并基线继承，
  不属于本 PR 的功能差异。远端检查结果以 GitHub 当前提交为准。

## 功能与兼容

- 头像裁剪、原子保存、安装目录下按环境/账号隔离、旧本地头像复制迁移、头像上传发布和双账号缓存。
- ResourceServer 提供流式上传、断点续传、Range 下载、摘要校验和资源权限；ChatServer 复用现有消息通道。
- 保留 develop 的已认证发送者、文本消息提交/重试、历史分页和客户端会话行为。
  资源消息同步写入 `chat_message.client_msg_uuid`，使重新登录后的历史保留身份。
- 数据库唯一迁移入口为 `schema/migrate.js`；版本 3 新增三张头像/资源表。
  版本 1/2 SQL 与校验和保持不变，完整 schema 指纹与 native 校验同步更新；不绕过结构验证。
- Qt 门禁在最新基线上新增 6 Unit / 4 Component，当前为 24 / 12 / 28；
  全仓聚合注册为 13 报告 / 357 项，资源专项仍有独立本地 runner。
- 合同与启用说明见 [Resources](../Resources.md)、[ResourceServer](../../ResourceServer/README.md)。

## 本次验证

- 客户端 Release 构建及 owning runner：24 Unit、12 Component、28 Integration 全部通过。
- Qt 资源专项 5 个业务场景通过（JUnit 另含 init/cleanup 两项）。
- 资源 Store 8 项、真实 HTTP 5 项、临时 MySQL 2 项，以及生产 Resource/双 Chat 流程通过。
- Server Unit 68 项、Gate/Status 各 2 项线程池回归通过。
- schema 静态合同 3 项、真实临时 MySQL 迁移回归 12 项通过。
- schema 2→3 专项升级通过：旧用户/消息保留，重复应用不改数据，删除头像表仍被结构验证拒绝。
- 合并 PR #8 后，测试注册、diff 检查，以及工具链/缓存/CI 路由/schema 的 22 项定向回归通过。
  本轮未改生产功能代码，先前业务验证保留；不把它当作新提交已通过远端 CI 的证明。
- 初次旧 schema fixture 引发 native 校验失败；接入真实版本化迁移并补齐用户/会话 fixture 后通过。
  首次失败日志保留，不计为通过。
- 日志位于仓库主工作区 `Cache/avatar-resource-integration/`；本工作树报告位于
  `build/test-results/` 与 `build/resource/`。未运行全仓 357 项或远端 CI。

## 下一步与边界

1. 跟进头像与资源 PR 的当前提交检查，完成人工窗口与原生文件选择器验收。
2. Status/Redis 在资源流程中使用明确 fixture，尚不构成全真实依赖 E2E；8 GiB 实传与吞吐验证未完成。
3. 本次只迁移测试自建数据库；个人数据库、原运行产物和 vcpkg 安装树保持原样。
4. ResourceServer 正式 CI/发布纳入、真实 GUI 双客户端验收及性能基线仍需后续推进。


<a id="basic-chat-20261002"></a>

## 2026-10-02 归档：基础聊天实施与 PR #24～#28 证据

更新：2026-10-02。此页维护当前进度、下一步和验证边界。历史计划与报告保留，
不作为本轮已经验收的证据。目标仍为先完成基础功能及必要正确性，再另行安排性能优化。

### 当前实施：基础聊天易用性与好友管理

任务分支 `feat/repo/basic-chat-usability` 基于最新 `develop` 的 `45a93b0`，包含已合并 PR #27
及其前置 PR #25/#26；实施前与收尾时均核对远端，创建本 PR 前没有其他面向 develop 的未合并 PR。
实现和复核修复均已完成本地验证，已提交 [PR #28](https://github.com/devzyz/Chat/pull/28) 至 `develop`。
以下是本轮本地证据，远端 CI 结果以本轮 PR 为准，
不宣称已合并验收。

已实现：按会话保留进程内草稿、附件、光标与撤销；退出/切换账号入口和统一未提交内容提示；
单条正文/附件名称复制；按最新本地消息排序的摘要与时间；修改本人用户名/描述；好友申请、同意、
拒绝、双向删除与重新申请。失效会话只读并保留历史/资源，重新加回复用原会话且旧待发请求不会复活。
隐藏未实现的语音/视频入口。复用既有 TCP、存储线程、目录 JSON、编辑器文档和提交控制器；
仅提取社交结果提示、版本目录 SQL 与新增私聊校验，没有另建消息或资源体系。

MySQL 007、SQLite 6 和 `basic_social_v1` 协商合同已同步文档。迁移仅在自建临时 MySQL 验证，
未迁移个人数据库，未恢复或安装依赖。部署前需要按 [Data](../Data.md#operational-entry) 备份并显式迁移，
同步更新 Gate/Chat/Resource；客户端本地升级前备份旧库。

本轮自动证据：

- 草稿切换旧实现 RED / GREEN：`build/usability-drafts-red.txt`、`build/usability-drafts-green.txt`。
- 私聊权限负向控制移除校验后，新回归明确失败；已恢复生产源并通过：
  `build/usability-social-red.txt`、`build/usability-social-green.txt`。
- `RunClientTests -Configuration Release`：74/74（25 Unit、19 Component、30 Integration），
  `build/usability-review-client.log`；新增生命周期用例最终复跑 `build/usability-review-lifecycle-final.log`。草稿、存储与摘要回归已进入现有默认入口。
- `RunServerTests -Configuration Release`：7 份报告、258 项，零失败/跳过，
  `build/usability-review-server.log` 和 `build/test-results/server*.xml`。
- 真实 MySQL 原生 10/10 与双生产 ChatServer TCP、Qt/SQLite 重启回归通过：
  `build/usability-review-social-green.log`。包含资料冲突/重名、好友全生命周期、按字节多页目录、旧版本拒绝、
  资源引用保留及大正文通知。Redis/Status 为隔离替身，不代表全生产依赖验收。
- 真实 Qt 资源/群/查找控件 20/20，零失败/跳过：`build/usability-resource-widgets.xml` 和 `.txt`。
  HTTP/文件系统为真实本机服务，认证为测试替身。
- 真实资源 catalog 4/4、生产 Resource/双 Chat 的图片/视频/文件流程通过：
  `build/usability-resource-integration.log`；包含 250 帧视频解码、续传、摘要及重复消息不再推送。
- MySQL 2→7 保留旧数据、重复应用和漂移拒绝通过：`build/usability-schema-upgrade.log`。
- 迁移合同 12/12、Node owner 3/3：`build/usability-schema-tests.log`、`build/usability-schema-owner.log`。
- 增量规范零违规：`build/usability-review-conventions.log`；Python TCP 流程已复核并真实执行。
  文档相对链接及 `git diff --check` 已通过。

过程中实际修复两处回归：会话展开入口保持原分页调用语义；已提交的大消息重试不重复推送，
本机/跨服通知统一按帧上限降为同步提示。通知断连 RED 位于 `build/usability-social-integration.log`，
最终大正文 TCP 回归已通过。提醒测试保持“第一页之外的未读会话”前提，没有降低未读合同。

提交前双轴复核发现并修复：合法长中文字段组合导致目录超限并阻塞发送；社交异步生命周期测试缺口；
资料查询的过时 Redis 注释和目录刷新重复回调。长字段缺陷真实 RED 为 `build/usability-review-long-red.log`，
GREEN 为上述真实 TCP 回归；仅社交目录读响应 1047 复用现有按类型限长机制放宽至 8192 字节，
写请求仍为 2048 字节。关系完整落盘即就绪，申请目录失败不阻断发送；没有增加分段协议或新依赖。
客户端新增 `session_reset.social_lifecycle`，公共 runner 基线相应增加至 14 份/399 项；本轮仍按所属模块验证，
不把注册总数记为全仓执行数。脚本回归 13/13，证据为 `build/test-results/script_component.xml` 和 `script_integration.xml`。

下一步是 PR #28 远端 CI、评审及主体功能完成后的人工桌面验收。按本轮范围，跨进程草稿、自动重连、
系统通知、拉黑、撤回、通话、多设备和性能专项仍未实现；这些不是本次计划的完成条件。
以下章节保留此前已合并工作的历史证据。

### 当前补充：前端输入与搜索正确性修复

本轮完成基础功能与必要封装修复，以及复核发现的相关缺陷；不进行群管理整体拆分、全量组件改名或性能专项。
账号级 MessageSubmissionController 固定发送目标和 UUID，串行上传并按明确落盘通知移交 outbox；
失败草稿由控制器独占保留，取消不留消息服务中的第二份重试副本。长文本按最终 JSON UTF-8 字节预算拆分。
编辑器读取无副作用，内部剪切/复制/粘贴保留附件身份；文档、可达撤销记录、剪贴板和任务共享临时文件。
图片准备移入有界后台队列，满载明确拒绝，无引用任务跳过准备；不可恢复的撤销分支释放附件。
主动退出/关闭提示未落盘提交，Cancel 保留任务。未落盘草稿仍不支持跨进程恢复。

UserSearchController 管理编号、十秒超时和取消；真实等待窗 Esc 以及离开搜索界面均使当前请求失效。
1007/1008 保留旧客户端兼容，新客户端遇到无编号响应提示更新服务器；迟到、重复和旧账号结果不进入新查询。
基础控件恢复字符长度、有效鼠标释放、键盘激活及正确的动画对象所有权。

本轮实际证据（本地执行，不代表远端 CI 或人工桌面）：

- 原输入/控件 RED：`build/composer-red-isolated.txt`。复核新增 RED：
  `build/composer-review-red.txt`（附件剪切粘贴降为文本）、`build/composer-cancel-red.txt`（取消后旧服务重新提交）、
  `build/search-cancel-red.txt`（Esc 未终止等待）、`build/preparation-capacity-red.txt`（后台队列无上限）。
- 最终 `RunClientTests -Configuration Release`：73/73 CTest（25 Unit、18 Component、30 Integration），
  `build/review-client-final.log`；报告注册及数量由 runner 校验。新增搜索控件回归进入默认 Component。
- 真实资源/群/查找 Qt 套件：20/20、零失败/跳过，`build/review-resource-final.xml` 与 `.txt`。
  其中混合提交在首段落盘、附件首块确认后关闭真实 HTTP 服务；同目录同端口重启后从确认偏移恢复，
  最终只产生三条有序消息。另覆盖页面按钮/Enter 和搜索弹窗取消、换账号后的旧结果隔离。
- `build/preparation-capacity-green.txt`：有界后台准备、无引用取消，以及真正排入旧账号 SQLite 队列后
  stop/start 的迟到回调隔离通过；退出提示 Cancel 与写盘失败取消已纳入默认组件回归。
- `BuildServers -Configuration Release`：通过，`build/review-server-build.log`；使用现有只读依赖，未恢复依赖。
  资源测试 host 的可选重启端口构建通过，`build/review-resource-host-build.log`。
- `tests/server/message-sync/integration.py`：通过，`build/review-search-server.log`；
  临时 MySQL 原生 6/6、生产双 ChatServer TCP、搜索成功/错误编号回传和旧客户端兼容。
  Redis/Status 沿用隔离替身，没有访问个人数据库。
- 增量规范检查通过：`build/review-conventions-final.log`。Python 协议回归另经源码复核和实际执行。
  `build/review-widget-evidence.txt` 的报告校验测试通过，实际 20 项 Qt 报告与校验器名称集合一致。
- 提交前 Standards/Spec 两轴复核提出的队列容量及旧存储回调证据问题均已补齐，复核未留阻断项。

任务分支为 `fix/client/preserve-drafts-and-cancellation`。远端 CI 状态以本轮 PR 为准；人工桌面验收继续后置。
以下保留基础聊天收口的历史证据及边界。

### 当前工作：基础聊天软件功能收口

本轮按用户目标只收口基础软件功能，不将性能优化、认证重构、自动服务发现或运维体系作为完成门槛。
用户明确要求本阶段通过自动化自查，人工操作验收留到主体功能完成后，不作为本轮提交 PR 的阻塞条件。
PR #23、#24 均已合入 develop，当前任务分支 `fix/client/basic-chat-completion` 基于 `aa868c8`。
PR #24 最终提交 `39b35f1` 的 [完整 CI](https://github.com/devzyz/Chat/actions/runs/36810440438)
已通过 Windows、Linux 和真实环境验收；下载核验真实服务 47 项、Qt 控件 18 项、资源存储 8 项均无失败/跳过，
源码 SHA 一致且清理完整。该提交与 develop `aa868c8` 文件树一致；这些是已合并基线的证据，不冒充本轮修改的 CI。

本轮修复会话行尚未创建、窗口重建或离线补拉后的新消息提醒丢失。复用账号 SQLite 与存储工作线程，
增加本地查看边界，不改服务端协议。私聊与群文字/资源统一计数，排除本人消息；打开前台会话后清除已展示快照的提醒，
不把本地清除当成对方已读回执。SQLite 升到 5，旧库先备份，保留正文/目录/outbox/回执；旧历史不重新提醒。
新增实际控件回归已观察 RED（期望 2、实际 0），修复后通过，且纳入默认客户端 Component 回归。
本轮完整回归结果见下方“本轮验证”；四账号人工桌面仍与自动验证分开记录。

继续自查修复分页补拉的刷新遗漏：每个非空页提交后立即发布消息变化，后续页失败不再隐藏已保存消息。
补齐隐藏/最小化/模态遮挡期间的提醒自动回归，并修复测试客户端恢复登录后遗留账号数据库的清理缺口。

| 阶段 | 实现与当前验收边界 |
| --- | --- |
| 1 动态群成员 | 群资料分页、添加/移除、退出、转让、改名/解散；版本和 UUID 幂等、历史边界、重入代次、只读状态、待确认管理命令落盘；自动验证已执行，人工桌面验收待完成 |
| 2 群资源 | 图片/视频/文件复用既有上传、续传、提交与下载；当前成员及入群边界授权；PNG/十秒 AVI/文件分别完成真实 HTTP/TCP 授权、同步和摘要验证；三类 Qt 卡片已测，人工组合仍待验收 |
| 3 联系人与查找 | 个人好友备注、全部本地联系人/群筛选、当前会话本地文字/文件名搜索与分页定位；存储、TCP 及真实控件自动验证已执行；超过一页搜索定位、70 联系人/70 群筛选与备注提示已覆盖 |
| 4 业务收口 | PR #24 最终 Windows/Linux/真实服务自动验收已通过；本轮收口本地消息提醒，四账号人工桌面走查仍未执行 |

依赖保持阶段 1 → 阶段 2/3 → 阶段 4。没有引入配置部署、服务发现、性能专项或认证体系重构。

### 已合并首版合同

- MySQL 006：可转让群主与不可变创建者分离；保存原建群名称和成员，管理操作结果与变更同事务。
  旧群成员迁移为有效、代次 1、边界 0；群主用户不存在、缺群主成员或角色矛盾明确阻止迁移。
- SQLite 4：保留消息、回执、outbox 和游标，升级前快照；群资格与待确认管理命令沿用目录存储。
  旧版本目录、旧代次正文/发送 ACK 不覆盖新状态；离群停止发送、轮询和自动重发，保留历史及不确定状态。
- 1036/1037 群资料、1038/1039 管理、1040/1041 备注；`group_membership_v1` 协商及群发送代次。
  目录每 10 秒完整分页、有效群每 2 秒增量同步，不新增跨服广播。
- 群资源授权按任一合法引用判断；离群不会撤销独立私聊/本人上传权限，不删除文件字节。
- 详细合同见 [Protocol](../Protocol.md#基础文字群聊)、[Data](../Data.md)、
  [MessageStorage](../MessageStorage.md)、[Resources](../Resources.md) 和 [测试矩阵](../../tests/TEST-CONTRACT-MATRIX.md)。

### 已执行证据

所有数据库测试使用独立临时 MySQL，未迁移个人运行库；构建复用现有 vcpkg/Qt/Node 依赖，未恢复依赖。
本地日志位于本工作树忽略目录 `build/`，不是远端 CI 证据。

| 检查 | 结果与证据 |
| --- | --- |
| Server Release / Qt Release 构建 | 通过；`build/server-regression-final.log`、`build/resource-build-final.log`、`build/client-build-closure.log` |
| Server 既有回归 | 7 份报告共 258 项通过，0 失败/跳过；`build/server-regression-final.log`、`build/test-results/server*.xml` |
| Qt CTest | 66/66 通过；`build/client-all-tests-closure.log`；包含存储、会话隔离、模型、传输等现有入口 |
| 群/消息 MySQL + 双 ChatServer TCP | 10/10 原生用例及四账号 TCP 流程通过；`build/sync-integration-final.log`，含 Qt/SQLite 跨进程恢复；Redis/Status 为替身 |
| Schema migration | 12/12 通过；`build/migration-orphan-green.log`；孤儿群主新增回归修复前失败（`build/migration-orphan-red.log`），修复后拒绝且保留旧数据；Node owner 3/3（`build/migration-owner-final.log`） |
| Resource catalog + HTTP/TCP | 4/4 catalog 及 PNG/AVI/文件各自群附件授权流程通过；`build/resource-integration-final.log`；Redis/Status 为替身 |
| HTTP 流式传输/视频 | 5/5 通过；`build/resource-stream-three-types.log`；包含续传、摘要和 250 帧解码；认证为测试替身 |
| 资源旧库升级 | 2→6 数据保留、重复应用和漂移拒绝通过；`build/resource-upgrade-final.log` |
| Qt 资源/群/查找控件 | 完整 18 项通过、0 失败/跳过，进程正常退出；`build/resource-ui-closure.txt`；历史定位另有 5 次独立重复通过（`build/history-repeat-1.txt` 至 `5.txt`） |
| 静态与注册 | 增量规范、测试结构与 diff 检查通过；`build/conventions-final.log`、`build/test-structure-final.log`；Python 变更另经人工源码复核 |

### 收口修复与证据边界

- 群资料面板保留拒绝原因；目录落盘立即更新权限；完整同版本成员分页前禁用成员变更。
  原三项 RED 见 `build/panel-red.txt`；真实 TCP 新版本回包自取消的 RED 见
  `build/panel-tcp-red.txt`，修复后 `build/panel-tcp-green.txt` 9 项通过。
- 搜索定位只消费匹配范围的历史回包，避免普通历史刷新抢走定位；切换会话清理旧定位及备注提示意图。
  后者修复前失败见 `build/ui-switch-red.txt`，最终完整控件回归通过。
- 自动控件使用真实 Qt 和 SQLite；备注失败结果及资源下载完成信号由测试驱动。
  三类资源 HTTP/TCP 使用真实 MySQL、生产 Chat/Resource，但 Redis/Status 仍是明确替身。
  上述证据均不能替代完整生产依赖或人工桌面验收。

### 按需真实环境 CI 检查点

- 已实现 `CI` 手动运行参数 `real_acceptance`，默认 `false`；普通提交/PR/定时运行不触发新增专项。
  显式开启时，Windows 与完整 Linux 同提交通过后才产生 `Real environment acceptance` 成功结果。
- 在现有 3D 真实服务编排中加入四个独立 Qt 客户端，使用 Docker MySQL/Redis/Mailpit 和生产
  Gate、Status、双 Chat、Varify、Resource。新增 14 项群管理/历史边界/PNG、十秒视频与文件授权/
  离线重登/原 UUID 重试/备注搜索验收，与原 33 项合计 47 项；报告缺失、失败、跳过、SHA 不符或清理失败均拒绝。
- Qt 自动控件与资源存储另有 18/8 项及截图证据；这些自动证据不等同于人工桌面走查。
  操作入口和产物见 [服务测试入口](../../tests/services/README.md)。
- 本轮实际本地验证：Qt CTest 66/66；CI/报告合同 Node 19/19；Qt 控件 18 项和资源 Store 8 项真实报告校验通过。
  日志为 `build/client-all-tests-real-acceptance.log`、`build/real-acceptance-contracts-final.log`、
  `build/acceptance-widget-evidence/`。可选 Resource CMake 配置图检查通过，不代表 Linux 编译通过。
- 测试驱动修复后台同步错误抢占群管理结果：真实 SQLite 写锁 RED `build/session-lock-red.xml`，
  GREEN `build/session-lock-green.xml`；释放锁后原 UUID 恢复通过。未改变生产协议或错误过滤。
- 首次按需运行 [36735068842](https://github.com/devzyz/Chat/actions/runs/36735068842) 已实际执行但失败：
  Linux 第二次 CMake 配置丢失 JDBC 原生依赖，ResourceServer 链接失败；Windows Qt 会话驱动一项失败。
  真实服务流程未启动，47 项业务不能记为通过；`Real environment acceptance` 当次误绿也不是成功证据。
- 本轮修复重复配置的 MySQL 包缓存初始化标记，以及汇总 Bash AND 列表被成功摘要覆盖的退出码。
  配置回归 `build/reconfigure-red.log` → `build/reconfigure-green.log`；门禁真实 Bash 回归
  `build/ci-gate-red.log` → `build/ci-gate-green.log`（15 项，其中门禁覆盖 25 种上游结果组合）。
  默认配置、Linux 预检合同及测试注册通过；真实 Linux 链接和完整业务已在下述同提交手动运行中通过。
- Windows 原失败本地连续 8 次未复现，不能认定已修复。已为会话驱动测试启用 Qt stderr，
  缺失控制响应仅输出命令名/ID、耗时、进程/管道状态，避免再次丢失失败断言；不输出请求、响应或凭据。
  强制群成员多页响应、群管理 ACK 丢失的真实环境组合仍不由这 14 项单独证明；已有局部回归与人工缺口继续分别记录。

### PR #24 修复过程记录（以下保留当时的结果）

- 提交 `368bbed` 的手动完整运行 [36740844790](https://github.com/devzyz/Chat/actions/runs/36740844790)
  实际通过 Windows、完整 Linux 和 `Real environment acceptance`。下载产物再次验证：
  真实服务 47/47、Qt 控件 18 项、资源存储 8 项；SHA 一致，清理记录通过。
  本地复核目录 `build/ci-368bbed-evidence/`、`build/ci-368bbed-widgets/`；不替代四账号人工桌面走查。
- 同一提交的 PR 运行 [36745424459](https://github.com/devzyz/Chat/actions/runs/36745424459)
  再次暴露 Windows 会话驱动间歇性失败：登录后的 `create` 在约四秒没有控制响应，进程及管道仍正常。
  本机完整场景并发 20 次、最小登录/建聊/退出 300 次均未复现，不能据此认定已修复。
- 诊断提交 `e86a3ba` 增加请求到达断言和存储错误固定分类；不输出请求、响应、路径或凭据。
  临时真实存储线程延迟探针复现相同四秒超时（`build/driver-delayed-red.txt`），
  证明驱动在本地存储初始化完成前报告就绪存在缺口，但不单独证明远端当次延迟的具体来源。

- 会话驱动现在等待当前账号 `directoryRestored` 后才报告 `authenticated`；开库失败返回失败并允许重登。
  认证与恢复共用十秒总期限，建聊仍为原四秒控制响应窗口；生产协议和存储 API 不变。
  正式回归 `build/driver-storage-ready-red.log` 证明旧代码在真实开库失败后误报成功，修复后会话套件 5/5 通过。
  临时真实 worker 探针 `build/driver-delayed-green.txt`、`build/driver-stop-pending-green.txt`、
  `build/driver-deadline-green.txt` 分别验证 4.5 秒恢复后建聊、等待中停止、超过十秒失败后重登，均通过；
  探针未加入生产入口。完整 Qt CTest 66/66 通过（`build/client-readiness-all.log`），测试注册与增量规范通过。最终候选提交远端 CI 仍需复核，不将一次重跑绿灯当作间歇失败根因的证明。

- 候选 `fa2a134` 的 Windows Qt 远端报告 66/66、0 失败/跳过；完整运行
  [36808745816](https://github.com/devzyz/Chat/actions/runs/36808745816) 的 Linux 编译链接通过，
  四账号群聊/三类资源 14/14 通过，但在第四个验收客户端 `released` 退出时失败，后续恢复用例缺失。
  `build/ci-fa2a134-evidence/` 的清理为失败，不能记为完整真实环境通过。
- 退出路径存在协议停止 ACK 后立即 SIGTERM 的竞态。现在仅已确认协议退出的 Qt 客户端先等待自然退出五秒；
  超时再用剩余十秒清理，并继续判失败。普通服务保持原主动停止逻辑，异常退出和升级终止仍拒绝通过。
  监督器替身回归在旧实现产生 `exit-143`（`build/ci-stop-red.log`），修复后相关 9/9，
  含证据合同的本地回归 27/27（`build/client-shutdown-contracts.log`），0 失败/跳过。
  历史 CI 未保留具体退出码，因此不能把模拟的 143 当作历史事实；本轮增加固定分类诊断，仍待真实重跑确认。

### 本轮验证

- 新提醒控件回归：修复前 `build/attention-red.txt` 失败，修复后 `build/attention-widget-green.txt` 通过。
- 私聊/群提醒存储和 SQLite 4 升级：`build/attention-store-green.txt` 通过。
- 分页补拉回归：`build/sync-page-red.txt` 复现已提交页未通知刷新，`build/sync-page-green.txt` 通过。
- 后台提醒控件回归：`build/attention-background-ctest.log` 通过。
- 临时账号清理：`build/session-cleanup-red.txt` 复现目录残留，`build/session-cleanup-green.txt` 通过。
- `RunClientTests -Configuration Release` 构建生产 Qt 客户端并通过 67/67 CTest（24 Unit、15 Component、28 Integration），
  日志 `build/basic-chat-client-tests-final.log`，报告 `build/test-results/client_*.xml`；新增提醒进入默认 Component 入口。
- 完整 Qt 资源/群管理/本地查找控件套件 19/19，0 失败/跳过：`build/basic-chat-widgets-final.xml`、
  `build/basic-chat-widgets-final.txt`。资源 HTTP 使用既有 `ResourceTests.exe` 隔离服务，非本轮完整生产服务重跑。
- 增量规范零违规、测试注册检查、控件证据校验器等 Node 回归 5/5 通过；`build/basic-chat-conventions-final.log`、
  `build/basic-chat-evidence-final.log`。远端 CI 以本任务 PR 的实际运行结果为准，不借用此前 PR 的通过结果。

### 下一步与边界

基础账号、好友、私聊、群管理、附件、历史和重登恢复均已有实现及自动验证。
当前接受 20 人群、2 秒群补拉、手动配置地址及单账号单在线会话；性能与架构专项另行安排。
人工桌面走查按用户要求后置到主体功能完成后：登录/加好友/私聊/群聊/附件/断网重登/历史/切换账号。
本轮以自动 Qt 控件、存储、协议和 CI 验证提交；不要求用户现在操作，也不把自动结果冒充人工跨机器验收。
此前本机 VMware/依赖恢复问题不作为新增架构任务；本轮未访问或迁移个人服务数据库。

<a id="server-maintenance-20261003"></a>

## 服务端修复前的维护与性能证据快照（2026-10-03 归档）

以下为当时状态，分支、待办和测试数量不代表当前结论。

### 项目当前状态

更新：2026-10-02。目标是先完成基础聊天功能及必要正确性，再安排性能优化。
本页只维护当前状态、下一步与证据边界；此前实施过程、失败复现和修复日志见
[开发历史](../history/DevelopmentHistory.md#basic-chat-20261002)。
历史中的分支、测试数量和待办仅代表记录时点。

#### 已合并功能基线

`develop` 为 `37d6c5e`，[PR #28](https://github.com/devzyz/Chat/pull/28) 已于 2026-10-02 合并，
包含此前 PR #25～#27 的提醒、输入与搜索修复、CI 精简，以及本轮草稿、资料与好友管理。

| 范围 | 已实现与边界 |
| --- | --- |
| 账号与好友 | 注册、登录、重置密码、头像、本人资料、备注、好友申请/接受/拒绝/双向删除/重新添加、退出及切换账号 |
| 私聊与消息可靠性 | 文字、图片/视频/文件、UUID 幂等、持久化 outbox、送达/已读、离线补拉和重登恢复；自动重连未实现 |
| 群管理 | 建群、增删成员、退出、转让、改名/解散、代次与历史边界、群资源授权；20 人群、2 秒补拉，无群已读回执 |
| 桌面交互 | 本地历史/搜索、未读提醒、最新消息摘要排序、消息复制、按会话保留草稿/附件/光标/撤销、统一退出确认 |
| 存储与部署 | MySQL 007、SQLite 6；升级先备份并按 [Data](../Data.md#operational-entry) 显式迁移，同步部署 Gate/Chat/Resource |

跨进程草稿、系统通知、拉黑、撤回、通话、多设备和性能专项未实现，不属于已完成基础功能的承诺。
当前使用手动服务地址和单账号单在线会话；本轮没有迁移个人数据库或恢复本机依赖。

#### 基线验证边界

- [PR #28 快速 CI](https://github.com/devzyz/Chat/actions/runs/36930239519) 已通过 Windows 静态、Server、Qt、Varify 与回归汇总；Linux/完整真实环境未执行。
- [合并后快速 CI](https://github.com/devzyz/Chat/actions/runs/36971377967) 已通过；它验证基线提交，不代表本轮尚未提交的维护修改。
- 最近功能轮本地 Qt 74 项、Server 258 项、脚本 13 项通过；真实 MySQL/双 Chat TCP、Qt/SQLite 重启、资源流程与迁移有独立日志。
  Redis/Status 或认证替身的边界见 [历史证据](../history/DevelopmentHistory.md#basic-chat-20261002)，不能当作全生产组合验收。
- PR #24 有 Windows/Linux/真实服务 47 项及 Qt 控件、资源存储的历史全量证据；不能替代最新代码的全量验收。
- 当前默认报告注册为 14 份/399 项，唯一数量来源为 `scripts/windows-local.ps1`。
  报告审计、实际执行和远端 CI 分开记录；人工四账号桌面验收尚未完成。

#### 当前维护：冗余与文档复核

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

#### 下一步

1. 完成本轮维护 PR 的 CI 与评审，再确认最新合并版本的完整真实环境验收。
2. 主体功能完成后进行四账号桌面走查：登录、好友、私聊/群聊、附件、断网重登、历史与账号切换。
3. 首版交付前核实迁移、安装和发布流程；当前未发布 GitHub Release，master 仍是旧发布基线，保留该长期分支。
4. 后续按 [能力缺口](../plans/CapabilityGaps.md) 安排认证、实例失效和恢复演练，按
   [工程简化](../plans/EngineeringSimplification.md) 逐步处理大文件与源码文本耦合；本轮不改消息/权限保护语义。

#### 当前性能测试实施

新增 GitHub 手动 performance 模式及 smoke/baseline/stress 档位，入口见
[性能测试](../../tests/services/performance/README.md)。代码基于已合并 PR #29；后续 Qt UI 提交
4bde00d 已静态核对，原分支保留，不混入本次服务端实现。

远端框架合同与 Release 构建通过；[完整 smoke](https://github.com/devzyz/Chat/actions/runs/37012646624)
七个正式场景通过，但离线预热首次 HTTP 登录失败（9/10），整场按合同判失败。
Gate 在接受连接前创建六十秒期限，空闲超过期限后首个请求可能立即被关闭；这是静态确认的
高可信根因，尚未做独立缺陷复现，不在本轮修改生产实现。
[baseline](https://github.com/devzyz/Chat/actions/runs/37013592301) 已在相同提交 a192754 完成三轮，
21 个正式结果中 20 个通过，清理通过。第 1 轮离线预热及正式恢复各发生一次 TCP 断连，
与 smoke 的 HTTP 失败分开记录，整场判失败。完整指标与证据限制见 [测试报告](../../测试.md)。
同提交的[普通 PR 回归](https://github.com/devzyz/Chat/actions/runs/37012645572)全部通过。
后续应修复 Gate 期限并定位离线登录断连；当前性能证据不能标记为通过或用于容量承诺。
本地只执行语法、格式检查，未启动业务服务、恢复依赖或运行压测；stress 未执行。


## 服务端修复验收中的故障与诊断（2026-10-03）

PR #30 合并为 `2db7f55` 后，修复分支 `fix/repo/server-lan-readiness` 的真实验收逐层发现问题：

- [第一次全量验收](https://github.com/devzyz/Chat/actions/runs/37030735625)：Redis 端口变化后同名旧租约阻止 ChatServer 重启；Qt 协商新社交协议后吞掉旧好友审批完成信号。分别修复租约接管与完成信号；Qt 用例实际 RED/GREEN。
- [第二次全量验收](https://github.com/devzyz/Chat/actions/runs/37034418526)：依赖恢复 89 项已通过，好友审批由超时变为立即拒绝。确认申请分页缺少接收者 UID 和申请备注，补齐字段并保留服务器身份校验。
- [生产修复全量验收](https://github.com/devzyz/Chat/actions/runs/37036710541)：`4c56bb3` 的常规 401 项、真实依赖及业务 136 项全部通过，清理完整。
- [性能异常](https://github.com/devzyz/Chat/actions/runs/37036752833)：`4c56bb3` 第一轮同服预热 750 个计划中发送 749、完成 748，1 次 `business-1017-1011` 和 1 次未发送；数据库持久化审计、21 个正式场景与清理通过，整场仍失败。旧客户端未保存具体 `commit_error`，不能确定根因，也不能仅由不同运行器的延迟差异归因于基础设施。
- [性能复测](https://github.com/devzyz/Chat/actions/runs/37041172126)：`84c75de` 只增加安全错误分类，原 baseline 三轮及预热全部通过。此前 `16d8d0a`、`0a371b8` 也各有通过的 baseline；这些结果均不能抹去那次异常或形成容量保证。
- 检查工件发现 C++ 服务滚动日志位于服务名子目录，而原导出器只处理顶层。`a649a23` 补一层目录、总数/大小上限、符号链接排除、截断首行丢弃及新旧令牌脱敏；不改变生产代码与负载。[框架合同与 smoke](https://github.com/devzyz/Chat/actions/runs/37044502611) 通过，工件确实包含正式 C++ 服务子目录日志。

当前结论与后续证据统一见 [Status](../Status.md)。
