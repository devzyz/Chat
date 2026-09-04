# 当前自动测试合同矩阵

状态：Phase 3A Plan 3A-05 基线清单
基线日期：2026-09-02
治理规则：[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md)

## 1. 统计口径

当前本地基线包含 232 个 runner testcase：Server 166、Qt 24、VarifyServer 29、PowerShell 13。

Test ID 表示被保护的 Interface 合同，runner testcase 表示测试框架实际报告的用例。二者不必一一对应：例如 Qt `network_state_tests` 是一个 runner testcase，但同时保护拆分 header、拆分 body 和相邻 frame 三个合同。覆盖率、testcase 数量和合同数量必须分别报告，不能相互替代。

当前所有 232 个 testcase 都属于 `develop` 全量快速门禁；`master` 和 release 继承它们。真实 Redis/MySQL/SMTP 与业务 E2E 尚不存在，因此不在当前 232 个基线中。

Plan 2.5-07 的历史本地证据为 12 份报告 / 173 testcase。Plan 3A-01 在同一报告集合中新增
7 个 `LogicDispatcherTests` runner testcase；focused 7/7 与完整 `server_unit.xml` 60/60
均已通过，manifest 因此增至 12 份报告 / 180 testcase。Plan 3A-02 又在同一报告集合中增加
8 个 Unit 与 6 个 Component case，当前 manifest
因此为 12 份报告 / 194 testcase。Plan 3A-03 再增加 10 个 Chat session Component case，当前 manifest
为 12 份报告 / 204 testcase。Plan 3A-04 再增加 16 个 Gate request Component case，当前 manifest
为 12 份报告 / 220 testcase。Plan 3A-05 再增加 11 个 Qt Unit 与 1 个 Qt Component case，当前 manifest
为 12 份报告 / 232 testcase。完整 `RunAllTests`、clean PR、远端 artifact 和 `develop`
branch protection 仍必须由 3A-06/实际 GitHub check 证明。

## 2. Suite 与报告

| Suite | Module | Domain | Current Level | Testcases | Report | 当前依赖 | Lane |
| --- | --- | --- | --- | ---: | --- | --- | --- |
| Server unit | config、messaging、transport、protocol、rpc、Chat lifecycle/logic dispatcher、Status selection | Foundation/Architecture/Business | Unit | 68 | `server_unit.xml` | in-process；临时文件；旧 wire fixture | develop required |
| Gate Asio | Gate lifecycle | Foundation | Unit | 2 | `server_gate_unit.xml` | in-process thread/io_context | develop required |
| Status Asio | Status lifecycle | Foundation | Unit | 2 | `server_status_unit.xml` | in-process thread/io_context | develop required |
| Server component | Chat Redis pool/session state、Gate response/request、Status token/store | Foundation/Architecture/Business | Component | 56 | `server_component.xml` | in-process fake；不连接 Redis/MySQL/Status/Varify | develop required |
| Server integration | Chat/Gate/Status startup、C++→Node Varify、Gate gRPC clients | Architecture | Integration | 34 | `server_integration.xml` | 受控子进程、动态 loopback 端口 | develop required |
| Chat gRPC integration | Chat production gRPC clients | Architecture | Integration | 4 | `server_chat_grpc_integration.xml` | 动态 loopback 端口、无外部服务 | develop required |
| Qt unit | frame decoder、message model rules Q01-MODEL-01..06、auth outcomes Q03-AUTH-01..11 | Foundation/Architecture/Business | Unit | 18 | `client_unit.xml` | Qt Core/Widgets minimal，无 socket | develop required |
| Qt component | message store/delegate Q01-MODEL-07..08、session reset Q02-SESSION-01..06、auth reset wiring Q03-AUTH-12 | Architecture/Business | Component | 6 | `client_component.xml` | Qt Widgets/Network minimal；真实 in-process Module，无连接 | develop required |
| Varify unit | protocol、handler、fake startup | Foundation/Business/Architecture | Unit | 18 | `varify_unit.xml` | in-process fake；descriptor/fixture | develop required |
| Varify integration | config、loopback RPC、process startup | Foundation/Architecture | Integration | 11 | `varify_integration.xml` | 子进程或动态 loopback | develop required |
| Script component | instance validation | Architecture | Component | 9 | `script_component.xml` | 临时目录、占位进程 | develop required |
| Script integration | instance lifecycle | Architecture | Integration | 4 | `script_integration.xml` | 受控子进程/PID identity | develop required |
| **合计** |  |  |  | **232** | 12 份报告 | 无个人服务或凭据 |  |

PowerShell 报告逐项输出 13 个 testcase：validation 9 个、lifecycle 4 个；控制台同步保留逐 Test ID 的 PASS/FAIL。该报告粒度改进不改变本表的逻辑基线数量。

## 3. Server testcase 目录（166）

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

### 3.8 Chat session registry/send state（10）

Interface：production `ChatSessionState::Create/RegisterCurrent/FindCurrent/Close/Send`；唯一外部身份为 opaque session handle。`UserMgr` 持有唯一 registry，`CServer`、`CSession`、`LogicSystem` 和 `ChatServiceImpl` 统一委托该 Interface。
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

## 4. Qt testcase 目录（24）

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

### 4.3 Authenticated session reset（3 runner testcase / 6 contracts）

Interface：生产 `ClientSession::resetSession`，并通过真实 `TcpMgr`、`UserMgr` 和 owned session root 完成连接与账号状态清理。
Domain/Level：Architecture/Business / Component。
报告：`client_component.xml`。测试不建立真实 socket，不访问固定端口或外部服务。

