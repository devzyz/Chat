# Phase 3A 正式计划：Windows PR 快速业务回归

状态：**Plan 3A-05 complete（2026-09-02）— 下一项 3A-06**

制定日期：2026-08-30

当前基线：12 份报告、232 个 testcase、四个稳定 Required Checks（173 + 3A-01 的 7 + 3A-02 的 14 + 3A-03 的 10 + 3A-04 的 16 + 3A-05 的 12）

合同决策：`PHASE-3A-DECISIONS.md` DG-05、DG-06、DG-07、DG-08（均 Confirmed）；
`PHASE-3B-RELEASE-DECISIONS.md` DG-25（本机 vcpkg 持久目录不可变，Confirmed）

## 1. 目标

在不访问真实 Redis、MySQL、SMTP 或公网的前提下，为当前仍未保护的 Business / Architecture
行为建立确定性 Unit 和 Component 回归合同。Phase 3A 完成后，`develop` 的绿色结果应额外证明：

1. Chat Logic dispatcher 的 FIFO、容量和 shutdown 合同；
2. Chat session 的单身份替换、条件清理和发送顺序；
3. Gate 注册、登录、重置和验证码编排及依赖失败映射；
4. Status ChatServer 选择和 Token fail-closed；
5. Qt 网络结果到认证流程/页面动作的确定性状态转换。

CI 绿色仍只承诺已登记合同，不承诺未测试行为。人工 UAT、真实 Adapter Integration 和业务 E2E
继续承担遗漏发现和真实环境验收。

## 2. 硬前置 R0：Phase 2.5 远端收尾

Phase 3A 不得在未确认的主线基线上开始实施。

### R0.1 主线运行终态

状态：**Complete（2026-08-30）**。develop push run `33261698599` 的四个 Required Checks
全部成功，包括最终完成的 `Server Release build`。

- 精确检查合并提交 `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e` 的 develop push run
  `33261698599`。
- 四个 Required Checks 必须全部为 `success`：
  - `Static configuration checks`
  - `Server Release build`
  - `Qt client Release`
  - `VarifyServer dependency and package check`
- 若 Server job 失败或超时，先针对原始日志建立独立修复 PR；不得在 Phase 3A 顺手混入修复。

### R0.2 本地收口

状态：**Complete（2026-08-30）**。

- 本地 `develop` 已通过 fast-forward 与 `origin/develop` 同步到
  `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e`。
- PR #1 已合并；topic `a534c542188a114120e741eeeaf3e79e6a84ba58` 与 develop 的 committed tree hash
  均为 `28af8f710cc45832a2014651724cc9ab2d6943bc`，本地与远端 topic 引用已删除。
- `tests/auto/chat-test.md`、`tests/auto/test-strand.md`、`.huorong-quarantine-check/`、
  `ChatGPT-Proxy.ps1` 与决策文件均通过前后 SHA-256/metadata 复核保持不变；staged index 为空。
- develop 保护复核为 strict、管理员受约束、禁止 force-push/deletion，四项 Required Check context
  未漂移；无活动 `gh.exe` 监控进程。

### R0 验收

- post-merge develop CI 四项全绿；
- 本地 `develop` 与 `origin/develop` 一致；
- 用户文件内容和状态未改变；
- Phase 2.5 主题分支已安全清理；
- 无遗留 `gh run watch` 或本任务拥有的构建/测试进程。

## 3. 范围与非范围

### 本阶段范围

| Gap | Phase 3A 交付 | Level |
| --- | --- | --- |
| G-007 | Logic dispatcher 队列与 handler dispatch | Unit |
| G-008 | session identity registry 和 in-process ordered send state | Unit / Component |
| G-009 | Gate request orchestration + in-memory dependency Adapters | Component |
| G-010 | Status selection/token Module + in-memory store Adapter | Unit / Component |
| G-011 | Qt auth/network outcome reducer/coordinator | Unit / Component |

### 明确非范围

