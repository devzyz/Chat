# 项目当前状态

更新：2026-10-01。此页维护当前进度、下一步和验证边界。历史计划与报告保留，
不作为本轮已经验收的证据。目标仍为先完成基础功能及必要正确性，再另行安排性能优化。

## 当前工作：局域网聊天首版功能

PR #23 已合入 develop，合并提交 `e0f927d`。合并后的 Windows CI #65 通过。
原工作区 `feat/chat/basic-groups`、HEAD `1d8d85b` 保留不动。
当前在既有 managed worktree 的 `fix/repo/real-acceptance-ci` 修复 CI 和会话驱动就绪合同；
功能基线与验收是否通过分开记录，合并不等于首版全部收口。

| 阶段 | 实现与当前验收边界 |
| --- | --- |
| 1 动态群成员 | 群资料分页、添加/移除、退出、转让、改名/解散；版本和 UUID 幂等、历史边界、重入代次、只读状态、待确认管理命令落盘；自动验证已执行，人工桌面验收待完成 |
| 2 群资源 | 图片/视频/文件复用既有上传、续传、提交与下载；当前成员及入群边界授权；PNG/十秒 AVI/文件分别完成真实 HTTP/TCP 授权、同步和摘要验证；三类 Qt 卡片已测，人工组合仍待验收 |
| 3 联系人与查找 | 个人好友备注、全部本地联系人/群筛选、当前会话本地文字/文件名搜索与分页定位；存储、TCP 及真实控件自动验证已执行；超过一页搜索定位、70 联系人/70 群筛选与备注提示已覆盖 |
| 4 业务收口 | 已扩展并运行所属自动回归；尚未达到完整首版验收完成条件，按需真实服务 CI 已通过一次，Windows 间歇失败修复复核与四账号人工桌面仍待完成 |

依赖保持阶段 1 → 阶段 2/3 → 阶段 4。没有引入配置部署、服务发现、性能专项或认证体系重构。

## 本次合同

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

## CI 修复复核（2026-10-01）

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

## 未完成的验收与下一步

1. 本机完整生产依赖：当前 VMware VM 不在运行，VMware NAT Service 停止；启动服务需要 Windows 管理员。
   尚未访问到虚拟机 Docker MySQL/Redis，未读取或升级个人 `chat` 库。
   网络恢复后先只读核对旧库，再用隔离库、Redis、账号和资源目录运行 Gate、Status、双 Chat、Varify、Resource。
2. 四账号桌面走查仍未执行。固定角色 A 群主、B/C 原成员、D 新成员：
   A 建群并发旧消息 → 添加 D → D 确认旧消息不可见 → 新消息及 PNG/视频/文件 → 移除 D 并重入 →
   转让 B → A 退出 → B 解散。每步核对成员权限和本地历史；穿插离线、重登、超时和本地保存失败，
   再核对备注、筛选、第二页历史定位及独立私聊资源权限。真实隔离账号须在依赖恢复后创建。
3. 完成会话驱动就绪合同修复，并在最终候选提交重跑 Windows 与完整 Linux CI。
   GitHub PR/合并权限和本机 CLI 已可用，原 403 阻塞已解除；`real_acceptance=true` 可直接触发。
   尚未完成四账号桌面验收，不能以普通 Windows CI 或误绿的专项汇总替代完整验收。


自动竞态覆盖添加/移除等待消息事务、管理版本竞争、转让与目标退出竞争；不代表穷尽所有交错时序。
未执行或受阻项不记为通过；当前尚未达到“PR 可合并、首版全部收口”的完成条件。