| Test ID | CTest name | 合同 |
| --- | --- | --- |
| Q02-SESSION-01..04 | `session_reset.account_state` | 清 user/token、好友/申请/会话 map、联系人/聊天 cursor 与 loading，同时保留应用级服务器配置 |
| Q02-SESSION-05 | `session_reset.owned_ui_and_idempotence` | 销毁旧 session UI/MessageModelStore 所有权树，保留精确 reset reason，重复 reset 无二次通知/释放 |
| Q02-SESSION-06 | `session_reset.pending_batch` | 清旧 pending batch、停止 reset 后新发送，未知旧 failure 不携带 client ID 修改后续 session |

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

Qt runner testcase 合计 24：Unit 18、Component 6。Level 精化不改变生产 Module 目录或断言。

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
不构成完成证据，不改变当前 12 份报告、232 个 runner testcase 或仍为 planned 的 Test ID 统计状态；
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

The following identifiers are frozen by [`PHASE-3B-TEST-PLAN.md`](plans/PHASE-3B-TEST-PLAN.md). They are planned-only and add zero cases to the current 12-report/232-case baseline until a real source, target and emitted JUnit testcase exist.

| Plan | Planned Test IDs | Domain / Level | Planned report owner | Current state |
| --- | --- | --- | --- | --- |
| 3B-00 | `T09-HOST-01..06` | Architecture / Integration | existing `server_integration.xml` | planned; first executable 3B slice |
| 3B-01 | `T09-PROC-01..12` | Architecture / Integration | existing `server_integration.xml` | planned; G-015 process ownership remains open |
| 3B-02 | `T09-GHTTP-01..12` | Architecture/Business / Integration | existing `server_integration.xml` | planned; G-009 real HTTP remains open |
| 3B-02 | `Q04-HTTP-01..10` | Architecture/Business / Integration | `client_integration.xml` after real emission | planned; G-011 real QNAM remains open |
| 3B-03 | `T09-SGRPC-01..12` | Architecture/Business / Integration | existing `server_integration.xml` | planned; G-010 process transport remains open |
| 3B-04 | `T09-CTCP-01..16` | Architecture / Integration | existing `server_integration.xml` | planned; G-008 real TCP remains open |
| 3B-04 | `Q04-TCP-01..12` | Architecture/Business / Integration | `client_integration.xml` after real emission | planned; G-011 real QTcpSocket remains open |
| 3B-05 | `T09-COMP-01..08` | Architecture / Integration | existing `server_integration.xml` plus structure gate | planned; formal composition closeout remains open |

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
| G-015 | 四进程生命周期 | Gate/Status 单进程本地 ready/stop 已覆盖；四发布单元依赖编排、共享状态与业务恢复仍未证明 | Integration | [3B §8 / 3B-01；§10 / 3B-03；§11 / 3B-04；§12 / 3B-05](plans/PHASE-3B-PLAN.md) → [3C §9 / 3C-00；§10 / 3C-01；§11 / 3C-02；§16 / 3C-07；§18 / 3C-09](plans/PHASE-3C-PLAN.md) | master |
| G-016 | 双 ChatServer 业务流 | 跨实例好友/消息/重连/历史没有公开 E2E | E2E | [3C §14 / 3C-05（持久化前置）](plans/PHASE-3C-PLAN.md) → [3D §9..12 / 3D-00..03；§14 / 3D-05（current-N owner）](plans/PHASE-3D-PLAN.md) | master/release |
| G-017 | 版本兼容 | 当前版与上一发布版无自动矩阵 | Compatibility | [3C §12 / 3C-03；§17 / 3C-08；§18 / 3C-09](plans/PHASE-3C-PLAN.md) → [3D §13 / 3D-04；§14 / 3D-05](plans/PHASE-3D-PLAN.md) → [Release §10 / R-01；§12 / R-03](plans/PHASE-RELEASE-PLAN.md) | master |
| G-018 | artifact/UAT | 尚无同产物 smoke 与版本化人工清单 | Release | [Release §9 / R-00；§10 / R-01；§11 / R-02；§12 / R-03](plans/PHASE-RELEASE-PLAN.md) | release |

## 8. 矩阵维护规则

- 新 testcase 合并时必须先分配 Test ID，并更新本矩阵及所属 Module README。
- 删除、合并或改写 Test ID 必须遵守 D-04 合同变更流程。
- `CheckTestStructure` 负责验证测试文件和 runner 注册；本矩阵负责语义、lane 和合同归属，二者不能互相替代。
- 执行证据按 [`CI-GOVERNANCE.md` section 2.1](CI-GOVERNANCE.md#21-比例化执行合同) 分层；本矩阵不要求任务重复
  full lane、secret/residue/diff audit 或无意义 mutation，这些聚合证据只由 phase closeout 收集一次。
- 每次发布后将“上一已发布版本”指针更新到本次 artifact，并保存兼容 fixture 和测试结果。
- 数量变化必须同时说明新增/删除 Test ID、runner testcase 和报告变化，禁止只报告一个总覆盖率百分比。
- 本机 vcpkg 工具/安装目录、manifest 自动安装、restore/install/remove/update/upgrade、triplet/baseline 或 install root
  变化受 DG-25 约束；未取得针对精确命令与目标的批准时必须 fail closed，不得把依赖恢复写成普通测试前置动作。