- 真实 Redis、MySQL、SMTP 命令、断线、TTL、事务和 schema：Phase 3C；
- Gate HTTP、Chat TCP、Status gRPC 的新增真实进程/transport 组合：Phase 3B；
- 四进程共享状态与完整依赖编排：Phase 3B/3C；
- 双 ChatServer 好友、消息、重连和历史业务流：Phase 3D；
- 当前/上一发布版互操作和 artifact 晋升：Phase 3C/3D / release gate；
- 本地缓存、群聊、文件、语音、音视频和局域网新功能本身；这些 Module 后续必须复用本计划建立的
  测试登记和 session 隔离合同，但不在 Phase 3A 提前实现。

## 4. Module 与 seam 原则

- 每个领域只有一个供生产 caller 和测试共同使用的外部 Interface；测试不得访问私有队列、map 或 flag。
- In-process 逻辑直接通过深 Module Interface 测试；Remote-but-owned 依赖在内部 seam 使用 production Adapter
  与 in-memory Adapter。
- Gate/Status 的 Redis、MySQL、Varify/Status RPC 是真实 port；生产 Adapter 调现有 Manager/client，测试
  Adapter 只表达状态，不复制业务判断。
- 不增加 `clearForTest`、测试编译宏、公开 private helper、测试专用 singleton reset 或第二套 parser/serializer。
- 一个 Module 应隐藏队列同步、依赖调用顺序、错误映射和生命周期复杂度；caller 只提供业务输入并消费稳定结果。
- 既有 `GateResponse`、`GrpcClientRuntime`、`ClientSession`、`TcpFrameDecoder` 合同必须复用，不重新包装或重复测试。

## 5. 计划与波次

实际执行在共享工作树中保持串行，避免多个子会话同时修改 Visual Studio 项目、runner 和矩阵。下表的 wave
表示逻辑依赖，不授权并发写同一工作树。

| Wave | Plan | 目标 | 依赖 |
| --- | --- | --- | --- |
| 0 | 3A-00 | 完成 R0、冻结 DG-05..08、记录基线 | 无 |
| 1 | 3A-01 | Chat Logic dispatcher | 3A-00 |
| 1 | 3A-02 | Status selection/token | 3A-00 |
| 2 | 3A-03 | Chat session registry/send state | 3A-01、3A-00 |
| 2 | 3A-04 | Gate request orchestration | 3A-00 |
| 3 | 3A-05 | Qt auth/network state | 3A-00、G-005 baseline |
| 4 | 3A-06 | 全量报告、结构门禁、PR 与 develop CI 收口 | 3A-01..05 |

### DG-25 本机工具链硬门禁

