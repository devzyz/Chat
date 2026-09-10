# 当前自动测试合同矩阵

Phase 3C-01 adds `T10-BLD-01..10` under CTest label
`phase3c-build-ownership`, reported in `linux_build_ownership.xml`.
The [process module contract](server/process-harness/README.md#linux-ownership-and-process-contracts-3c-01)
owns exact standalone/full selector registration, real-process boundaries and
timeouts. These Linux contracts supplement, not replace, the Windows baseline;
registration alone is not PASS evidence.

状态：Phase 3B Plan 3B-05 本地 closeout 基线清单；远端 clean PR / post-merge develop evidence pending
基线日期：2026-09-06
治理规则：[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md)

## 1. 统计口径

当前公开 runner 注册 342 个 testcase：Server 226、Qt 49、VarifyServer 54、PowerShell 13。
3C-04/06 在既有两份 Varify 报告新增 25 项（Unit +15、Integration +10），
3C-05 新增 1 项 Server session principal Component、2 项 Qt pending/账号 Component 和
1 项 Qt authenticated retry Integration，计数从 338 增至 342。
报告集合仍为 13 份；注册数量不代表本轮执行了全量门禁。下方 313 等计数保留阶段历史。

Test ID 表示被保护的 Interface 合同，runner testcase 表示测试框架实际报告的用例。二者不必一一对应：例如 Qt `network_state_tests` 是一个 runner testcase，但同时保护拆分 header、拆分 body 和相邻 frame 三个合同。覆盖率、testcase 数量和合同数量必须分别报告，不能相互替代。

上述 342 个 testcase 属于 `develop` 快速门禁；`master` 和 release 继承它们。
新增 hosted Redis/SMTP、SchemaMigration 和 MessageCommit 报告单独统计，不计入这 342 项；
真实数据库迁移/事务的执行证据与四服务业务 E2E 分开，注册不代表 hosted 验收完成。

Plan 2.5-07 的历史本地证据为 12 份报告 / 173 testcase。Plan 3A-01 在同一报告集合中新增
7 个 `LogicDispatcherTests` runner testcase；focused 7/7 与完整 `server_unit.xml` 60/60
均已通过，manifest 因此增至 12 份报告 / 180 testcase。Plan 3A-02 又在同一报告集合中增加
8 个 Unit 与 6 个 Component case，当前 manifest
因此为 12 份报告 / 194 testcase。Plan 3A-03 再增加 10 个 Chat session Component case，当前 manifest
为 12 份报告 / 204 testcase。Plan 3A-04 再增加 16 个 Gate request Component case，当前 manifest
为 12 份报告 / 220 testcase。Plan 3A-05 再增加 11 个 Qt Unit 与 1 个 Qt Component case，当前 manifest
为 12 份报告 / 232 testcase。Plan 3B-00 在既有 `server_integration.xml` 中增加 6 个 T09-HOST contract case，manifest 因此增至 12 份报告 / 238 testcase。Plan 3B-01 再增加 12 个 T09-PROC contract case，manifest 为 12 份报告 / 250 testcase。Plan 3B-02 增加 7 个 T09-GHTTP 与 10 个 Q04-HTTP case，并首次生成 `client_integration.xml`，manifest 为 13 份报告 / 267 testcase。Plan 3B-03 再增加 12 个 T09-SGRPC case，manifest 为 13 份报告 / 279 testcase。Plan 3B-04 增加 16 个 T09-CTCP 与 12 个 Q04-TCP case，manifest 为 13 份报告 / 307 testcase。Plan 3B-05 增加 6 个 actual T09-COMP case，本地 manifest 为 13 份报告 / 313 testcase。完整 `RunAllTests`、clean PR、远端 artifact 和 `develop`
branch protection 仍必须由 3A-06/实际 GitHub check 证明。

## 2. Suite 与报告

| Suite | Module | Domain | Current Level | Testcases | Report | 当前依赖 | Lane |
| --- | --- | --- | --- | ---: | --- | --- | --- |
| Server unit | config、messaging、transport、protocol、rpc、Chat lifecycle/logic dispatcher、Status selection | Foundation/Architecture/Business | Unit | 68 | `server_unit.xml` | in-process；临时文件；旧 wire fixture | develop required |
| Gate Asio | Gate lifecycle | Foundation | Unit | 2 | `server_gate_unit.xml` | in-process thread/io_context | develop required |
| Status Asio | Status lifecycle | Foundation | Unit | 2 | `server_status_unit.xml` | in-process thread/io_context | develop required |
| Server component | Chat Redis pool/session state/principal、Gate response/request、Status token/store | Foundation/Architecture/Business | Component | 57 | `server_component.xml` | in-process fake；不连接 Redis/MySQL/Status/Varify | develop required |
| Server integration | Chat/Gate/Status startup、C++→Node Varify、Gate gRPC clients、IntegrationHost composition、run-owned process harness、Gate Beast HTTP、Status gRPC、Chat TCP、formal production composition | Architecture | Integration | 93 | `server_integration.xml` | 受控子进程、动态 loopback 端口、run-owned temp、in-memory Adapter；Chat real-ready 留在 G-015/3C | develop required |
| Chat gRPC integration | Chat production gRPC clients | Architecture | Integration | 4 | `server_chat_grpc_integration.xml` | 动态 loopback 端口、无外部服务 | develop required |
| Qt unit | frame decoder、message model rules Q01-MODEL-01..06、auth outcomes Q03-AUTH-01..11 | Foundation/Architecture/Business | Unit | 18 | `client_unit.xml` | Qt Core/Widgets minimal，无 socket | develop required |
| Qt component | message store/delegate Q01-MODEL-07..08、session reset/pending Q02-SESSION-01..08、auth reset wiring Q03-AUTH-12 | Architecture/Business | Component | 8 | `client_component.xml` | Qt Widgets/Network minimal；真实 in-process Module，无连接 | develop required |
| Qt HTTP integration | production GateHttpTransport Q04-HTTP-01..10 | Architecture/Business | Integration | 10 | `client_integration.xml` | Qt QNetworkAccessManager、动态 numeric loopback、无公网 | develop required |
| Qt TCP integration | production ChatTcpTransport Q04-TCP-01..12 | Architecture/Business | Integration | 12 | `client_integration.xml` | real QTcpSocket、动态 numeric loopback、无公网 | develop required |
| Qt authenticated retry integration | session reset/retry Q02-SESSION-09 | Architecture/Business | Integration | 1 | `client_integration.xml` | real QTcpSocket、动态 loopback；认证后保留原 UUID 重试 | develop required |
| Varify unit | protocol、handler、fake startup、Redis/SMTP adapters | Foundation/Business/Architecture | Unit | 33 | `varify_unit.xml` | in-process fake；descriptor/fixture | develop required |
| Varify integration | config、loopback RPC、process startup、Redis/SMTP bounded faults | Foundation/Architecture | Integration | 21 | `varify_integration.xml` | 子进程或动态 loopback | develop required |
| Script component | instance validation | Architecture | Component | 9 | `script_component.xml` | 临时目录、占位进程 | develop required |
| Script integration | instance lifecycle | Architecture | Integration | 4 | `script_integration.xml` | 受控子进程/PID identity | develop required |
| **合计** |  |  |  | **342** | 13 份报告 | 无个人服务或凭据；保留 12/232 floor |  |

PowerShell 报告逐项输出 13 个 testcase：validation 9 个、lifecycle 4 个；控制台同步保留逐 Test ID 的 PASS/FAIL。该报告粒度改进不改变本表的逻辑基线数量。

## 3. Server testcase 目录（226）

### 3.1 Chat ConfigMgr（17）

Interface：显式配置路径、发布示例配置、必填字段、端口和 peer 校验。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase / 参数 | 合同 |
| --- | --- | --- |
| F01-CFG-01 | `ExplicitPathOverridesChatConfigEnvironmentVariable` | 显式路径高于 `CHAT_CONFIG` |
| F01-CFG-02 | `ShippedDualInstanceConfigurationsHaveDistinctIdentities` | 两份发布配置实例、TCP、RPC 和日志身份不同 |
| F01-CFG-03 | `EmptyPeerListAllowsSingleInstanceConfiguration` | 单实例允许空 peer 列表 |
| F01-CFG-04 | `MissingConfigFileIsRejected` | 缺失文件被拒绝 |
| F01-CFG-05 | `MissingRequiredSectionIsRejected` | 缺失必需 section 被拒绝并带字段诊断 |
| F01-CFG-06 | `MissingRequiredKeyIsRejected` | 缺失必需 key 被拒绝 |
| F01-CFG-07 | `InvalidPorts/.../0`（空字符串参数） | 空 TCP 端口被拒绝 |
| F01-CFG-08 | `InvalidPorts/.../1`（非数字参数） | 非数字端口被拒绝 |
| F01-CFG-09 | `InvalidPorts/.../2`（`-1`） | 负端口被拒绝 |
| F01-CFG-10 | `InvalidPorts/.../3`（`0`） | 端口 0 被拒绝 |
| F01-CFG-11 | `InvalidPorts/.../4`（`65536`） | 超过 65535 被拒绝 |
| F01-CFG-12 | `InvalidPorts/.../5`（尾随字符） | 非规范十进制端口被拒绝 |
| F01-CFG-13 | `EqualTcpAndRpcPortsAreRejected` | 同实例 TCP/RPC 端口不能相同 |
| F01-CFG-14 | `ReferencedPeerSectionMustExist` | 被引用 peer section 必须存在 |
| F01-CFG-15 | `SelfReferencePeerIsRejected` | peer 不得引用自身实例名 |
| F01-CFG-16 | `DuplicatePeerNamesAreRejectedAcrossSections` | 多 section 的运行时 peer 名不能重复 |
| F01-CFG-17 | `EmptyPeerEntryIsRejected` | peer 列表中的空项被拒绝 |

### 3.2 Message nodes（8）

Interface：buffer 所有权、初始化、清理、长度和二进制内容。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| F02-MSG-01 | `MsgNodeTests.ConstructorCreatesZeroedNullTerminatedBuffer` | 普通 buffer 零初始化并带终止符 |
| F02-MSG-02 | `MsgNodeTests.ZeroLengthNodeStillProvidesTerminator` | 零长度仍提供安全终止符 |
| F02-MSG-03 | `MsgNodeTests.ClearResetsProgressAndEntirePayloadBuffer` | `Clear` 重置进度和完整 payload |
| F02-MSG-04 | `SendNodeTests.StringConstructorPreservesEmbeddedNullBytes` | 嵌入 NUL 的二进制内容无损 |
| F02-MSG-05 | `SendNodeTests.MaximumApplicationBodyLengthIsCopiedWithoutTruncation` | 最大合法 body 不截断 |
| F02-MSG-06 | `SendNodeTests.OversizedApplicationBodyIsRejectedBeforeAllocation` | 超长 body 在分配前拒绝 |
| F02-MSG-07 | `SendNodeTests.DeclaredLengthCannotExceedTheSourceString` | 声明长度不能超过源数据 |
| F02-MSG-08 | `RecvNodeTests.ConstructorRetainsMessageIdentityAndClearReusesBuffer` | 接收节点保留消息 ID 且可安全复用 |

### 3.3 Chat frame codec（3）

Interface：生产 frame header 的网络字节序和长度校验。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T02-FRM-01 | `ValidatedHeaderPreservesHighBitMessageIdInNetworkOrder` | 高位 message ID 网络序无损 |
| T02-FRM-02 | `MaximumBodyLengthAcceptsAnUnknownMessageId` | 最大合法 body 与未知 ID 可完成 header 解码 |
| T02-FRM-03 | `BodyLengthAboveTheReceiveBufferLimitIsRejected` | 超过接收上限的 body 被拒绝 |

### 3.4 protobuf（3）

Interface：项目使用的文本聊天与验证码 wire 字段。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| F02-PROTO-01 | `TextChatRequestRoundTripsAllRoutingAndMessageFields` | 路由、重复消息、UTF-8 和嵌入 NUL round-trip |
| F02-PROTO-02 | `VerifyResponsePreservesErrorEmailAndCode` | 验证码响应错误、邮箱和 code round-trip |
| F02-PROTO-03 | `CurrentCppConsumerParsesInitialWireFixtureWithUnknownFields` | 当前 C++ 解析迁移前固定 payload；unknown field 不破坏公开 email |

### 3.5 Asio lifecycle（6）

Interface：三份现有 `AsioIOServicePool` 的任务执行、重复停止和析构。
Domain/Level：Foundation / Unit。

| Test ID | Target/report | Runner testcase | 合同 |
| --- | --- | --- | --- |
| F03-ASIO-01 | Chat / `server_unit.xml` | `PostedTaskCompletesBeforeScopedDestruction` | 已提交任务在作用域析构前完成 |
| F03-ASIO-02 | Chat / `server_unit.xml` | `RepeatedStopAndDestructionCompleteWithinTheDeadline` | 重复 stop 与析构在两秒内完成 |
| T03-GATE-01 | Gate / `server_gate_unit.xml` | 同一 contract testcase 1 | Gate 任务执行合同 |
| T03-GATE-02 | Gate / `server_gate_unit.xml` | 同一 contract testcase 2 | Gate 重复停止/析构合同 |
| T03-STATUS-01 | Status / `server_status_unit.xml` | 同一 contract testcase 1 | Status 任务执行合同 |
| T03-STATUS-02 | Status / `server_status_unit.xml` | 同一 contract testcase 2 | Status 重复停止/析构合同 |

### 3.6 Chat Logic dispatcher（7）

Interface：production `LogicDispatcher::Submit/Stop`；`LogicSystem` 复用同一 Interface 保留 handler registry，`CSession` 观察 `Accepted/Full/Closed`。
Domain/Level：Architecture / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T08-LOGIC-01 | `AcceptedMessagesDispatchInFifoOrder` | accepted message 按全局 FIFO dispatch |
| T08-LOGIC-02 | `ConcurrentProducersDispatchEveryAcceptedMessageExactlyOnce` | 多 producer accepted node 不丢失、不重复 |
| T08-LOGIC-03 | `ExactPendingCapacityRejectsOnlyTheNextMessage` | 精确 `MAX_DEALQUE` pending 容量，下一条 `Full` 且不破坏旧队列 |
| T08-LOGIC-04 | `WaitingWorkerWakesForMessageAndStop` | 空闲 worker 由 message 或 stop 唤醒 |
| T08-LOGIC-05 | `StopDrainsAcceptedMessagesWithinTwoSeconds` | Stop 排空 accepted message 并在两秒内结束 |
| T08-LOGIC-06 | `ClosedDispatcherRejectsImmediatelyAndRepeatedStopIsIdempotent` | 关闭后立即 `Closed`，重复 Stop 幂等 |
| T08-LOGIC-07 | `UnknownIdDoesNotBlockValidMessageAndLogOmitsBody` | 未知 ID 不阻断后续有效消息且日志不含 body |

### 3.7 Status routing/token（14）

Interface：production `StatusRouting::Assign/Validate`；`StatusServiceImpl` 只 shape 现有 proto reply。
Domain/Level：Business/Architecture；01..07/14 为 Unit，08..13 为 Component。
报告：Unit 写入 `server_unit.xml`，Component 写入 `server_component.xml`；无真实 Redis。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T08-STATUS-01..07 | `EmptyServerListFailsClosed` 等 7 case | 空列表、单 server、最小有效 count、Name tie-break、部分/全部 unknown、负数/畸形 count |
| T08-STATUS-08 | `SuccessfulStoreProducesCompleteAssignment` | token 成功持久化后才返回完整 assignment |
| T08-STATUS-09 | `StoreFalseFailsClosedAndClearsAssignment` | store false 映射 `RPCFailed` 并清空公开字段 |
| T08-STATUS-10 | `StoreExceptionUsesStableFailClosedEnvelope` | store exception 使用同一 fail-closed envelope |
| T08-STATUS-11 | `MissingUidIsDistinctFromMismatch` | UID 不存在映射 `UidInvalid` |
| T08-STATUS-12 | `TokenMismatchIsRejected` | Token 不匹配映射 `TokenInvalid` |
| T08-STATUS-13 | `MatchingTokenSucceeds` | Token 匹配返回成功 UID/token |
| T08-STATUS-14 | `ConcurrentSelectionIsDeterministic` | barrier 并发选择确定且无 container-order 依赖 |

### 3.8 Chat session registry/send state（11）

Interface：production `ChatSessionState::Create/RegisterCurrent/FindCurrent/AuthenticatedUid/Close/Send`；唯一外部身份为 opaque session handle。`UserMgr` 持有唯一 registry，`CServer`、`CSession`、`LogicSystem` 和 `ChatServiceImpl` 统一委托该 Interface。
Domain/Level：Architecture / Component。
报告：`server_component.xml`。固定 session ID source、手工完成 writer callback 与 presence recorder 均为 in-process Adapter；不连接真实 TCP/Redis。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T08-SESSION-01 | `NewSessionHandlesAreNonEmptyAndUnique` | session ID 非空且唯一 |
| T08-SESSION-02 | `FirstRegistrationBecomesTheCurrentSession` | 首次认证建立 UID current mapping |
| T08-SESSION-03 | `ANewSessionAtomicallyReplacesTheCurrentSession` | 同 UID 新认证原子替换 current session |
| T08-SESSION-04 | `ClosingTheReplacedSessionDoesNotDeleteTheNewMapping` | 旧 session Close 不删除 replacement 或触发错误 cleanup |
| T08-SESSION-05 | `ClosingTheCurrentSessionCleansUpOnceAndIsIdempotent` | 当前 session 重复 Close 幂等且 presence cleanup 一次 |
| T08-SESSION-06 | `ConcurrentClosePerformsPresenceCleanupAtMostOnce` | barrier 并发 Close 最多一次 cleanup |
| T08-SESSION-07 | `AcceptedFramesAreWrittenInFifoOrder` | accepted frame 依次启动 writer，保持每 session FIFO |
| T08-SESSION-08 | `ExactCapacityRejectsOnlyTheNextFrameWithoutOverwriting` | 精确接受 `MAX_SENDQUE`，下一帧 `Full` 且不覆盖 |
| T08-SESSION-09 | `ClosedSessionRejectsNewFramesImmediately` | 关闭后 Send 立即 `Closed` |
| T08-SESSION-10 | `WriterFailureClosesAndCleansUpExactlyOnce` | writer failure exactly-once Close 与 matching cleanup |
| T08-SESSION-11 | `ChatSessionPrincipal.OnlyCurrentLiveOwnedHandleAuthenticates` | 仅本 registry 当前 live handle 产生认证 UID；被替换/已关闭/外部 handle 不认证，关闭后不可重新注册 |

### 3.9 Gate request orchestration（16）

Interface：production `GateRequest::Handle(Endpoint, Json::Value)`；四条 POST route 经既有 `GateResponse` callback 调用同一 Module。
Domain/Level：Business / Component。
报告：`server_component.xml`。verification、code store、user store、status 四个 port 使用 scoped in-memory Adapter；不连接真实 Redis/MySQL/gRPC/SMTP。

| Test ID | Runner testcase(s) | 合同 |
| --- | --- | --- |
| T08-GATE-01..03 | `VerificationWithoutEmailFailsBeforeAdapters` 等 3 case | 缺 email、verification success/failure；早退不调用后续 Adapter |
| T08-GATE-04..08 | `RegistrationPasswordMismatchStopsBeforeCodeRead` 等 5 case | confirmation、code read/compare、user create 严格有序且失败即停止 |
| T08-GATE-09..13 | `ResetExpiredCodeStopsBeforeIdentityCheck` 等 5 case | code read/compare、identity match、password update 严格有序且 fail closed |
| T08-GATE-14..16 | `LoginCredentialFailureStopsBeforeStatusAssignment` 等 3 case | credentials 在 status assignment 前；失败清空 assignment，成功返回完整 assignment |

### 3.10 Peer routing（2）

Interface：配置 peer section 到运行时名称和 endpoint 的映射。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T05-ROUTE-01 | `ConfiguredSectionsResolveToRuntimeNamesAndAddresses` | 多个 section 映射到运行时名称和地址 |
| T05-ROUTE-02 | `MissingRuntimeNameDoesNotCreateARoute` | 缺少运行时名称时不产生不可用 route |

### 3.11 gRPC client runtime、真实连接池与远端 Adapter（26）

Interface：生产 `GrpcClientRuntime`、Gate/Chat 的四类实际连接池，以及 Gate/Chat 生产 gRPC client public Interface。
Domain：Foundation/Architecture。Unit 报告为 `server_unit.xml`；Gate Integration 为 `server_integration.xml`；Chat Integration 为 `server_chat_grpc_integration.xml`。

| Test ID | Level / runner testcase(s) | 合同 |
| --- | --- | --- |
| T08-RUNTIME-01..04 | Unit / `BoundedGrpcPoolTests.*` | 有限等待、归还复用、close 唤醒、重复 close/关闭后归还 |
| T08-RUNTIME-05 | Unit / `ClassifiesStableGrpcFailureCategories` | PoolExhausted、Closed、DeadlineExceeded、Unavailable、Cancelled 稳定分类 |
| T08-RUNTIME-06 | Unit / `DurationUsesDefaultOrValidatesConfiguredRange` | 缺省值与 100..60000 ms 配置边界 |
| T08-GATE-POOL-01..06 | Unit / `GateVarifyPoolContractTests.*`、`GateStatusPoolContractTests.*` | 两类实际 Gate pool 的借还、耗尽、关闭生命周期 |
| T08-CHAT-POOL-01..06 | Unit / `ChatStatusPoolContractTests.*`、`ChatPeerPoolContractTests.*` | 两类实际 Chat pool 的借还、耗尽、关闭生命周期 |
| T08-GATE-RPC-01..04 | Integration / `GateGrpcClientIntegrationTests.*` | 动态 loopback success、deadline、unavailable、peer shutdown 均走生产客户端；失败公开映射 `RPCFailed` |
| T08-CHAT-RPC-01..04 | Integration / `ChatGrpcClientIntegrationTests.*` | 动态 loopback Status/Chat success、deadline、unavailable、peer shutdown；有限清理且无公网 |

### 3.12 Redis pool without service（3）

Interface：Chat `RedisConnectionPool` 的有限借用和关闭。
Domain/Level：Foundation / Component。
报告：`server_component.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T04-RDS-01 | `CloseWakesAWaitingBorrowerWithoutAServiceConnection` | close 唤醒有限等待 borrower |
| T04-RDS-02 | `CloseIsIdempotentAndFutureBorrowsFailImmediately` | close 幂等，关闭后借用立即失败 |
| T04-RDS-03 | `ExhaustedBorrowReturnsWhenItsFiniteWaitExpires` | 耗尽借用在生产超时后返回 |

### 3.13 Gate response allowlist（21）

Interface：生产 `gate::HandleJsonRequest` 对四个公开 POST 端点的 JSON 解析、精确 allowlist、稳定错误 envelope 和异常收口。
Domain/Level：Architecture / Component。
报告：`server_component.xml`。依赖仅为真实生产 shaping Module 和 in-process callback Adapter，不连接 Redis、MySQL、Status 或 Varify。

| Test ID | Runner testcase(s) | 合同 |
| --- | --- | --- |
| T06-GATE-01 | `NonLoginResponseTest.*GetVarifyCode`（2） | 验证码 success/failure 均仅 `error` |
| T06-GATE-02 | `NonLoginResponseTest.*UserRegister`（2） | 注册 success/failure 均仅 `error` |
| T06-GATE-03 | `NonLoginResponseTest.*ResetPassword`（2） | 重置 success/failure 均仅 `error` |
| T06-GATE-04 | `GateLoginResponseTest.*`（3） | 登录 success 精确 `error/uid/token/host/port`；业务/RPC failure 仅 `error` |
| T06-GATE-05 | `AllEndpointResponseTest.MalformedJsonReturnsTheStableJsonError/*`（4） | 四端点 JSON 失败稳定映射 `Error_Json` 且不调用业务 Adapter |
| T06-GATE-06 | `AllEndpointResponseTest.ForbiddenRequestFieldsAndValuesAreNeverReflected/*`（4） | 请求秘密、PII、禁止字段名和值均不反射 |
| T06-GATE-07 | `AllEndpointResponseTest.InternalExceptionReturnsStableErrorWithoutLoggingDetails/*`（4） | 四端点内部异常稳定映射 `RPCFailed`，响应/捕获日志无异常文本或 marker |

### 3.14 ChatServer startup process（8）

Interface：CLI/config 来源、TCP/gRPC bind 失败及资源释放。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| S01-CLI-01 | `MissingConfigValueReturnsNonZeroWithUsage` | `--config` 缺路径非零退出并打印用法 |
| S01-CLI-02 | `UnknownArgumentReturnsNonZeroWithUsage` | 未知参数非零退出并打印用法 |
| S01-CFG-01 | `ExplicitConfigPathOverridesEnvironment` | CLI 配置路径优先 |
| S01-CFG-02 | `EnvironmentConfigPathOverridesWorkingDirectoryDefault` | 环境配置高于工作目录默认值 |
| S01-CFG-03 | `WorkingDirectoryConfigIsTheDefault` | 无覆盖时使用工作目录配置 |
| S01-GRPC-CFG-01 | `OutOfRangeGrpcTimeoutReturnsNonZeroBeforeListening` | 超过 60000 ms 的 gRPC deadline 在监听前拒绝 |
| S01-BIND-01 | `OccupiedTcpPortFailsBeforeExternalDependencies` | TCP bind 冲突在外部依赖前失败 |
| S01-BIND-02 | `OccupiedGrpcPortFailureReleasesTcpPort` | gRPC bind 失败后释放已绑定 TCP 端口 |

### 3.15 C++ → Node Varify loopback（1）

Interface：当前生成的 C++ `VarifyService::Stub` 与 Node 生产 `createServer` 的真实 gRPC wire 互操作。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T05-GRPC-02 | `CrossLanguageProtocolTests.CppClientCallsNodeVarifyOnDynamicLoopbackPort` | `127.0.0.1:0` 动态端口、2 秒 client deadline、所有结果清理 Node process/server/pipe |

### 3.16 Gate/Status startup process（21）

Interface：真实 Gate/Status EXE 的 CLI/config 优先级、fail-fast、本地协议 ready、bind/start 失败和 Windows 等价 graceful stop。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`。Gate 11 case、Status 10 case；外部 endpoint 仅为不可用 loopback 占位。

| Test ID | Runner testcase / 服务 | 合同 |
| --- | --- | --- |
| S02-CLI-01 / S03-CLI-01 | `MissingConfigValueReturnsNonZeroWithUsage` / Gate、Status | `--config` 缺值显示对应 EXE usage 并非零退出 |
| S02-CLI-02 / S03-CLI-02 | `UnknownArgumentReturnsNonZeroWithUsage` / Gate、Status | 未知参数显示 usage 并非零退出 |
| S02-CFG-01 / S03-CFG-01 | `MissingConfigFileReturnsNonZero` / Gate、Status | 显式缺失配置文件 fail-fast，并保留非敏感路径诊断 |
| S02-CFG-02 / S03-CFG-02 | `InvalidServicePortReturnsNonZero` / Gate、Status | 非规范/越界 listener 端口在 bind 前拒绝 |
| S02-CFG-03 / S03-CFG-03 | `MissingCriticalEndpointReturnsNonZero` / Gate、Status | 缺少关键 Status endpoint 在外部访问前拒绝 |
| S02-CFG-04 / S03-CFG-04 | `ExplicitConfigPathOverridesEnvironment` / Gate、Status | CLI 配置高于 `CHAT_CONFIG` |
| S02-CFG-05 / S03-CFG-05 | `EnvironmentConfigPathOverridesWorkingDirectoryDefault` / Gate、Status | `CHAT_CONFIG` 高于 cwd 默认值 |
| S02-CFG-06 / S03-CFG-06 | `WorkingDirectoryConfigIsTheDefault` / Gate、Status | 无覆盖时选择 cwd `config.ini` |
| S02-GRPC-CFG-01 | `OutOfRangeGrpcTimeoutReturnsNonZeroBeforeListening` / Gate | 小于 100 ms 的 pool acquire timeout 在监听前拒绝 |
| S02-BIND-01 / S03-BIND-01 | `OccupiedPortFailsWithoutProtocolReadyAndReleasesOwnership` / Gate、Status | HTTP bind 或 gRPC `BuildAndStart` 失败不 ready、非零退出、端口可回收 |
| S02-LIFE-01 / S03-LIFE-01 | `ProtocolReadyThenCtrlBreakStopsWithinDeadlineAndReleasesPort` / Gate、Status | 真实 HTTP/gRPC ready 后，scoped `CTRL_BREAK_EVENT` 5 秒内正常退出并释放端口 |

### 3.17 Integration host composition（6）

Interface：`integration::IntegrationHostFactory::Start`、`HostHandle::BoundEndpoint/Ready/Stop` 与 `CleanupObserver`。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`。通过 shared production targets 构造真实 Phase 3A Modules；transport seam 仅验证 host composition/lifecycle，不声明后续真实 HTTP/gRPC/TCP transport 已完成。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-HOST-01 | `RejectsNonLoopbackEndpoint` | 仅接受 numeric loopback endpoint |
| T09-HOST-02 | `RequiresSelectedConcreteProductionModule` | host family 必须提供对应真实 Phase 3A Module |
| T09-HOST-03 | `RejectsExpiredOwnedDeadline` | 缺失、过期或无界 owned deadline fail closed |
| T09-HOST-04 | `ExposesActualBoundEndpointAndReadyProbe` | handle 暴露实际 bound endpoint 与 protocol-ready probe |
| T09-HOST-05 | `StopIsBoundedAndIdempotent` | stop 使用调用方 deadline，重复调用保留首次 cleanup 结果 |
| T09-HOST-06 | `DestructionPublishesBoundedCleanupResult` | 析构执行 bounded stop 并发布 cleanup 结果，不 detach |

### 3.18 Run-owned process harness（12）

Interface：`integration::RunContext`、`ProcessHarness` 与内部 `Win32ProcessAdapter`。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`；5 个 RunContext、5 个 ProcessHarness、2 个 fault testcase。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-PROC-01..05 | `T09_PROC_RunContext.*`（5） | 唯一 run identity、动态 loopback port、run-owned temp、绝对 deadline、逆序且幂等 cleanup |
| T09-PROC-06..10 | `T09_PROC_ProcessHarness.*`（5） | Win32 start、protocol-ready probe、PID+creation-time identity、bounded graceful/escalated stop、pipe evidence |
| T09-PROC-11 | `T09_PROC_Faults.RefusalDeadlineAndPortConflictAreBoundedAndReleaseOwnership` | refused readiness、expired deadline、occupied port 均 bounded，释放后端口可重绑 |
| T09-PROC-12 | `T09_PROC_Faults.LateOutputAndCleanupFailureRemainSeparateSanitizedAndResidueFree` | late output、primary/cleanup failure 分离并脱敏，reader/process/port/temp residue 为零 |

### 3.19 Gate Beast HTTP transport（7）

Interface：production `CServer` / `HttpConnection` / `LogicSystem` 经 shared `GateTransport.vcxproj` 调用 Phase 3A `GateRequest::Handle`。
Domain/Level：Architecture/Business / Integration。
报告：`server_integration.xml`；真实 HTTP bytes 仅跨 dynamic numeric loopback，不访问 Redis/MySQL/gRPC/公网。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-GHTTP-01 | `PublishesReadyLoopbackEndpointAndStopsIdempotently` | 发布实际 bound loopback endpoint，stop 幂等 |
| T09-GHTTP-02..05 | `*RouteDelegatesToGateRequest`（4） | 四条 production POST route 各且仅各委派一次 `GateRequest::Handle` |
| T09-GHTTP-06 | `FragmentedMaximumBodyIsAcceptedExactlyOnce` | 分片的精确 8 KiB body 被 production Beast parser 接受一次 |
| T09-GHTTP-07 | `OverLimitMalformedAndInterruptedRequestsNeverDispatch` | max+1、畸形与中断请求 fail closed 且不进入业务 dispatch |

### 3.20 Status gRPC transport（12）

Interface：generated `StatusService::Stub` 经 shared `StatusTransport.vcxproj` 调用 production `StatusServiceImpl` 与 Phase 3A `StatusRouting`。
Domain/Level：Architecture/Business / Integration。
报告：`server_integration.xml`；真实 gRPC 仅跨 dynamic numeric loopback，延迟只由 in-memory `StatusStore` fault schedule 控制。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-SGRPC-01 | `T09_SGRPC_Core.PublishesProtocolReadyNumericLoopbackEndpoint` | 发布实际 bound numeric-loopback endpoint 并通过协议 ready |
| T09-SGRPC-02 | `T09_SGRPC_Core.GeneratedGetChatServerStubDelegatesMaximumUidToRouting` | generated GetChatServer stub 委派 Phase 3A routing 并接受 int32 最大边界 |
| T09-SGRPC-03 | `T09_SGRPC_Core.GeneratedLoginStubDelegatesStoredTokenValidation` | generated Login stub 委派已存 token 校验 |
| T09-SGRPC-04 | `T09_SGRPC_CoreStandalone.EmptyServerListFailsClosedOverGeneratedStub` | 空 server list 经真实 transport 保持 fail-closed envelope |
| T09-SGRPC-05 | `T09_SGRPC_Core.InvalidBusinessInputUsesStableSanitizedEnvelope` | 非法业务输入映射稳定错误且不泄漏异常文本 |
| T09-SGRPC-06 | `T09_SGRPC_Fault.DeadlineExpiryIsBoundedAndClassified` | in-memory Adapter 阻塞时由 ClientContext hard deadline 有界终止 |
| T09-SGRPC-07 | `T09_SGRPC_Fault.ExplicitCancellationHasOneTerminalOutcome` | 显式取消只有一个 Cancelled 终态，late server completion 无第二效果 |
| T09-SGRPC-08 | `T09_SGRPC_Fault.RefusedConnectionIsBoundedAndSanitized` | 动态 closed loopback port 在 hard deadline 内返回 sanitized failure |
| T09-SGRPC-09 | `T09_SGRPC_Fault.ShutdownDuringCallCancelsWithoutLateHostMutation` | shutdown deadline 取消 in-flight call，stopped host 状态不被 late completion 改写 |
| T09-SGRPC-10 | `T09_SGRPC_Fault.LateCompletionCannotCrossRestartGeneration` | expired old response 不能跨 restart generation 完成新 call |
| T09-SGRPC-11 | `T09_SGRPC_Fault.OccupiedPortStartupFailsWithoutStealingListener` | occupied port startup fail closed，释放原 owner 后才可 bind |
| T09-SGRPC-12 | `T09_SGRPC_Fault.StopReleasesServerResourcesAndPortForRestart` | stop 幂等并完整释放 server/thread/CQ/socket/port ownership 供同 endpoint 重启 |

### 3.21 Chat TCP transport（16）

Interface：production `chat_transport::CServer` / `CSession` / `ChatFrameCodec` 经唯一 shared `ChatTransport.vcxproj` 调用 Phase 3A `LogicDispatcher`。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`；原始 TCP bytes 仅跨 dynamic numeric loopback，in-memory Adapter 不连接 Redis/MySQL/Status/peer RPC。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-CTCP-01 | `T09_CTCP_Stream.PublishesProtocolReadyNumericLoopbackEndpoint` | 发布实际 bound numeric-loopback endpoint 并 protocol-ready |
| T09-CTCP-02 | `T09_CTCP_Stream.SplitHeaderTraversesProductionSessionAndDispatcher` | 拆分 header 通过 production session/frame/dispatcher exactly once |
| T09-CTCP-03 | `T09_CTCP_Stream.SplitBodyTraversesProductionSessionAndDispatcher` | 拆分 body 完整前不 dispatch，完整后 exactly once |
| T09-CTCP-04 | `T09_CTCP_Stream.CoalescedAdjacentFramesDispatchInExactOrder` | 相邻 frame 合并读取仍按 wire 顺序 dispatch |
| T09-CTCP-05 | `T09_CTCP_Stream.ZeroLengthBodyDispatchesExactlyOnce` | 零 body frame 仍 exactly once |
| T09-CTCP-06 | `T09_CTCP_Stream.MaximumLegalBodyDispatchesWithoutTruncation` | 精确最大 body 不截断 |
| T09-CTCP-07 | `T09_CTCP_Stream.OneByteOverMaximumClosesBeforeDispatch` | max+1 在 dispatch 前关闭 |
| T09-CTCP-08 | `T09_CTCP_Stream.MalformedOversizedHeaderCannotDesynchronizeNextConnection` | 畸形超限 header 不污染下一连接 |
| T09-CTCP-09 | `T09_CTCP_Stream.ReadInterruptionClosesOnlyTheInterruptedSession` | read interruption 只关闭所属 session |
| T09-CTCP-10 | `T09_CTCP_Stream.StopDuringWriteInterruptionIsBoundedAndIdempotent` | write interruption 中 stop 有界幂等 |
| T09-CTCP-11 | `T09_CTCP_Stream.RefusedConnectionCompletesBeforeOwnedDeadline` | refused connect 在 owned deadline 内终止 |
| T09-CTCP-12 | `T09_CTCP_Stream.SilentReadIsCancelledAtTheOwnedDeadline` | silent read 在 owned deadline 取消 |
| T09-CTCP-13 | `T09_CTCP_Stream.QueuedWritesSurvivePartialCompletionsExactlyOnce` | queued writes 经 partial completion/backpressure 保持完整且一次 |
| T09-CTCP-14 | `T09_CTCP_Stream.OccupiedPortIsRejectedWithoutReplacingTheOwner` | occupied port fail closed 且不窃取 listener |
| T09-CTCP-15 | `T09_CTCP_Stream.StopCancelsPendingAcceptAndReleasesThePort` | stop 取消 pending accept 并释放 port |
| T09-CTCP-16 | `T09_CTCP_Stream.StopReleasesSessionsThreadsSocketsAndServerOwnership` | stop 完整释放 session/thread/socket/server/port ownership |

### 3.22 Formal production composition（6 actual；2 planned-only）

Interface：GateServer、StatusServer、ChatServer formal EXE 经 production CLI/config、shared transport target 与 Phase 3A business target 的真实 composition root。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`；不增加 fake dependency mode、test macro、test-only Interface、固定端口、真实外部服务或 synthetic credential。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T09-COMP-01 | `T09_COMP_Formal.GateExplicitConfigWinsAndInvalidSelectedConfigFails` | Gate 显式 config 优先，所选 invalid config fail closed |
| T09-COMP-02 | `T09_COMP_Formal.GateProtocolReadyThenGracefulStopIsBoundedAndReleasesPort` | Gate production HTTP protocol-ready、graceful signal、有界退出与 port release |
| T09-COMP-03 | `T09_COMP_Formal.StatusExplicitConfigWinsAndInvalidSelectedConfigFails` | Status 显式 config 优先，所选 invalid config fail closed |
| T09-COMP-04 | `T09_COMP_Formal.StatusProtocolReadyThenGracefulStopIsBoundedAndReleasesPort` | Status production gRPC protocol-ready、graceful signal、有界退出与 port release |
| T09-COMP-05 | `T09_COMP_Formal.ChatExplicitConfigWinsAndInvalidSelectedConfigFails` | Chat 显式 config 优先，所选 invalid config fail closed |
| T09-COMP-06 | `T09_COMP_Formal.ChatRealDependencyBoundaryIsExplicitBoundedAndResidueFree` | 不伪造 Chat ready；明确 G-015/3C real-dependency boundary，并证明 owned stop/pipe/port/temp cleanup 与稳定 nonzero |

冻结的 `T09-COMP-07..08` 保留为 structure-only planned identifiers；它们不对应 fabricated JUnit case，也不计入 313 baseline。

## 4. Qt testcase 目录（49）

### 4.1 Frame decoder（1 runner testcase / 4 contracts）

Interface：`TcpFrameDecoder::append` 的流式状态转换。
Domain/Level：Foundation / Unit。
报告：`client_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T07-FRM-01 | `network_state_tests` | 拆分 header 被保留并按网络序解码 |
| T07-FRM-02 | `network_state_tests` | 拆分 body 在完整前不发出 frame |
| T07-FRM-03 | `network_state_tests` | 相邻 frame 和零 body 按序发出且无残留 |
| T07-FRM-04 | `network_state_tests` | connection reset 丢弃旧半帧，新连接完整帧独立解析 |

### 4.2 Message model/store/delegate（8）

Interface：消息模型索引、历史、状态、Store 与 delegate 布局。
Domain：Business。
报告：Unit 写入 `client_unit.xml`，Component 写入 `client_component.xml`。

| Test ID | CTest name | Level | 合同 |
| --- | --- | --- | --- |
| Q01-MODEL-01 | `message_model.append_ack_status_removal` | Unit | 插入、ack、状态和删除保持索引同步 |
| Q01-MODEL-02 | `message_model.unknown_ids` | Unit | 未知稳定 ID 不修改模型 |
| Q01-MODEL-03 | `message_model.history_order` | Unit | 历史去重并保持时间顺序 |
| Q01-MODEL-04 | `message_model.multiple_history_pages` | Unit | 多页历史合并保持稳定顺序 |
| Q01-MODEL-05 | `message_model.shifted_indexes` | Unit | 删除/ack 后重建偏移索引 |
| Q01-MODEL-06 | `message_model.text_and_chat_identity` | Unit | 文本和 chat identity 无损 round-trip |
| Q01-MODEL-07 | `message_model.store_pagination_state` | Component | 每个 chat 保留独立 model 与分页状态 |
| Q01-MODEL-08 | `message_model.delegate_reflow` | Component | 窄视口下 delegate 合法重排长文本 |

### 4.3 Authenticated session reset/retry（6 runner testcase / 9 contracts）

Interface：生产 `ClientSession::resetSession`，并通过真实 `TcpMgr`、`UserMgr` 和 owned session root 完成连接与账号状态清理。
Domain/Level：Architecture/Business / Component + Integration。
前 5 个 runner testcase 写入 `client_component.xml`，不建立真实 socket；
authenticated retry 写入 `client_integration.xml`，仅使用动态 loopback，不访问外部服务。

| Test ID | CTest name | 合同 |
| --- | --- | --- |
| Q02-SESSION-01..04 | `session_reset.account_state` | 清 user/token、好友/申请/会话 map、联系人/聊天 cursor 与 loading，同时保留应用级服务器配置 |
| Q02-SESSION-05 | `session_reset.owned_ui_and_idempotence` | 销毁旧 session UI/MessageModelStore 所有权树，保留精确 reset reason，重复 reset 无二次通知/释放 |
| Q02-SESSION-06 | `session_reset.pending_batch` | 清旧 pending batch、停止 reset 后新发送，未知旧 failure 不携带 client ID 修改后续 session |
| Q02-SESSION-07 | `session_reset.uncertainBatchSurvivesDisconnectAndMatchesExactUuid` | 按 UUID 集合匹配乱序回复；暂时错误/畸形成功保留 pending，终态只清匹配批次 |
| Q02-SESSION-08 | `session_reset.retryDoesNotCrossAuthenticatedAccounts` | pending 绑定认证账号，切换账号不重放旧批次 |
| Q02-SESSION-09 | `session_reset.authenticated_wire_retry` | 实际断线保留原 bytes/UUID，重新认证前不重放，认证后发送相同批次 |

### 4.4 Auth/network outcome coordinator（12）

Interface：production `AuthFlowCoordinator::Reduce(flowId, AuthOutcome) -> AuthAction`；`AuthAction::kind` 以 optional 稳定表达无 action，存在的 action 仅为 `StayAndShowError | ConnectChat | ShowLogin | ShowChat`。
Domain/Level：Business/Architecture；Q03-AUTH-01..11 为 Unit，Q03-AUTH-12 为 Component。
报告：Unit 写入 `client_unit.xml`，Component 写入 `client_component.xml`；synthetic outcome，无真实 HTTP/TCP。

| Test ID | CTest name | 合同 |
| --- | --- | --- |
| Q03-AUTH-01..03 | `auth_flow.*_network_error` | Register/Reset/Login network failure 各留在当前 flow 且只产生一次稳定 error action |
| Q03-AUTH-04 | `auth_flow.unknown_outcome` | 未知 module/request 不改变当前 flow |
| Q03-AUTH-05..06 | `auth_flow.malformed_json`、`auth_flow.business_error` | JSON/business failure 留在当前页面且不连接 Chat |
| Q03-AUTH-07..10 | `auth_flow.login_http_success`、`tcp_failure`、`chat_login_failure`、`chat_login_success` | HTTP/TCP/Chat login 只按合法 stage 推进，failure 不进入 Chat，success exactly-once |
| Q03-AUTH-11 | `auth_flow.duplicate_and_late` | duplicate 与旧 flow outcome 不 action |
| Q03-AUTH-12 | `auth_flow.abnormal_disconnect_reset` | abnormal disconnect production mapping 复用 `ClientSession::resetSession(UnexpectedDisconnect)` |

Plan 3B-02 前 Qt runner testcase 合计 24：Unit 18、Component 6。Level 精化不改变生产 Module 目录或断言。

### 4.5 Gate HTTP transport（10）

Interface：production `GateHttpTransport::post/cancel/reset` 与单一 `finished(GateHttpResult)` 终态；QNAM/reply 通过 pImpl 保持私有。
Domain/Level：Architecture/Business / Integration。
报告：`client_integration.xml`；真实 QNetworkAccessManager 仅连接 run-owned numeric loopback peer。

| Test ID | CTest name | 合同 |
| --- | --- | --- |
| Q04-HTTP-01 | `http_transport.successPreservesRequestAndFlowIdentity` | 成功响应保留 flow/request/module identity |
| Q04-HTTP-02 | `http_transport.refusedConnectionHasOneBoundedOutcome` | loopback socket 拒绝映射为单一 NetworkError |
| Q04-HTTP-03 | `http_transport.finiteDeadlineAbortsAnUnresponsivePeer` | silent peer 由 transport deadline 有界终止 |
| Q04-HTTP-04 | `http_transport.malformedJsonIsRejectedWithoutLeakingItsBody` | 畸形 JSON 返回稳定错误且不传播 body |
| Q04-HTTP-05..06 | `http_transport.maximumResponseIsAccepted` / `oneByteOverMaximumIsRejected` | 精确 8 KiB 接受，max+1 在累积时拒绝 |
| Q04-HTTP-07 | `http_transport.peerClosingMidResponseHasOneNetworkOutcome` | response 中途关闭只产生一个 network outcome |
| Q04-HTTP-08 | `http_transport.explicitCancelHasExactlyOneTerminalOutcome` | cancel 与随后的 reply signal 合并为一个终态 |
| Q04-HTTP-09 | `http_transport.lateReplyFromAnOldFlowCannotCompleteTheNewFlow` | generation 抑制旧 flow late completion |
| Q04-HTTP-10 | `http_transport.deletingTransportReleasesReplyAndLoopbackSocket` | QObject/reply/loopback socket 随 owner 有界释放 |

### 4.6 Chat TCP transport（12）

Interface：production `ChatTcpTransport::connectTo/send/close/reset`、real `QTcpSocket` 与既有 `TcpFrameDecoder`；socket/decoder/queue/timer 经 pImpl 保持私有。
Domain/Level：Architecture/Business / Integration。
报告：`client_integration.xml`；真实 QTcpSocket 仅连接 run-owned numeric loopback peer。

| Test ID | CTest name | 合同 |
| --- | --- | --- |
| Q04-TCP-01 | `tcp_transport.connectPreservesGenerationAndFlowIdentity` | connect 保留 generation/flow identity |
| Q04-TCP-02 | `tcp_transport.sendWritesProductionFrame` | send 写出 production frame bytes |
| Q04-TCP-03 | `tcp_transport.fragmentedFrameDecodedOnce` | fragmented frame exactly once |
| Q04-TCP-04 | `tcp_transport.coalescedFramesStayOrdered` | coalesced frames 保持 wire 顺序 |
| Q04-TCP-05 | `tcp_transport.maximumFrameIsAccepted` | 精确 2 KiB body boundary 接受 |
| Q04-TCP-06 | `tcp_transport.malformedOversizedFrameTerminates` | malformed oversized frame 单一终止 |
| Q04-TCP-07 | `tcp_transport.refusedConnectHasOneBoundedOutcome` | refused connect 有界且一个终态 |
| Q04-TCP-08 | `tcp_transport.writeDeadlineAbortsSilentPeer` | silent peer 写入由 deadline 终止 |
| Q04-TCP-09 | `tcp_transport.peerCloseMidWriteHasOneTerminalOutcome` | peer mid-write close 只有一个终态 |
| Q04-TCP-10 | `tcp_transport.resetDiscardsHalfFrame` | reset 丢弃旧 half-frame |
| Q04-TCP-11 | `tcp_transport.lateOldGenerationCannotCompleteRetry` | late old generation 不能完成 retry |
| Q04-TCP-12 | `tcp_transport.closeAndDeleteReleaseOwnedResources` | close/delete 有界释放 QObject/socket/timer/queue |

Qt runner testcase 合计 46：Unit 18、Component 6、Integration 22。

## 5. VarifyServer testcase 目录（29）

### 5.1 Configuration process（9）

Interface：配置来源、凭据环境变量和明文配置拒绝。
Domain/Level：Foundation / Integration。
报告：`varify_integration.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| V01-CFG-01 | `explicit --config takes precedence over CHAT_CONFIG` | CLI 配置路径优先 |
| V01-CFG-02 | `CHAT_CONFIG takes precedence over the working-directory default` | 环境配置高于工作目录默认值 |
| V01-CFG-03 | `config.json is loaded from the working directory by default` | 默认读取工作目录配置 |
| V01-CFG-04 | `malformed configuration exits with a failure status` | 畸形 JSON 非零退出 |
| V01-CFG-05 | missing `CHAT_VARIFY_EMAIL_USER` | 缺少变量失败且不输出值 |
| V01-CFG-06 | missing `CHAT_VARIFY_EMAIL_PASS` | 缺少变量失败且不输出值 |
| V01-CFG-07 | missing `CHAT_VARIFY_MYSQL_PASSWORD` | 缺少变量失败且不输出值 |
| V01-CFG-08 | missing `CHAT_VARIFY_REDIS_PASSWORD` | 缺少变量失败且不输出值 |
| V01-CFG-09 | `plaintext credential fields in config.json are rejected` | 旧明文凭据字段被拒绝 |

### 5.2 Handler（9）

Interface：验证码缓存、生成、TTL、邮件错误、callback 和日志脱敏。
Domain/Level：Business / Unit。
报告：`varify_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| V06-MOD-01 | `requiring the handler module does not load Redis or SMTP adapters` | 模块加载无外部连接副作用 |
| V06-HDL-01 | `cached code is reused without UUID generation or Redis write` | 缓存命中复用 code |
| V06-HDL-07 | `mail uses the sender supplied by runtime configuration` | 发件人来自运行时配置 |
| V06-HDL-02 | `missing code generates four characters and stores a 600 second TTL` | 缺失时生成四字符并写 600 秒 TTL |
| V06-HDL-03 | `failed Redis write returns RedisErr without sending mail` | Redis 写失败不发邮件 |
| V06-HDL-04 | `false mail result returns Exception` | mailer false 映射 Exception |
| V06-HDL-05 | `Redis read rejection returns Exception without sending mail` | Redis rejection 不发邮件并映射 Exception |
| V06-HDL-06 | `mail rejection returns Exception` | SMTP rejection 映射 Exception |
| V06-LOG-01 | `default handler events never disclose the recipient or verification code` | 默认日志不泄漏收件人或验证码 |

### 5.3 Protocol（6）

Interface：wire-visible error 与 Varify RPC descriptor。
Domain/Level：Foundation / Unit。
报告：`varify_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| V02-PROTO-01 | `shared verification constants keep their wire-visible values` | 错误码数值稳定 |
| V02-PROTO-02 | `loaded protobuf exposes the verification RPC request and response types` | descriptor 暴露正确 RPC 和消息类型 |
| V02-PROTO-03 | `repository canonical proto module is the sole editable wire authority` | 仅 `proto/varify.proto`、`status.proto`、`chat.proto` 为 editable authority |
| V02-PROTO-04 | `protocol compatibility command accepts the canonical contract and generated consumers` | 固定生成链、descriptor 兼容与提交式 generated drift 同时 GREEN |
| V02-PROTO-05 | `compatibility command rejects isolated field-number and RPC-name drift` | 隔离副本字段号或 RPC rename 必须 RED，不修改 canonical/baseline |
| V02-PROTO-06 | `current Node consumer parses the initial wire fixture with unknown fields` | 当前 Node 解析迁移前固定 payload；unknown field 不破坏公开 email |

### 5.4 gRPC route（1）

Interface：生产 `createServer(handler)` 的服务注册和动态 loopback 路由。
Domain/Level：Architecture / Integration。
报告：`varify_integration.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T05-GRPC-01 | `loopback service routes GetVarifyCode to the registered handler` | 真实 loopback gRPC 路由到注入 handler |

### 5.5 Startup（4）

Interface：`startServer` bind/start 转换和直接进程 bind 失败。
Domain：Architecture。
报告：Unit 在 `varify_unit.xml`，Integration 在 `varify_integration.xml`。

| Test ID | Level | Runner testcase | 合同 |
| --- | --- | --- | --- |
| V07-START-01 | Unit | `bind failure rejects startup and never starts the server` | bind error 拒绝且不 start |
| V07-START-02 | Unit | `zero bound port rejects startup and never starts the server` | 无效实际端口拒绝且不 start |
| V07-START-03 | Unit | `successful bind starts once and exposes the actual bound port` | 成功 bind 只 start 一次并返回实际端口 |
| V07-START-04 | Integration | `direct process exits with a failure status when its port is occupied` | 固定端口冲突时直接进程非零退出且不泄密 |

## 6. PowerShell testcase 目录（13）

### 6.1 Instance validation（9）

Interface：`chatserver-instances.ps1 -Task Start` 的启动前批量配置校验。
Domain/Level：Architecture / Component。
报告：`script_component.xml`（9 个逐 Test ID testcase）。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| A02-VAL-01 | `Duplicate SelfServer.Name is rejected` | 实例名唯一 |
| A02-VAL-02 | `Duplicate Log.Name is rejected` | 日志名唯一 |
| A02-VAL-03 | `TCP and RPC cross-type collision is rejected` | 跨类型监听端口唯一 |
| A02-VAL-04 | `Port 08090 is normalized to 8090 before comparison` | 端口规范化后比较 |
| A02-VAL-05 | `Unsafe config file name is rejected` | 配置 basename 安全 |
| A02-VAL-06 | `Duplicate config base names are rejected` | 配置 basename 唯一 |
| A02-VAL-07 | `Missing config file is rejected` | 缺失配置拒绝 |
| A02-VAL-08 | `Missing config argument is rejected` | Start 缺配置参数拒绝 |
| A02-VAL-09 | `Missing required config value is rejected` | 缺必填值拒绝 |

### 6.2 Instance lifecycle（4）

Interface：Start/Status/Stop 的进程身份、失败诊断和冲突保护。
Domain/Level：Architecture / Integration。
报告：`script_integration.xml`（4 个逐 Test ID testcase）。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| A02-LIFE-01 | `A child that exits during startup reports diagnostics and leaves no state` | 启动失败有退出诊断且无状态残留 |
| A02-LIFE-02 | `Status treats a reused PID with a different start time as stopped` | PID 重用但身份不同视为 Stopped |
| A02-LIFE-03 | `Stop never terminates a PID whose recorded identity is stale` | Stop 不误杀身份不匹配进程 |
| A02-LIFE-04 | `Start rejects conflicts recorded by an independently running instance` | 新批次拒绝与既有运行实例冲突 |

## 7. 当前覆盖缺口与计划归属

### Phase 3A contract 状态

以下 Test ID 已在 [`plans/PHASE-3A-TEST-PLAN.md`](plans/PHASE-3A-TEST-PLAN.md)
冻结。3A-01..05 的生产 Interface、测试源与 runner registration 已落地并进入当前 baseline。

下表与后续 G-008..G-018 行使用的 canonical 正式计划为
[`PHASE-3A-PLAN.md`](plans/PHASE-3A-PLAN.md)、
[`PHASE-3B-PLAN.md`](plans/PHASE-3B-PLAN.md)、
[`PHASE-3C-PLAN.md`](plans/PHASE-3C-PLAN.md)、
[`PHASE-3D-PLAN.md`](plans/PHASE-3D-PLAN.md) 和
[`PHASE-RELEASE-PLAN.md`](plans/PHASE-RELEASE-PLAN.md)。这些 route 均为 planned/open owner 定位，
不构成远端完成证据，不改变当前 13 份报告、313 个 runner testcase 或仍为 planned-only 的 Test ID 统计状态；
阶段边界与 actual-or-bootstrap 条件继续严格遵守 DG-09..DG-24；所有后续计划同时受 DG-25 的本机 vcpkg
不可变/审批门禁约束。

| Plan / Gap | Planned Test IDs | 唯一生产 Interface | Planned Level / report | 当前状态 |
| --- | --- | --- | --- | --- |
| [3A §7 / 3A-01](plans/PHASE-3A-PLAN.md) / G-007 | T08-LOGIC-01..07 | `LogicDispatcher::Submit/Stop` | Unit / `server_unit.xml` | complete；7 testcase 已进入 180 baseline |
| [3A §8 / 3A-02](plans/PHASE-3A-PLAN.md) / G-010 | T08-STATUS-01..14 | `StatusRouting::Assign/Validate` | Unit + Component / `server_unit.xml` + `server_component.xml` | complete；14 testcase 已进入 194 baseline |
| [3A §9 / 3A-03](plans/PHASE-3A-PLAN.md) / G-008 | T08-SESSION-01..10 | `ChatSessionState` 的 create/register/find/close/send | Component / `server_component.xml` | complete（3A in-memory part）；10 testcase 已进入 204 baseline |
| [3A §10 / 3A-04](plans/PHASE-3A-PLAN.md) / G-009 | T08-GATE-01..16 | `GateRequest::Handle` | Component / `server_component.xml` | complete（3A in-process part）；16 testcase 已进入 220 baseline |
| [3A §11 / 3A-05](plans/PHASE-3A-PLAN.md) / G-011 | Q03-AUTH-01..12 | `AuthFlowCoordinator::Reduce` | Unit + Component / `client_unit.xml` + `client_component.xml` | complete（3A outcome/state 部分）；12 testcase 已进入 232 baseline |

### Phase 3B planned contract registry

The following identifiers are frozen by [`PHASE-3B-TEST-PLAN.md`](plans/PHASE-3B-TEST-PLAN.md). T09-HOST contributes six emitted cases, T09-PROC contributes twelve, Plan 3B-02 contributes seven T09-GHTTP plus ten Q04-HTTP cases, Plan 3B-03 contributes twelve T09-SGRPC cases, Plan 3B-04 contributes sixteen T09-CTCP plus twelve Q04-TCP cases, and Plan 3B-05 contributes six actual T09-COMP cases to the current 13-report/313-case local baseline. T09-COMP-07..08 remain planned-only and contribute zero; remote closeout evidence remains pending.

| Plan | Planned Test IDs | Domain / Level | Planned report owner | Current state |
| --- | --- | --- | --- | --- |
| 3B-00 | `T09-HOST-01..06` | Architecture / Integration | existing `server_integration.xml` | complete; 6 testcase 已进入 238 baseline |
| 3B-01 | `T09-PROC-01..12` | Architecture / Integration | existing `server_integration.xml` | complete; 12 testcase 已进入 250 baseline |
| 3B-02 | `T09-GHTTP-01..07` | Architecture/Business / Integration | existing `server_integration.xml` | complete; 7 testcase 已进入 267 baseline |
| 3B-02 | `Q04-HTTP-01..10` | Architecture/Business / Integration | `client_integration.xml` | complete; 10 testcase 已进入 267 baseline |
| 3B-03 | `T09-SGRPC-01..12` | Architecture/Business / Integration | existing `server_integration.xml` | complete; 12 testcase 已进入 279 baseline |
| 3B-04 | `T09-CTCP-01..16` | Architecture / Integration | existing `server_integration.xml` | complete; 16 testcase 已进入 307 baseline |
| 3B-04 | `Q04-TCP-01..12` | Architecture/Business / Integration | `client_integration.xml` | complete; 12 testcase 已进入 307 baseline |
| 3B-05 | `T09-COMP-01..06` | Architecture / Integration | existing `server_integration.xml` plus structure gate | local complete; 6 testcase 已进入 313 baseline；remote pending |
| 3B-05 | `T09-COMP-07..08` | Architecture / Integration | structure gate only | planned-only; no fabricated JUnit case |

3A-05 的 production library、Module README、非空测试源、真实 CMake/runner registration 与 production Adapter 接线均已落地。

| Gap ID | Module / Interface | 当前风险 | 目标 Level | 计划归属 | 目标 lane |
| --- | --- | --- | --- | --- | --- |
| G-001 | Gate/Status CLI 与 Config | 已由 Plan 2.5-04 关闭：真实 EXE CLI/config/bind/ready/stop，15 秒 startup、5 秒 stop 与 scoped PID cleanup | Integration | Phase 2.5-04 complete | develop |
| G-002 | 跨服务 protobuf | 已由 Plan 2.5-02 关闭：canonical Module、初始 descriptor、旧 wire fixture、生成漂移与 C++→Node loopback | Unit/Integration | Phase 2.5-02 complete | develop |
| G-003 | C++ gRPC clients/pools | 已由 Plan 2.5-05 关闭：统一有限 deadline、实际池生命周期、动态 loopback failure mapping、启动配置边界与共享生产库 | Unit/Integration | Phase 2.5-05 complete | develop |
| G-004 | Gate 响应脱敏 | 已由 Plan 2.5-03 关闭：四端点精确 allowlist、稳定 JSON/业务/RPC/异常 envelope、响应与日志 secret regression | Component | Phase 2.5-03 complete | develop |
| G-005 | Qt connection/session reset | 已由 Plan 2.5-06 关闭：decoder/reconnect reset、pending batch、账号 transient state、owned 页面/MessageModelStore 销毁、幂等与关闭分类 | Unit/Component | Phase 2.5-06 complete | develop |
| G-006 | PowerShell JUnit | 已由 Plan 2.5-01 关闭：13 个逻辑用例逐 Test ID 呈现 | reporting | Phase 2.5-01 complete | develop |
| G-007 | Chat Logic dispatcher | Phase 3A-01 已关闭：FIFO、多 producer exactly-once、精确容量、wake/drain/closed/幂等 Stop 与未知 ID 脱敏诊断 | Unit | Phase 3A-01 complete | develop |
| G-008 | Chat CSession/session registry | 3A in-memory identity、FIFO、容量、并发/旧 session close 已关闭；真实 TCP partial write/peer disconnect 与 Redis presence integration 仍未证明 | Component/Integration | Phase 3A-03 in-memory part complete → [3B §11 / 3B-04；§12 / 3B-05](plans/PHASE-3B-PLAN.md) → Phase 3C Redis Integration | develop |
| G-009 | Gate request Module | 3A in-process 注册/登录/重置顺序、早退和依赖失败已保护；真实 HTTP 组合及 Redis/MySQL/gRPC/SMTP Adapter 仍未证明 | Component/Integration | Phase 3A-04 in-process part complete → [3B §9 / 3B-02；§12 / 3B-05](plans/PHASE-3B-PLAN.md) → Phase 3C real Adapters | develop |
| G-010 | Status selector/token | Phase 3A-02 已保护空列表、确定性选择和 token fail-closed；真实 Redis 与 process 组合仍未证明 | Unit/Component/Integration | Phase 3A-02 complete → [3B §10 / 3B-03；§12 / 3B-05](plans/PHASE-3B-PLAN.md) → Phase 3C Redis Integration | develop |
| G-011 | Qt UserMgr/TcpMgr/HttpMgr/UI state | 3A synthetic outcome/state、duplicate/late 与 abnormal reset wiring 已保护；真实 HTTP/TCP transport timing 未证明 | Unit/Component/Integration | Phase 3A-05 outcome/state part complete → [3B §9 / 3B-02；§11 / 3B-04；§12 / 3B-05](plans/PHASE-3B-PLAN.md) | develop |
| G-012 | Redis Adapters | 真实命令、断线、锁和 TTL 未证明 | Integration | [3C §11 / 3C-02；§13 / 3C-04；§16 / 3C-07；§18 / 3C-09](plans/PHASE-3C-PLAN.md) | master |
| G-013 | MySQL Adapters | 无可重复 schema/migration/事务测试 | Integration | [3C §11 / 3C-02；§12 / 3C-03；§14 / 3C-05；§16 / 3C-07；§18 / 3C-09](plans/PHASE-3C-PLAN.md) | master |
| G-014 | SMTP Adapter | 真实发送参数和错误映射未证明 | Integration | [3C §11 / 3C-02；§15 / 3C-06；§16 / 3C-07；§18 / 3C-09](plans/PHASE-3C-PLAN.md) | master |
| G-015 | 四进程生命周期 | run-owned process/port/temp/deadline 与 Gate/Status 单进程 ready/stop 已覆盖；四发布单元依赖编排、共享状态与业务恢复仍未证明 | Integration | [3B §8 / 3B-01；§10 / 3B-03；§11 / 3B-04；§12 / 3B-05](plans/PHASE-3B-PLAN.md) → [3C §9 / 3C-00；§10 / 3C-01；§11 / 3C-02；§16 / 3C-07；§18 / 3C-09](plans/PHASE-3C-PLAN.md) | master |
| G-016 | 双 ChatServer 业务流 | 跨实例好友/消息/重连/历史没有公开 E2E | E2E | [3C §14 / 3C-05（持久化前置）](plans/PHASE-3C-PLAN.md) → [3D §9..12 / 3D-00..03；§14 / 3D-05（current-N owner）](plans/PHASE-3D-PLAN.md) | master/release |
| G-017 | 版本兼容 | 当前版与上一发布版无自动矩阵 | Compatibility | [3C §12 / 3C-03；§17 / 3C-08；§18 / 3C-09](plans/PHASE-3C-PLAN.md) → [3D §13 / 3D-04；§14 / 3D-05](plans/PHASE-3D-PLAN.md) → [Release §10 / R-01；§12 / R-03](plans/PHASE-RELEASE-PLAN.md) | master |
| G-018 | artifact/UAT | 尚无同产物 smoke 与版本化人工清单 | Release | [Release §9 / R-00；§10 / R-01；§11 / R-02；§12 / R-03](plans/PHASE-RELEASE-PLAN.md) | release |

### Phase 3C disposable services registration

`T10-SVC-01..12` are owned by [the dependency coordinator](services/README.md),
Architecture / Integration, CTest label `phase3c-services`, report
`linux_services.xml`, job `Phase 3C disposable services`. The runner proves job
container/port identity, synthetic authentication, run-owned data, bounded real
faults and cleanup. It does not close production Adapter G-012..G-015 or change
the existing Windows report baseline. Actual execution status belongs to
`docs/Status.md`; an unexecuted hosted test is not PASS.

### Phase 3C Redis / SMTP adapter registration

| IDs | Owner / level | Report / count |
| --- | --- | --- |
| T10-RDS-01..09 | [Native Redis](server/data/README.md), Component / Integration | `linux_redis.xml`, 9 |
| V08-REDIS-01..06 | [Node Redis](../VarifyServer/test/redis/README.md), Integration | `varify_redis.xml`, 6 |
| V09-SMTP-01..12 | [SMTP](../VarifyServer/test/smtp/README.md), Integration | `varify_smtp.xml`, 12 |

These run through `3C-adapters` in the owned hosted services job, alongside the
existing twelve infrastructure cases. Missing or failed selected reports fail
the outer gate. G-012/G-014 remain open until hosted evidence is accepted;
registration and local socket tests do not imply Redis/Mailpit acceptance.
V09-SMTP-06..11 also run locally in `varify_integration.xml`; these are shared
contracts executed in two lanes, not twelve different Test IDs. Local adapter
unit/loopback IDs and their precise assertions are owned by the linked module
READMEs. Schema/MySQL G-013 has an authoritative historical schema-only source;
its current-N implementation and hosted acceptance remain separate from Redis/SMTP.

### Phase 3C SchemaMigration / MessageCommit registration

| IDs | Owner / level | Report / count | Contract |
| --- | --- | --- | --- |
| T10-MIG-01..02 | [SchemaMigration](server/schema-migration/README.md) / Integration | `linux_migration.xml` / 2 | Empty 0→N, repeat application preserves data |
| T10-MIG-03..06 | Same / Integration | same / 4 | Checksum drift, unknown version, missing index/routine fail closed |
| T10-MIG-07..09 | Same / Integration | same / 3 | Concurrent same/distinct registration UID uniqueness; missing seed rolls back |
| T10-MIG-10..12 | Same / Integration | same / 3 | Real partial DDL failure and explicit empty-bootstrap recovery, routine-body drift, owned auxiliary cleanup |
| T10-MSG-01 | [MessageCommit](server/message-commit/README.md) / Component | `linux_message_commit.xml` / 1 | Canonical UUID and pre-storage validation |
| T10-MSG-02..12 | Same / Integration | same / 11 | Created/Existing stable IDs, content/chat/recipient conflicts, batch rollback, principal/membership/deadline rejection |
| T10-MSG-13..14 | Same / Integration | same / 2 | Real concurrent connections share one row/ID; killed transaction at COMMIT leaves no partial batch |
| T10-MSG-15..19 | Same / Integration | same / 5 | Autocommit restored, SQL error rollback, NULL legacy UUID/history IDs, bounded real row-lock contention |
| T10-MSG-20 | Same / Integration | same / 1 | Closed-storage rejection, native process exit, exact owned database teardown |

SchemaMigration has 12 cases; MessageCommit has 20. They use the same versioned
SQL and owned hosted MySQL service. The native MessageCommit executable links the
production library and connects through its C++ driver to the mapped TCP port;
the migration helper's mysql CLI is not itself native DAO proof. Both groups are
additional reports and do not alter the thirteen-report/342-case Windows count.
No promoted N-1 is supplied: `BOOTSTRAP_NO_PROMOTED_N_MINUS_1` remains a compatibility
gap; importing historical DDL or rebuilding a disposable empty database is not an
N-1 upgrade result. Local MySQL 8.0 evidence does not substitute for hosted 8.4
acceptance. These tests do not prove four-process startup or cross-server public
E2E; actual accepted status belongs to the main workspace's `docs/Status.md`.

## 8. 矩阵维护规则

3C-07 新登记 `T10-4PROC-01..18`，Domain 为 Architecture/Business、Level 为 Integration，
owner 为 `tests/services/fourProcessCases.js`，公开入口为 Linux `3C-07` selector，
独立报告为 `linux_four_process.xml`。合同细分、期限和清理见 [模块入口](services/README.md#four-production-processes-3c-07)。
本地驱动/报告回归不计为上述真实依赖用例通过；G-015 的验收须查当前状态页的 hosted 证据。

- 新 testcase 合并时必须先分配 Test ID，并更新本矩阵及所属 Module README。
- 删除、合并或改写 Test ID 必须遵守 D-04 合同变更流程。
- `CheckTestStructure` 负责验证测试文件和 runner 注册；本矩阵负责语义、lane 和合同归属，二者不能互相替代。
- 执行证据按 [`CI-GOVERNANCE.md` section 2.1](CI-GOVERNANCE.md#21-比例化执行合同) 分层；本矩阵不要求任务重复
  full lane、secret/residue/diff audit 或无意义 mutation，这些聚合证据只由 phase closeout 收集一次。
- 每次发布后将“上一已发布版本”指针更新到本次 artifact，并保存兼容 fixture 和测试结果。
- 数量变化必须同时说明新增/删除 Test ID、runner testcase 和报告变化，禁止只报告一个总覆盖率百分比。
- 本机 vcpkg 工具/安装目录、manifest 自动安装、restore/install/remove/update/upgrade、triplet/baseline 或 install root
  变化受 DG-25 约束；未取得针对精确命令与目标的批准时必须 fail closed，不得把依赖恢复写成普通测试前置动作。
