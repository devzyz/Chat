# Phase 3A 测试合同冻结

状态：**3A-01..05 complete；下一项 3A-06**

本文件是在五个新生产 Interface 尚不存在时的最小登记位置。依据
`tests/REGRESSION.md`，此时不创建空测试目录、空测试工程或占位测试源；每个后续 plan
先落地生产 Interface，再创建其所属 Module 目录和 README，并把本文件的登记迁移到该
README。3A-01 的 7 个、3A-02 的 14 个、3A-03 的 10 个、3A-04 的 16 个、3A-05 的 12 个 Test ID 已进入所属报告；
当前 baseline 为 12-report/232-case；
G-007/G-010 complete，G-008/G-009/G-011 的 Phase 3A in-process/outcome 部分 complete；真实
TCP/HTTP/Redis/MySQL/gRPC/SMTP transport、Adapter 与 E2E 层仍 open。

所有剩余计划受 DG-25 约束：`D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 默认只读；普通构建/测试
必须显式关闭 MSBuild manifest 自动安装并使用固定 installed tree。当前 required packages 已恢复；3A-02 在一次只读
phase-entry preflight 通过后可执行。未来缺包只形成 fail-closed blocker，不能触发自动 restore、替代目录或删除重建。
执行深度、每-plan owning runner 和一次性 closeout 遵循 `tests/CI-GOVERNANCE.md` section 2.1。

## 共同 seam 约束

- 每个目标 Module 只有一个供生产 caller 与测试共同调用的外部 Interface。Interface 包括
  方法、结果、不变量、错误、顺序、生命周期与 timeout；不得把 private queue、map、flag
  或 internal seam 暴露成测试 helper。
- 只有确实存在 production 与 in-memory 两个 Adapter 时才建立 port。In-process 逻辑直接
  测生产 Interface，不为 mock 便利增加浅 port。
- 禁止 `clearForTest`、测试编译宏、测试专用 singleton reset、公开 private helper，以及复制
  parser、serializer、选择、排序或错误映射算法。
- 新测试创建后必须由真实 MSBuild/CMake target 显式登记并链接同一 production library；
  不把生产 `.cpp` 复制成第二份漂移实现。
- 所有异步验证使用 latch、condition variable、promise/future、Qt signal 或等价事件同步；
  不使用固定 sleep。每个等待有硬上限，teardown 必须释放本例创建的线程、队列、socket、
  QObject、临时状态和 in-memory Adapter 数据。

## 3A-01 — Chat Logic dispatcher（G-007）

Planned Test ID range：`T08-LOGIC-01..07`。

### 唯一生产 Interface

拟新增 `ChatServer/ChatServer/LogicDispatcher.h`，外部 Interface 冻结为：

- 构造时接收生产 handler callback；`Submit(LogicMessage)` 返回
  `Accepted | Full | Closed`；`Stop()` 同步、幂等。
- `Accepted` 消息按接受顺序全局 FIFO，且每个只 dispatch 一次；最多同时接受
  `MAX_DEALQUE` 条待处理消息，第 `MAX_DEALQUE + 1` 条返回 `Full`，不得覆盖旧消息。
- 未知 message ID 安全丢弃且不含 body/凭据地记录诊断，不阻塞后续已接受消息。
- `Stop()` 原子停止新接受、唤醒空队列 worker、排空已接受消息，并在调用方 2 秒硬上限内
  完成；关闭后 `Submit` 立即返回 `Closed`，析构等价于幂等 `Stop()`。
- caller 和测试只能观察返回值、handler 收到的消息与 Stop 完成；queue、worker、condition
  variable、callback map 均为 Implementation。

依赖分类：队列、同步与 handler dispatch 均为 In-process，不建立 port。生产 caller 是
`LogicSystem` 的 handler registry，测试使用同一构造 callback 记录可观察 dispatch；后者不是
测试专用 Interface。

### Planned contracts

| Test ID | Domain | Level | 合同 |
| --- | --- | --- | --- |
| T08-LOGIC-01 | Architecture | Unit | 顺序提交按 FIFO 处理 |
| T08-LOGIC-02 | Architecture | Unit | 多 producer 的每个 accepted node 只处理一次 |
| T08-LOGIC-03 | Architecture | Unit | 精确容量与 `Full`，旧队列不受损 |
| T08-LOGIC-04 | Architecture | Unit | 空队列等待由新消息或 stop 事件唤醒 |
| T08-LOGIC-05 | Architecture | Unit | shutdown 排空并在 2 秒内结束 |
| T08-LOGIC-06 | Architecture | Unit | 关闭后 `Closed`，重复 stop 幂等 |
| T08-LOGIC-07 | Architecture | Unit | 未知 ID 不阻断后续有效 ID，日志不含 body |

报告归属：`server_unit.xml`，实际 7 个 runner testcase；manifest 从 53 增至 60 Unit，
全仓从 173 增至 180，报告数保持 12。
拟接线：新增 shared production static-library project，由 `ChatServer.vcxproj` 与
`ServerUnitTests.vcxproj` 共同 `ProjectReference`；`CSession::AsyncReadBody` 只通过
`Submit` 观察 overflow/closed，`LogicSystem` 保留业务 handler 注册。

确定性与清理：barrier 同时释放 producers；handler latch 控制消费以精确填满容量；所有
future 等待不超过 2 秒；每例显式 `Stop()` 并证明 worker 已 join。RED 是生产 Interface 缺失；
GREEN 仅增加该 Module 并接入两个 caller。Mutation 必须分别恢复 `size() > MAX_DEALQUE`、
stop 丢队列、关闭后仍接受，focused test 均应非零。

剩余 gap：真实 `CSession` socket 读取、断开/取消时序属于 3B；业务 handler 的 Redis/MySQL/
gRPC 组合及双 ChatServer 流属于 3C/3D。不得重复 T02-FRM、F03-ASIO 或 T08-RUNTIME。

## 3A-02 — Status routing/token（G-010）

Implemented Test ID range：`T08-STATUS-01..14`。

### 唯一生产 Interface

`StatusServer/StatusServer/StatusRouting.h` 的外部 Interface 为
`Assign(uid) -> AssignmentResult` 与 `Validate(uid, token) -> LoginResult` 两个操作：

- Module 隐藏 server-count 读取、确定性选择、token 生成、token store 写/读与公开错误映射。
- 非负十进制 count 为有效；missing、empty、负数或 malformed 为 unknown，并排在所有有效
  count 后。最小有效 count 优先；并列或全 unknown 按运行时 `Name` 字典序；空列表失败。
- `Assign` 只在非空 token 成功写入 store 后返回 `Success` 和非空 endpoint/token。store
  返回 false 或抛异常时 fail closed 为既有 `RPCFailed`，token/host/port 全空，不外泄异常文本。
- `Validate` 稳定区分 UID 不存在、token 不匹配与成功；异常同样 fail closed。
- Interface 不修改 proto 或公开错误码。Module 自身不无限等待；production Redis Adapter 的
  每个调用必须采用其 production finite policy，focused 测试本地 2 秒硬上限。

依赖分类与 Adapter：选择/错误映射为 In-process。`StatusStore` 是 remote-but-owned port，
production Adapter 调现有 `RedisMgr`，test Adapter 为线程安全 in-memory store；它同时提供
count read 与 token put/get。token source 是 internal local-substitutable seam，production
Adapter 生成 UUID，test Adapter 提供确定序列。两者都不出现在 gRPC caller Interface。

### Implemented contracts

| Test ID | Domain | Level | 合同 |
| --- | --- | --- | --- |
| T08-STATUS-01..07 | Business/Architecture | Unit | 空列表、单 server、最小有效 count、并列 Name、部分/全部 unknown、负数/畸形 count |
| T08-STATUS-08 | Business | Component | token store 成功后才返回完整成功 assignment |
| T08-STATUS-09 | Business | Component | store false 映射 `RPCFailed` 且清空公开字段 |
| T08-STATUS-10 | Business | Component | store exception 同一 fail-closed envelope 且无异常文本 |
| T08-STATUS-11..13 | Business | Component | UID 不存在、token 不匹配、token 匹配 |
| T08-STATUS-14 | Architecture | Unit | 并发选择不依赖 `unordered_map` 顺序且无竞态 |

报告归属：01..07、14 为 `server_unit.xml` 的 8 个实际 case；08..13 为 `server_component.xml`
的 6 个实际 case。shared `StatusRouting` production library 由 `StatusServer.vcxproj`、
`ServerUnitTests.vcxproj`、`ServerComponentTests.vcxproj` 共同引用；gRPC
`StatusServiceImpl` 只调用该 Interface 并 shape 现有 proto reply。

确定性与清理：固定 server fixture、确定 token source、barrier 并发；in-memory Adapter
作用域清理，无 Redis。RED 用空列表和 store failure 暴露当前 `begin()->second`/忽略写结果；
GREEN 只新增 Module、两个真实 Adapter 和 caller wiring。Mutation 恢复直接 `begin()` 或忽略
token write false，focused test 必须 RED。

剩余 gap：真实 Redis command/断线/TTL/清理为 3C；真实 gRPC transport 已有 client 相邻合同，
新增 Status process 业务组合属 3B/3C。不得重复 GrpcClientRuntime/pool/deadline tests。

## 3A-03 — Chat session registry/send state（G-008 的 3A 部分，complete）

Implemented Test ID range：`T08-SESSION-01..10`。

### 唯一生产 Interface

`ChatServer/ChatServer/ChatSessionState.h` 已落地。外部 Interface 以 opaque session handle 表达
`Create/RegisterCurrent/FindCurrent/Close/Send`，不得暴露 map、queue 或 close flag：

- 每个新 session ID 非空且唯一；同 UID 同时只有一个 current session，新认证原子替换旧值。
- `Close(handle)` 只在 UID 当前仍指向该 session ID 时删除映射；旧 session 清理不得删新映射。
  cleanup 与重复/并发 close 幂等，对外 store cleanup 最多一次。
- `Send(handle, frame)` 返回 `Accepted | Full | Closed`；每 session FIFO，最多接受
  `MAX_SENDQUE` 条，下一条拒绝且不覆盖；关闭后立即 `Closed`。
- writer success 后推进下一帧；writer failure 只触发一次 close 与 matching cleanup。
- 所有状态转换同步返回；真实异步 writer 的完成 callback 必须 exactly once。测试等待上限 2 秒。

依赖分类与 Adapter：registry/send state 为 In-process。`SessionWriter` 是真实 port：production
Adapter 包装 `boost::asio::async_write`，test Adapter 为可控 in-memory writer。presence cleanup
是 remote-but-owned port：production Adapter 包装现有 Redis session/IP 清理，test Adapter 为
in-memory recorder。两个 port 均有 production + test Adapter，业务匹配判断只在 Module 中。

### Implemented contracts

| Test ID | Domain | Level | 合同 |
| --- | --- | --- | --- |
| T08-SESSION-01..05 | Architecture | Component | ID 唯一、首次 current、原子替换、旧清理不删新映射、current/重复清理 |
| T08-SESSION-06 | Architecture | Component | 并发 close 最多一次 registry/store cleanup |
| T08-SESSION-07..09 | Architecture | Component | 发送 FIFO、精确容量、关闭后拒绝 |
| T08-SESSION-10 | Architecture | Component | writer failure 一次 close + matching cleanup |

报告归属：`server_component.xml`，实际新增 10 个 runner testcase，使 Component 为 40、Server 为
150、全 manifest 为 12-report/204-case。shared production library 由 `ChatServer.vcxproj` 与
`ServerComponentTests.vcxproj` 共同引用；`CServer`、`CSession`、`LogicSystem`、`ChatServiceImpl`
和 `UserMgr` caller 统一委托该 Interface，现有真实 socket 与 Redis manager 只作为 Adapter。

确定性与清理：固定 ID source、barrier 并发关闭、in-memory writer 手动完成 callback；每例
2 秒上限，作用域结束前关闭全部 handles，断言无 pending callback/thread。RED 覆盖旧 session
unconditional erase、容量 off-by-one、重复 close；GREEN 只接入单一 Module。Mutation 把
matching delete 改成 UID unconditional erase，focused test 必须 RED。

剩余 gap：TCP partial write、peer disconnect、真实 socket/read timing 与进程级清理由 3B；
真实 Redis presence command/lock/TTL 由 3C。不得重复 `ChatFrameCodec`、Redis pool lifecycle、
gRPC 或 Qt `ClientSession` 合同。

## 3A-04 — Gate request orchestration（G-009 的 3A 部分）

Implemented Test ID range：`T08-GATE-01..16`。

### 唯一生产 Interface

`GateServer/GateServer/GateRequest.h` 的外部 Interface 为
`Handle(gate::Endpoint, const Json::Value&) -> gate::Result`：

- Module 是四个 POST 业务顺序和依赖错误映射的唯一所有者；HTTP route 只做 transport，现有
  `gate::HandleJsonRequest` 继续唯一拥有 JSON parse、exception boundary、response envelope 与
  allowlist，二者不得合并成第二套 parser/shaper。
- 验证码请求要求 email 后调用 verification；注册依次 password confirmation、code read/
  compare、user create；重置依次 code read/compare、username-email match、password update；
  登录依次 credential check、status assignment。
- 每个 early failure 必须停止后续 Adapter；dependency false/错误/异常稳定映射现有 ErrorCodes；
  每次 `Handle` 只返回一次结果，不回显 secret 或内部异常。
- Module 不自建线程；remote production Adapter 复用现有有限 gRPC deadline，数据库/store 调用
  服从 production finite policy；focused Component 每例 2 秒硬上限。

依赖分类与 Adapter：四个 remote-but-owned port 均为真实 seam：verification production
`VerifyGrpcClient` + in-memory Adapter；code store production `RedisMgr` + in-memory Adapter；
user store production `MysqlMgr` + in-memory Adapter；status production `StatusGrpcClient` +
in-memory Adapter。测试 Adapter 只表达返回值、异常和调用记录，不复制业务判断。

### Implemented contracts

| Test ID | Domain | Level | 合同 |
| --- | --- | --- | --- |
| T08-GATE-01..03 | Business | Component | 验证码缺 email、RPC success、RPC failure |
| T08-GATE-04..08 | Business | Component | 注册 confirmation/code expired/code mismatch/user exists/success |
| T08-GATE-09..13 | Business | Component | 重置 code expired/mismatch、身份不匹配、update failure、success |
| T08-GATE-14..16 | Business | Component | 登录 credential failure、status failure、success assignment |

每例同时断言 early-return 后续 Adapter 未调用以及 result exactly once。报告归属：
`server_component.xml` 的 16 个实际 case，使 Component 为 56、Server 为 166、全 manifest 为
12-report/220-case。shared `GateRequest` production library 由
`GateServer.vcxproj` 与 `ServerComponentTests.vcxproj` 共同引用；Gate routes 通过现有
`GateResponse` callback 调用同一 request Interface；production Adapter 继续复用
`GateGrpcClients.vcxproj`。

确定性与清理：in-memory Adapter 记录有序调用，数据只用 synthetic marker，作用域清理且
无 Redis/MySQL/gRPC/SMTP；无固定 sleep。真实 RED 先证明 shared production library 未实现，
随后逐行为暴露默认失败和未调用 Adapter；GREEN 只移动业务编排并保留 GateResponse。
Mutation 在注册 code mismatch 后继续 user create，使 T08-GATE-06 稳定 RED；精确恢复后 16/16 GREEN。

剩余 gap：真实 Gate HTTP transport 组合为 3B；真实 Redis/MySQL/Varify/Status/SMTP 为 3C；
完整注册/登录 E2E 为 3C/3D。T06-GATE allowlist 与 T08-GATE-RPC/GrpcClientRuntime 原样复用，
不新增近义字段、secret、deadline 或 transport tests。

## 3A-05 — Qt auth/network outcome coordinator（G-011 的 3A 部分，complete）

Planned Test ID range：`Q03-AUTH-01..12`。

### 唯一生产 Interface

拟新增 `chat/authflowcoordinator.h/.cpp`。外部 Interface 冻结为
`Reduce(flowId, AuthOutcome) -> AuthAction`；有限 action 仅为
`StayAndShowError | ConnectChat | ShowLogin | ShowChat`：

- outcome 覆盖 Register/Reset/Login HTTP network result、JSON/business result、TCP connect、
  Chat login 与 abnormal disconnect。未知 module/request、重复结果及旧 flowId 不改变当前
  flow，也不重复 action。
- network/JSON/business failure 留在对应 flow 并只产生一次稳定错误 action；Login HTTP
  success 只产生一次 `ConnectChat`；TCP failure 不创建 ChatDialog；Chat login failure 不
  `ShowChat`；success 只迁移一次。
- coordinator 隐藏 flow generation/state/dedup；caller 和测试只提交 outcome、消费 action。
  它不解析 frame，也不清理 account/session 内部状态。
- Module 无阻塞和 timer；HTTP/TCP timeout 由 transport Adapter 形成 outcome。CTest 每例硬
  timeout 10 秒，测试事件循环只等明确 Qt signal，不用固定 sleep。
- abnormal disconnect 的 production action 必须调用既有
  `ClientSession::resetSession(UnexpectedDisconnect)`；不得建立第二套 reset。

依赖分类与 Adapter：reducer/state 为 In-process。外部 seam 的 production Adapter 是现有
`HttpMgr`/`TcpMgr` outcome signals 与 Login/Register/Reset/MainWindow action executor；test
Adapter 直接驱动 synthetic outcome 并用返回值/QSignalSpy 捕获 action。二者共用同一
Interface；不另设 test helper port。真实 QNetworkAccessManager/QTcpSocket timing 留给 3B。

### Implemented contracts

| Test ID | Domain | Level | 合同 |
| --- | --- | --- | --- |
| Q03-AUTH-01..03 | Business | Unit | Register/Reset/Login HTTP network error 路由到正确 flow 且只通知一次 |
| Q03-AUTH-04 | Architecture | Unit | 未知 module/request 不改变其他页面状态 |
| Q03-AUTH-05 | Business | Unit | 畸形 JSON 留在当前页面并产生稳定 error action |
| Q03-AUTH-06 | Business | Unit | business error 不发起 TCP connect |
| Q03-AUTH-07 | Business | Unit | Login HTTP success 只产生一次 connect action |
| Q03-AUTH-08 | Business | Unit | TCP connect failure 不创建 chat 页面 |
| Q03-AUTH-09 | Business | Unit | Chat login failure 不进入聊天页 |
| Q03-AUTH-10 | Business | Unit | Chat login success 只迁移一次 |
| Q03-AUTH-11 | Architecture | Unit | 重复/迟到旧 flow outcome 不重复迁移 |
| Q03-AUTH-12 | Architecture/Business | Component | abnormal disconnect wiring 继续委托既有 ClientSession reset |

报告归属：01..11 写入 `client_unit.xml`，12 写入 `client_component.xml`；实际为 11 个 Unit 与 1 个 Component CTest。`chat_auth_flow` static library 由 production `chat`、
`auth_flow_tests` 与 `auth_flow_component_tests` 共同链接；`chat/tests/auth-flow/` README、
非空测试源及 CMake add_executable/add_test/Level/TIMEOUT 均已显式登记。

确定性与清理：单调 flowId、直接 outcome、QSignalSpy/event-loop signal；QObject parent 作用域
清理，禁止真实 socket、固定端口、显示器或 sleep，使用 `QT_QPA_PLATFORM=minimal`。RED 是
当前分散分支无单一 Interface；GREEN 让 manager/dialog 只适配 outcome/action。Mutation 允许
重复 success、接受旧 flow 或把 network failure 导向错误页面，focused test 必须 RED。

剩余 gap：真实 QNetworkAccessManager/QTcpSocket loopback 与 transport timing 为 3B；完整
登录 E2E 为 3C/3D。T07-FRM 与 Q02-SESSION 保留为相邻合同，不重复 decoder、pending batch、
账号状态清理或 owned UI 销毁断言。

## 首个 RED 前迁移检查表

3A phase entry 只执行一次 DG-25 fixed-path 只读 preflight，确认 runner 已关闭 manifest 自动安装；3A-01..05 已完成迁移：
生产 Interface 文件落地；把本节迁入真实 Module README；
创建非空测试源；生产与测试 target 链接同一 library；登记 Test ID、Level、报告与硬 timeout；
记录真实 focused RED/GREEN，并只对本节列出的可观察行为、失败传播或 lifecycle 做 mutation；代码稳定后运行一次
owning public runner。3A-04 按其执行授权运行 `CheckTestStructure`；单一结构 negative probe 与 phase-level 聚合仍由 3A-06 执行。
上述 3A-05 项均已完成并进入 12-report/232-case baseline；真实 transport/E2E gap 仍按后续计划开放。
