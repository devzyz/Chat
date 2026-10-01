# 项目当前状态

更新：2026-10-02。此页维护当前进度、下一步和验证边界。历史计划与报告保留，
不作为本轮已经验收的证据。目标仍为先完成基础功能及必要正确性，再另行安排性能优化。

## 当前实施：基础聊天易用性与好友管理

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
未迁移个人数据库，未恢复或安装依赖。部署前需要按 [Data](Data.md#operational-entry) 备份并显式迁移，
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

## 当前补充：前端输入与搜索正确性修复

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

## 当前工作：基础聊天软件功能收口

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

## 已合并首版合同

- MySQL 006：可转让群主与不可变创建者分离；保存原建群名称和成员，管理操作结果与变更同事务。
  旧群成员迁移为有效、代次 1、边界 0；群主用户不存在、缺群主成员或角色矛盾明确阻止迁移。
- SQLite 4：保留消息、回执、outbox 和游标，升级前快照；群资格与待确认管理命令沿用目录存储。
  旧版本目录、旧代次正文/发送 ACK 不覆盖新状态；离群停止发送、轮询和自动重发，保留历史及不确定状态。
- 1036/1037 群资料、1038/1039 管理、1040/1041 备注；`group_membership_v1` 协商及群发送代次。
  目录每 10 秒完整分页、有效群每 2 秒增量同步，不新增跨服广播。
- 群资源授权按任一合法引用判断；离群不会撤销独立私聊/本人上传权限，不删除文件字节。
- 详细合同见 [Protocol](Protocol.md#基础文字群聊)、[Data](Data.md)、
  [MessageStorage](MessageStorage.md)、[Resources](Resources.md) 和 [测试矩阵](../tests/TEST-CONTRACT-MATRIX.md)。

## 已执行证据

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

## 收口修复与证据边界

- 群资料面板保留拒绝原因；目录落盘立即更新权限；完整同版本成员分页前禁用成员变更。
  原三项 RED 见 `build/panel-red.txt`；真实 TCP 新版本回包自取消的 RED 见
  `build/panel-tcp-red.txt`，修复后 `build/panel-tcp-green.txt` 9 项通过。
- 搜索定位只消费匹配范围的历史回包，避免普通历史刷新抢走定位；切换会话清理旧定位及备注提示意图。
  后者修复前失败见 `build/ui-switch-red.txt`，最终完整控件回归通过。
- 自动控件使用真实 Qt 和 SQLite；备注失败结果及资源下载完成信号由测试驱动。
  三类资源 HTTP/TCP 使用真实 MySQL、生产 Chat/Resource，但 Redis/Status 仍是明确替身。
  上述证据均不能替代完整生产依赖或人工桌面验收。

## 按需真实环境 CI 检查点

- 已实现 `CI` 手动运行参数 `real_acceptance`，默认 `false`；普通提交/PR/定时运行不触发新增专项。
  显式开启时，Windows 与完整 Linux 同提交通过后才产生 `Real environment acceptance` 成功结果。
- 在现有 3D 真实服务编排中加入四个独立 Qt 客户端，使用 Docker MySQL/Redis/Mailpit 和生产
  Gate、Status、双 Chat、Varify、Resource。新增 14 项群管理/历史边界/PNG、十秒视频与文件授权/
  离线重登/原 UUID 重试/备注搜索验收，与原 33 项合计 47 项；报告缺失、失败、跳过、SHA 不符或清理失败均拒绝。
- Qt 自动控件与资源存储另有 18/8 项及截图证据；这些自动证据不等同于人工桌面走查。
  操作入口和产物见 [服务测试入口](../tests/services/README.md)。
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

## PR #24 修复过程记录（以下保留当时的结果）

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

## 本轮验证

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

## 下一步与边界

基础账号、好友、私聊、群管理、附件、历史和重登恢复均已有实现及自动验证。
当前接受 20 人群、2 秒群补拉、手动配置地址及单账号单在线会话；性能与架构专项另行安排。
人工桌面走查按用户要求后置到主体功能完成后：登录/加好友/私聊/群聊/附件/断网重登/历史/切换账号。
本轮以自动 Qt 控件、存储、协议和 CI 验证提交；不要求用户现在操作，也不把自动结果冒充人工跨机器验收。
此前本机 VMware/依赖恢复问题不作为新增架构任务；本轮未访问或迁移个人服务数据库。
