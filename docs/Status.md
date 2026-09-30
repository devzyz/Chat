# 项目当前状态

更新：2026-09-30。此页维护当前进度、下一步和验证边界。历史计划与报告保留，
不作为本轮已经验收的证据。目标仍为先完成基础功能及必要正确性，再另行安排性能优化。

## 当前工作：局域网聊天首版功能

实现分支 `feat/repo/chat-features` 位于独立 managed worktree，基于远端 develop
`f3d6bd683624100ef22080e1f63f48c4e8c84c7a`（PR #22 基础群聊）。
原工作区 `feat/chat/basic-groups`、HEAD `1d8d85b` 保留不动；原 HEAD 已被 develop 包含，
两者文件树相同，原群聊实现已合入。功能提交 `252b078`（服务端）及 `cde9b88`（客户端）已推送；尚未创建 PR 或合并。

| 阶段 | 实现与当前验收边界 |
| --- | --- |
| 1 动态群成员 | 群资料分页、添加/移除、退出、转让、改名/解散；版本和 UUID 幂等、历史边界、重入代次、只读状态、待确认管理命令落盘；自动验证已执行，人工桌面验收待完成 |
| 2 群资源 | 图片/视频/文件复用既有上传、续传、提交与下载；当前成员及入群边界授权；PNG/十秒 AVI/文件分别完成真实 HTTP/TCP 授权、同步和摘要验证；三类 Qt 卡片已测，人工组合仍待验收 |
| 3 联系人与查找 | 个人好友备注、全部本地联系人/群筛选、当前会话本地文字/文件名搜索与分页定位；存储、TCP 及真实控件自动验证已执行；超过一页搜索定位、70 联系人/70 群筛选与备注提示已覆盖 |
| 4 业务收口 | 已扩展并运行所属自动回归；尚未达到完整首版验收完成条件，四账号人工桌面与完整生产依赖环境仍待完成 |

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
- 新增 47 项真实依赖流程尚未在 GitHub 运行；首次远端构建、运行时间预算及业务组合仍须实际验证。
  强制群成员多页响应、群管理 ACK 丢失的真实环境组合仍不由这 14 项单独证明；已有局部回归与人工缺口继续分别记录。

## 未完成的验收与下一步

1. 本机完整生产依赖：当前 VMware VM 不在运行，VMware NAT Service 停止；启动服务需要 Windows 管理员。
   尚未访问到虚拟机 Docker MySQL/Redis，未读取或升级个人 `chat` 库。
   网络恢复后先只读核对旧库，再用隔离库、Redis、账号和资源目录运行 Gate、Status、双 Chat、Varify、Resource。
2. 四账号桌面走查仍未执行。固定角色 A 群主、B/C 原成员、D 新成员：
   A 建群并发旧消息 → 添加 D → D 确认旧消息不可见 → 新消息及 PNG/视频/文件 → 移除 D 并重入 →
   转让 B → A 退出 → B 解散。每步核对成员权限和本地历史；穿插离线、重登、超时和本地保存失败，
   再核对备注、筛选、第二页历史定位及独立私聊资源权限。真实隔离账号须在依赖恢复后创建。
3. 创建 develop 草稿 PR，核对同提交 Windows 与完整 Linux CI 及产物。
   分支已推送，但 GitHub 连接器创建 PR 返回 403 `Resource not accessible by integration`；当前没有新 PR 或 CI 运行。
   完整 CI 需 workflow_dispatch（refresh_tools=false、cold_linux=false）；当前 GitHub 内置浏览器尚未登录，
   连接器没有工作流触发入口。不得以仅 Windows PR CI 代替完整 CI。

自动竞态覆盖添加/移除等待消息事务、管理版本竞争、转让与目标退出竞争；不代表穷尽所有交错时序。
未执行或受阻项不记为通过；当前尚未达到“PR 可合并、首版全部收口”的完成条件。