- `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 默认只读；普通 phase、构建、测试或诊断授权不允许
  restore/install/remove/update/upgrade、manifest 自动安装、目录清理/重建、install root/triplet/baseline/tool identity 变化。
- 当前所需 packages 已由另一次获批操作恢复；3A-02 在一次 phase-entry read-only preflight 通过后即可开始。该 preflight
  只核验固定路径和当前所需依赖，不重复 tool hash、package identity 或 vcpkg fingerprint，也不得触发 restore。
- 在运行任何后续 MSBuild 前，使所有本地 Server build/test 调用显式传递
  `/p:VcpkgManifestInstall=false` 与 `/p:VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\`，并由
  `CheckTestStructure` 保护这条接线。缺包/状态不一致必须稳定非零，不得隐式调用 `RestoreServers`。
- 若未来确需任何 package/path 修改，先向用户展示精确命令、目标、原因、影响与回滚边界并取得本次明确批准；不得切换
  替代 tree、删除整个 tree 后重建或把过去批准复用于新操作。
- GitHub-hosted Windows CI 使用 workflow 自己的 run-owned 临时 install root；它不能访问上述本机目录。以后修改本机或 CI
  install root、manifest、baseline、triplet 或 tool identity，均先取得明确批准并完成 D-04 评审。

### 比例化执行与共享 phase 前置

执行一次上述 DG-25 preflight，并在 phase entry 一次性读取 `tests/CI-GOVERNANCE.md` section 2.1、DG-05..DG-08、DG-25、
G-007..G-011 owner 与当前 baseline。其后每个行为任务只读取 edited source、closest analog 和一个必要
authority：3A-02..05 各行为切片先 focused RED/GREEN，仅对可观察行为/失败传播/lifecycle/gate 做 meaningful mutation；代码
稳定后每个 plan 只运行一次 owning public runner（3A-02..04 为 `RunServerTests`，3A-05 为 `RunClientTests`）。它们不运行
`RunAllTests`，也不重复 aggregate secret/residue/diff/docs audit。3A-06 独占一次 full lane 与 phase/CI closeout 证据。

## 6. Plan 3A-00 — 基线与合同冻结

状态：**Complete（2026-08-30）**。当前下一项为 **Plan 3A-04**；R0.1/R0.2 的
Complete 状态与证据保持不变。

### Read first

- `tests/CI-GOVERNANCE.md`
- `tests/REGRESSION.md`
- `tests/TEST-CONTRACT-MATRIX.md`
- `tests/plans/PHASE-2.5-07-SUMMARY.md`
- `tests/plans/PHASE-3A-DECISIONS.md`
- `.github/workflows/windows-ci.yml`
- `scripts/windows-local.ps1`

### 任务

1. 完成第 2 节 R0，不修改 Phase 3A 生产代码。
2. 在开始 RED 前记录 12-report/173-case 基线、四个 Required Check 名称和用户文件状态。
3. 为每个后续 Module 建立或更新 README，先分配 Test ID、Domain、Level、Interface、timeout、报告和 mutation。
4. 证明现有测试没有已经覆盖目标行为；重复合同应复用或扩展原 Module，不新增近义测试。

### 验收

- R0 全部通过；
- DG-05..08 均为 Confirmed；
- 每个后续 plan 在首个测试提交前拥有 Test Plan；
- 未修改、暂存或提交 `tests/auto/` 及其他用户路径。

### 完成证据（2026-08-30）

- 执行前基线为 `develop == origin/develop == a03d0c30e9a5e90d2de11320bf7c9e492294ed0e`，
  staged index 为空；post-merge push run `33261698599` 在同一 SHA completed/success，四个
  Required Check 精确名称未漂移。
- `scripts/windows-local.ps1` 的 declarative manifest 仍为 12 reports / 173 testcase：Server
  119、Qt 12、VarifyServer 29、PowerShell 13；本 plan 未改 runner、report 或精确计数。
- DG-05..08 均保持 Confirmed。五个后续 Module 的唯一生产 Interface、planned Test ID、
  Domain/Level、依赖/Adapter、timeout、报告、同步清理、RED/GREEN/mutation、剩余 gap 与拟接线
  已冻结在 [`PHASE-3A-TEST-PLAN.md`](PHASE-3A-TEST-PLAN.md)。
- 去重审计确认现有 frame codec、Asio lifecycle、GateResponse、GrpcClientRuntime/pools/clients、
  Qt `TcpFrameDecoder` 与 `ClientSession` reset 均为相邻合同，不替代 G-007..G-011；详见
  [`PHASE-3A-00-SUMMARY.md`](PHASE-3A-00-SUMMARY.md)。
- 新生产 Interface 尚未落地，因此依 `tests/REGRESSION.md` 未创建空 future test directory、
  空工程或占位测试源；矩阵中的 planned ID 明确不计入 173 baseline，也未关闭任何 Gap。
- 低成本 `CheckTestStructure` 通过；未重复运行完整 `RunAllTests`，本阶段复用 R0 的既有
  post-merge CI 作为行为基线证据。

## 7. Plan 3A-01 — Chat Logic dispatcher（G-007）

状态：**Complete（2026-08-31）**。下一项为 **Plan 3A-04**；完成证据见
[`PHASE-3A-01-SUMMARY.md`](PHASE-3A-01-SUMMARY.md)。

### 生产入口

- `ChatServer/ChatServer/LogicSystem.h/.cpp`
- `ChatServer/ChatServer/CSession.cpp::AsyncReadBody`
- `ChatServer/ChatServer/Const.h::MAX_DEALQUE`

### 目标 Module

将队列、worker、condition variable、容量和 stop 隐藏在一个 production-owned dispatcher Module 中。
外部 Interface 只需要：投递一个 Logic message 并返回 `Accepted/Full/Closed`，以及幂等 `Stop()`。
`LogicSystem` 保留业务 handler 注册和依赖调用；`CSession` 通过同一投递 Interface 观察 overflow/closed。

### 必须测试的合同

- T08-LOGIC-01：顺序投递按 FIFO 处理；
- T08-LOGIC-02：多 producer 的每个 accepted node 只处理一次；
- T08-LOGIC-03：恰好 `MAX_DEALQUE` 容量，下一条返回 `Full` 且不破坏已有队列；
- T08-LOGIC-04：空队列等待可被新消息和 stop 唤醒；
- T08-LOGIC-05：shutdown 排空已接受消息并在 2 秒内结束；
- T08-LOGIC-06：关闭后投递立即返回 `Closed`，重复 stop 幂等；
- T08-LOGIC-07：未知 ID 被丢弃，后续有效 ID 仍处理，日志不含 body。

### TDD 与 mutation

- RED：测试先因缺少可观察投递结果/生产 dispatcher Interface 编译或行为失败；
- GREEN：只增加最小生产 Interface，并让 `LogicSystem`/`CSession` 使用它；
- mutation：恢复 off-by-one、停止时 drop 或关闭后接受任一行为，focused 测试必须非零；随后恢复并复跑。

### 验收

- 无 Redis/MySQL/gRPC/真实 socket；
- 无固定 sleep；每个等待有 2 秒硬上限；
- production executable 与测试链接同一 dispatcher 实现；
- `server_unit.xml` 精确增加本计划实际 runner testcase 数，现有 53 个 Unit 不减少。

## 8. Plan 3A-02 — Status selection/token（G-010）

执行状态：**Complete（2026-09-01）**。完成证据见
[`PHASE-3A-02-SUMMARY.md`](PHASE-3A-02-SUMMARY.md)；下一项为 **Plan 3A-04**。

### 生产入口

- `StatusServer/StatusServer/StatusServiceImpl.h/.cpp`
- `StatusServer/StatusServer/RedisMgr.h/.cpp`
- `StatusServer/StatusServer/ConfigMgr.h/.cpp`
- `proto/status.proto`

### 目标 Module

建立一个深的 Status routing Module：caller 提供 UID，Interface 返回稳定 assignment/login 结果；Module 隐藏
连接数选择、Token 生成、Token store 写入/读取和错误映射。内部 store port 有现有 Redis production Adapter
和测试 in-memory Adapter；不把 Adapter 方法暴露给 gRPC caller。

### 必须测试的合同

- T08-STATUS-01..07：空列表、单服务器、最小有效计数、并列 Name、部分未知、全部未知、负数/畸形计数；
- T08-STATUS-08：Token 写入成功后才返回 Success + 非空 endpoint/token；
- T08-STATUS-09：写入 false 时 `RPCFailed` 且 token/host/port 为空；
- T08-STATUS-10：store 抛出异常时同一稳定 fail-closed envelope，无异常文本；
- T08-STATUS-11..13：UID 不存在、Token 不匹配、Token 匹配；
- T08-STATUS-14：并发选择不依赖 `unordered_map` 顺序且无数据竞争。

### TDD 与 mutation

- RED：空列表和 store failure 先证明当前实现会解引用 begin 或继续返回 Success；
- GREEN：gRPC `StatusServiceImpl` 只委托 production routing Interface 并 shape reply；
- mutation：恢复 `begin()->second` 或忽略 Token store 失败，focused 测试必须 RED。
- owner：所有切片 GREEN 且 meaningful mutation 恢复后，只运行一次 `RunServerTests -Configuration Release`。

### 验收

- 不修改 proto；不新增公开错误码；
- 不连接真实 Redis；Token 使用合成 marker 且不得进入 XML/log；
- StatusServer production target 和测试共享同一 Module；
- 测试登记在 Server Unit/Component 所属现有报告，不伪称 Integration。

### 完成证据（2026-09-01）

- `StatusRouting::Assign/Validate` 成为唯一 production Interface；Redis/UUID 与线程安全 in-memory
  Adapters 保持为 internal seam，`StatusServiceImpl` 只委托并 shape 现有 proto。
- T08-STATUS-01..14 focused GREEN；忽略 token store false 的 meaningful mutation 使
  T08-STATUS-09 非零后已恢复。Unit/Component 报告精确增加 8/6 case。
- manifest 更新为 12 reports / 194 testcase；真实 Redis、完整 Status transport 组合、
  `RunAllTests` 与远端 CI 仍分别留给 Phase 3C、3B/3C 和 3A-06。

## 9. Plan 3A-03 — Chat session registry 与发送状态（G-008 的 3A 部分）

执行状态：**Complete（2026-09-01）**。完成证据见
[`PHASE-3A-03-SUMMARY.md`](PHASE-3A-03-SUMMARY.md)；下一项为 **Plan 3A-04**。

### 生产入口

- `ChatServer/ChatServer/CServer.h/.cpp`
- `ChatServer/ChatServer/CSession.h/.cpp`
- `ChatServer/ChatServer/UserMgr.h/.cpp`
- `ChatServer/ChatServer/MsgNode.h/.cpp`
- `ChatServer/ChatServer/Const.h::MAX_SENDQUE`

### 目标 Module

将 UID→current session 身份、session ID→owned session、原子替换和条件删除集中在一个 registry Module。
将 CSession 发送的接受/关闭/FIFO 状态置于 production Interface 后，socket async-write 作为内部 Adapter。
Phase 3A 测试 in-memory writer；Phase 3B 再用真实 loopback socket 验证 transport。

### 必须测试的合同

- T08-SESSION-01：每个新 session ID 非空且唯一；
- T08-SESSION-02：首次注册成为 UID 的 current session；
- T08-SESSION-03：同 UID 新 session 原子替换旧 session；
- T08-SESSION-04：旧 session 清理不删除新映射；
- T08-SESSION-05：current session 条件清理成功且重复清理幂等；
- T08-SESSION-06：并发关闭只触发一次 registry/store cleanup；
- T08-SESSION-07：发送帧保持 FIFO；
- T08-SESSION-08：恰好 `MAX_SENDQUE` 容量，下一帧拒绝且已排队帧不变；
- T08-SESSION-09：关闭后发送拒绝；
- T08-SESSION-10：writer failure 只触发一次 close + matching cleanup。

### TDD 与 mutation

- RED：旧 session 无条件按 UID 删除新映射、send capacity off-by-one 和重复关闭；
- GREEN：CServer/CSession/UserMgr caller 通过一个 registry Interface 协作，不分别复制身份判断；
- mutation：将 matching delete 改为 unconditional erase，focused 测试必须 RED。
- owner：所有切片 GREEN 且 meaningful mutation 恢复后，只运行一次 `RunServerTests -Configuration Release`。

### 验收

- 真实 TCP partial write、read timing、对端断开和进程清理由 Phase 3B 承担；
- in-memory Adapter 用例为 Unit/Component；
- 所有并发等待有硬超时，测试结束无线程、socket 或 io_context 残留；
- 现有 frame、Redis pool 和 gRPC 测试不删除、不降级。

### 完成证据（2026-09-01）

- production-owned `ChatSessionState` 静态库提供 opaque handle 的
  `Create/RegisterCurrent/FindCurrent/Close/Send` 唯一 Interface；`ChatServer` 与
  `ServerComponentTests` 链接同一实现。
- `UserMgr` 持有唯一 registry；`CServer`、`CSession`、`LogicSystem` 与
  `ChatServiceImpl` 统一委托；production writer 复用既有 `async_write`，presence Adapter
  复用 Redis session/IP 清理，测试使用固定 ID、manual writer 和 recorder。
- T08-SESSION-01..10 focused GREEN；matching delete 临时改为 UID unconditional erase 后
  T08-SESSION-04 稳定非零，恢复精确匹配后 10/10 GREEN。Component 报告增加 10 case。
- `CheckTestStructure` GREEN；定向 Release Module/Component/真实 ChatServer build GREEN；唯一有效
  owning `RunServerTests -Configuration Release` GREEN，六份 Server XML 精确为 68/40/34/4/2/2、
  合计 150，全部 zero failure/error。
- manifest 更新为 12 reports / 204 testcase；G-008 仅 3A in-memory 部分完成，真实 TCP
  partial write/peer disconnect 与 Redis presence Integration 仍分别留给 Phase 3B/3C。

## 10. Plan 3A-04 — Gate request orchestration（G-009 的 3A 部分）

### 生产入口

- `GateServer/GateServer/LogicSystem.h/.cpp`
- `GateServer/GateServer/GateResponse.h/.cpp`
- `GateServer/GateServer/VerifyGrpcClient.*`
- `GateServer/GateServer/StatusGrpcClient.*`
- `GateServer/GateServer/RedisMgr.*`
- `GateServer/GateServer/MysqlMgr.*`

### 目标 Module

建立一个 Gate request Module，外部 Interface 接收 `gate::Endpoint + Json request` 并返回 `gate::Result`。
Module 隐藏验证码、code store、user store、Status assignment 的调用顺序和错误映射。LogicSystem 的真实四条
POST route 与 Component 测试调用同一 Interface；`GateResponse` 继续唯一负责 JSON parse/envelope/allowlist。

内部 seam：

- verification port：production `VerifyGrpcClient` + in-memory Adapter；
- code store port：production `RedisMgr` + in-memory Adapter；
- user store port：production `MysqlMgr` + in-memory Adapter；
- status port：production `StatusGrpcClient` + in-memory Adapter。

### 必须测试的合同

- T08-GATE-01..03：验证码缺 email、RPC success、RPC failure；
- T08-GATE-04..08：注册确认密码不一致、验证码过期、验证码错误、用户已存在、成功；
- T08-GATE-09..13：重置验证码过期/错误、用户名邮箱不匹配、更新失败、成功；
- T08-GATE-14..16：登录凭据失败、Status failure、成功 assignment；
- 每个早退 case 同时断言后续 Adapter 未调用；每个 callback/result 只完成一次；
- 复用 T06-GATE response allowlist，不重复其字段/secret 测试。

### TDD 与 mutation

- RED：当前 LogicSystem lambda 直接创建 Singleton 依赖，无法通过生产请求 Interface 注入 Adapter；
- GREEN：route 只做 HTTP→Module 适配，业务顺序只存在于 Module；
- mutation：交换调用顺序、忽略 dependency failure 或 early failure 后仍调用写操作，focused 测试必须 RED。
- owner：所有切片 GREEN 且 meaningful mutation 恢复后，只运行一次 `RunServerTests -Configuration Release`。

### 验收

- 不访问真实 Redis/MySQL/gRPC/SMTP；不宣称 HTTP Integration；
- 不改变 DG-03 response allowlist、公开 error 数值或 proto；
- 合成 password/code/token/email marker 不出现在 XML/log；
- 生产 GateServer 和 Component 测试链接同一 request Module。

### 完成证据（2026-09-01）

- `GateRequest::Handle` 与四个 internal port 已落地，真实 Gate route 通过现有 `GateResponse` callback
  调用同一 production library；T08-GATE-01..16 已进入 `server_component.xml`。
- focused 16/16 GREEN；在注册 code mismatch 后仍调用 user create 的 mutation 使 T08-GATE-06
  稳定非零，精确恢复后 GREEN；定向 Release build 覆盖 GateRequest、ServerComponentTests 与 GateServer。
- runner manifest 保持 12 份报告并增至 220 testcase（Server Component 56、Server 合计 166）。
- 这里只关闭 G-009 的 3A in-process 部分；真实 Gate HTTP 与 Redis/MySQL/gRPC/SMTP Adapter gap 仍开放。

## 11. Plan 3A-05 — Qt auth/network 状态转换（G-011 的剩余 3A 部分）

状态：**Complete（2026-09-02）**。完成证据见 [`PHASE-3A-05-SUMMARY.md`](PHASE-3A-05-SUMMARY.md)；下一项为 **Plan 3A-06**。

### 生产入口

- `chat/httpmgr.h/.cpp`
- `chat/tcpmgr.h/.cpp`
- `chat/logindialog.h/.cpp`
- `chat/registerdialog.h/.cpp`
- `chat/resetdialog.h/.cpp`
- `chat/mainwindow.h/.cpp`
- `chat/clientsession.h/.cpp`

### 目标 Module

建立 production auth-flow coordinator/reducer：Interface 接收 HTTP/TCP/auth outcome，返回有限 UI action
（stay-and-show-error、connect-chat、show-login、show-chat）。网络 manager 和 Dialog 只负责 Adapter 工作；
网络错误、业务错误、非法响应、成功推进和重复结果的状态规则只存在于该 Module。

### 必须测试的合同

- Q03-AUTH-01..03：Register/Reset/Login 的 HTTP network error 路由到正确 flow 且只通知一次；
- Q03-AUTH-04：未知 module/request ID 不触发其他页面状态；
- Q03-AUTH-05：畸形 JSON 保持当前页面并产生稳定错误 action；
- Q03-AUTH-06：业务 error 保持当前页面，不发起 TCP connect；
- Q03-AUTH-07：登录 HTTP success 只产生一次 connect action；
- Q03-AUTH-08：TCP connect failure 保持/返回登录流程，不创建 ChatDialog；
- Q03-AUTH-09：Chat login failure 不进入聊天页；
- Q03-AUTH-10：Chat login success 只进行一次 show-chat transition；
- Q03-AUTH-11：重复或迟到的旧 flow outcome 不重复迁移；
- Q03-AUTH-12：异常断线继续复用 DG-02 `ClientSession::resetSession`，不建立第二套 reset。

### TDD 与 mutation

- RED：从 Dialog/TcpMgr 分散分支中提取当前可观察规则，先证明没有单一生产 Interface；
- GREEN：Qt signals/slots 调用 coordinator；测试通过同一 Module 和 QSignalSpy/QtTest 断言 action；
- mutation：重复 success、迟到旧 response 或 network failure 触发错误页面时，focused 测试必须 RED。
- owner：所有切片 GREEN 且 meaningful mutation 恢复后，只运行一次 `RunClientTests -Configuration Release`。

### 验收

- 无真实 QNetworkAccessManager/QTcpSocket；真实 transport timing 属于 Phase 3B；
- 不重复 G-005 decoder/session-reset cases；
- 使用 `QT_QPA_PLATFORM=minimal`，不依赖像素、显示器或固定 sleep；
- production `chat.exe` 与 Qt Unit/Component 测试链接同一 coordinator Module。

### 完成证据（2026-09-02）

- production-owned `chat_auth_flow` 由真实 `chat.exe`、11-case Unit target 与 1-case Component target 共同链接；HttpMgr/TcpMgr 与 Login/Register/Reset/MainWindow 只形成 outcome 或执行 action。
- Q03-AUTH-01..12 focused 12/12 GREEN；重复 Chat login success mutation 使 Q03-AUTH-10 稳定非零，精确恢复后 12/12 GREEN。
- 定向 Release `chat_auth_flow`、Unit/Component targets 与 `chat.exe` 构建通过；`CheckTestStructure` 通过。
- 唯一有效 owning `RunClientTests -Configuration Release` 通过：`client_unit.xml` 18/18、`client_component.xml` 6/6，Qt 合计 24，manifest 为 12 reports / 232 cases。
- abnormal disconnect 的 production mapping 仍调用既有 `ClientSession::resetSession(UnexpectedDisconnect)`；未复制 Q02 account/session/pending/UI reset 断言。
- 未访问真实 HTTP/TCP、固定端口或凭据；未触碰 vcpkg，真实 transport timing 与完整登录 E2E gap 继续开放。

## 12. Plan 3A-06 — 回归、报告与 CI 收口

### 任务

1. 每个新增 testcase 分配稳定 Test ID，并更新 Module README 与 `TEST-CONTRACT-MATRIX.md`。
2. 将新增源注册到正确 production/test project；在隔离副本执行一次结构 negative probe，证明临时移除任一必要注册或生产接线时
   `CheckTestStructure` 非零，恢复后通过；不为简单 registration 逐项 mutation。
3. 更新 `scripts/windows-local.ps1` 的每报告精确 testcase 数和总数；保持 12 个报告，不新建伪 Integration 报告。
4. 复用 3A-02..05 已取得的 focused RED/GREEN 与每-plan owning runner 证据；本 plan 只运行一次完整
   `RunAllTests -Configuration Release`，并以该次结果作为 phase final local evidence。
5. 精确审计 12 个 XML：总数与 manifest 一致、zero failure/error、无合成 secret marker。
6. 检查本任务进程/线程/端口/临时目录残留；不删除预存开发环境、build、vcpkg tree 或 node_modules。
7. 若修改 workflow，必须先使用 actionlint 1.7.12；保留 vcpkg baseline/tool identity 分离、
   accepted native nonzero 清零和 Qt `windeployqt` 自动检测约束。
8. 通过 topic branch + PR 提交；四个 Required Checks 全绿后合并，并确认 post-merge develop push CI 全绿。
9. 复核全部本地 MSBuild/runner 都显式禁用 vcpkg manifest 自动安装并使用 DG-25 固定 installed tree；不得通过真实 package
   mutation 制造 RED。任何 package 写入仍需逐次批准。

### 完成定义

- G-007～G-011 的 Phase 3A 部分在矩阵中标记 complete；Phase 3B/3C 剩余范围仍明确；
- 当前 232 个 testcase 全部保留，除非先完成 D-04 评审；
- 新总数在 runner manifest、Module README、矩阵、Summary 和 12 份报告中一致；
- Server、Qt、VarifyServer、PowerShell public runner 均 GREEN；
- full local regression 和 clean PR CI 均 GREEN；
- 四个 Required Check 名称不漂移；
- post-merge develop CI GREEN 后才将 Phase 3A 标记完成；
- 无 staging/commit 包含 `tests/auto/`、`.planning/` 旧 handoff、隔离目录、proxy、build 或凭据。

## 13. 风险与停止条件

- 任一目标合同仍有歧义：停止该 plan，新增 DG 决策，不由 executor 猜测。
- 为测试需要暴露 private container、增加 test-only switch 或复制生产算法：停止并重新设计 Module seam。
- focused test 只能依赖真实个人服务才能运行：停止，移到 Phase 3B/3C 并设计 disposable dependency。
- 同一失败原因连续出现三次且无新证据：写 handoff/checkpoint，不循环重试。
- 长构建超过既有硬上限：精确停止本次拥有的进程，记录未验证范围，不删除 build/cache。
- pinned vcpkg 路径缺失、安装状态不一致、出现隐式 manifest install 或需要任一 package/path 变更：按 DG-25 立即停止，
  只报告精确待批准命令与目标；不得自动 restore、改目录或删除重建。
- 发现现有合同缺陷：保留正确 RED，按 D-04 报告；不得降低断言迁就实现。

## 14. 后续阶段路由

以下四份 `status: Planned` 的正式计划是 Phase 3A 的 canonical downstream execution routes；
`PHASE-3B-RELEASE-PLAN-OUTLINE.md` 继续保留为来源大纲，不能替代这些正式计划。这里的链接只冻结
后续 owner 与依赖，不表示 Phase 3A、3B、3C、3D 或 Release gate 已完成，也不改变当前 baseline/count。

- [Phase 3B 正式计划](PHASE-3B-PLAN.md)：§7..12 / 3B-00..3B-05，负责真实 Gate HTTP、
  Chat TCP、Status gRPC、Qt HTTP/TCP loopback/process Integration，以及 G-008..G-011、G-015
  的 3B transport/process owner；
- [Phase 3C 正式计划](PHASE-3C-PLAN.md)：§9..18 / 3C-00..3C-09，负责 hosted-Ubuntu
  same-source build、disposable Redis/MySQL/Mailpit、schema/migration、四进程真实依赖与适用兼容，
  对应 G-012..G-015 及 G-016/G-017 的 3C owner；
- [Phase 3D 正式计划](PHASE-3D-PLAN.md)：§9..14 / 3D-00..3D-05，负责双 ChatServer、
  双 production-module client 的跨实例好友/聊天/重连/历史 E2E，以及 G-016/G-017 的 3D owner；
- [Release gate 正式计划](PHASE-RELEASE-PLAN.md)：§9..12 / R-00..R-03，负责同一不可变
  Windows x64 artifact 的 build-once、artifact-only smoke、版本化 UAT、same-bytes promotion 与
  G-017/G-018 的 release owner。
