# 当前自动测试合同矩阵

状态：P0 基线清单
基线日期：2026-08-29
治理规则：[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md)

## 1. 统计口径

当前本地基线包含 173 个 runner testcase：Server 119、Qt 12、VarifyServer 29、PowerShell 13。

Test ID 表示被保护的 Interface 合同，runner testcase 表示测试框架实际报告的用例。二者不必一一对应：例如 Qt `network_state_tests` 是一个 runner testcase，但同时保护拆分 header、拆分 body 和相邻 frame 三个合同。覆盖率、testcase 数量和合同数量必须分别报告，不能相互替代。

当前所有 173 个 testcase 都属于 `develop` 全量快速门禁；`master` 和 release 继承它们。真实 Redis/MySQL/SMTP 与业务 E2E 尚不存在，因此不在当前 173 个基线中。

Plan 2.5-07 本地证据：`RunAllTests -Configuration Release` 已在同一工作树生成并通过下表全部
12 份报告；`CheckTestReports` 对每份报告执行存在性、精确数量、failure/error 节点检查并核验
总数 173。Server、Qt、VarifyServer、PowerShell 各临时移走一份报告时该门禁均返回非零，恢复后
返回 GREEN。本次运行未新增受管测试临时目录或受管进程。clean PR、远端 artifact 和
`develop` branch protection 不属于这份本地证据，必须由实际 GitHub check 另行证明。

## 2. Suite 与报告

| Suite | Module | Domain | Current Level | Testcases | Report | 当前依赖 | Lane |
| --- | --- | --- | --- | ---: | --- | --- | --- |
| Server unit | config、messaging、transport、protocol、rpc、Chat lifecycle | Foundation | Unit | 53 | `server_unit.xml` | in-process；临时文件；旧 wire fixture | develop required |
| Gate Asio | Gate lifecycle | Foundation | Unit | 2 | `server_gate_unit.xml` | in-process thread/io_context | develop required |
| Status Asio | Status lifecycle | Foundation | Unit | 2 | `server_status_unit.xml` | in-process thread/io_context | develop required |
| Server component | Chat Redis pool、Gate response | Foundation/Architecture | Component | 24 | `server_component.xml` | in-process fake；不连接 Redis/MySQL/Status/Varify | develop required |
| Server integration | Chat/Gate/Status startup、C++→Node Varify、Gate gRPC clients | Architecture | Integration | 34 | `server_integration.xml` | 受控子进程、动态 loopback 端口 | develop required |
| Chat gRPC integration | Chat production gRPC clients | Architecture | Integration | 4 | `server_chat_grpc_integration.xml` | 动态 loopback 端口、无外部服务 | develop required |
| Qt unit | frame decoder、message model rules Q01-MODEL-01..06 | Foundation/Business | Unit | 7 | `client_unit.xml` | Qt Core/Widgets minimal，无 socket | develop required |
| Qt component | message store/delegate Q01-MODEL-07..08、session reset Q02-SESSION-01..06 | Architecture/Business | Component | 5 | `client_component.xml` | Qt Widgets/Network minimal；真实 in-process Module，无连接 | develop required |
| Varify unit | protocol、handler、fake startup | Foundation/Business/Architecture | Unit | 18 | `varify_unit.xml` | in-process fake；descriptor/fixture | develop required |
| Varify integration | config、loopback RPC、process startup | Foundation/Architecture | Integration | 11 | `varify_integration.xml` | 子进程或动态 loopback | develop required |
| Script component | instance validation | Architecture | Component | 9 | `script_component.xml` | 临时目录、占位进程 | develop required |
| Script integration | instance lifecycle | Architecture | Integration | 4 | `script_integration.xml` | 受控子进程/PID identity | develop required |
| **合计** |  |  |  | **173** | 12 份报告 | 无个人服务或凭据 |  |

PowerShell 报告逐项输出 13 个 testcase：validation 9 个、lifecycle 4 个；控制台同步保留逐 Test ID 的 PASS/FAIL。该报告粒度改进不改变本表的逻辑基线数量。

## 3. Server testcase 目录（119）

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

### 3.6 Peer routing（2）

Interface：配置 peer section 到运行时名称和 endpoint 的映射。
Domain/Level：Foundation / Unit。
报告：`server_unit.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T05-ROUTE-01 | `ConfiguredSectionsResolveToRuntimeNamesAndAddresses` | 多个 section 映射到运行时名称和地址 |
| T05-ROUTE-02 | `MissingRuntimeNameDoesNotCreateARoute` | 缺少运行时名称时不产生不可用 route |

### 3.7 gRPC client runtime、真实连接池与远端 Adapter（26）

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

### 3.8 Redis pool without service（3）

Interface：Chat `RedisConnectionPool` 的有限借用和关闭。
Domain/Level：Foundation / Component。
报告：`server_component.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T04-RDS-01 | `CloseWakesAWaitingBorrowerWithoutAServiceConnection` | close 唤醒有限等待 borrower |
| T04-RDS-02 | `CloseIsIdempotentAndFutureBorrowsFailImmediately` | close 幂等，关闭后借用立即失败 |
| T04-RDS-03 | `ExhaustedBorrowReturnsWhenItsFiniteWaitExpires` | 耗尽借用在生产超时后返回 |

### 3.9 Gate response allowlist（21）

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

### 3.10 ChatServer startup process（8）

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

### 3.11 C++ → Node Varify loopback（1）

Interface：当前生成的 C++ `VarifyService::Stub` 与 Node 生产 `createServer` 的真实 gRPC wire 互操作。
Domain/Level：Architecture / Integration。
报告：`server_integration.xml`。

| Test ID | Runner testcase | 合同 |
| --- | --- | --- |
| T05-GRPC-02 | `CrossLanguageProtocolTests.CppClientCallsNodeVarifyOnDynamicLoopbackPort` | `127.0.0.1:0` 动态端口、2 秒 client deadline、所有结果清理 Node process/server/pipe |

### 3.12 Gate/Status startup process（21）

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

## 4. Qt testcase 目录（12）

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

Qt runner testcase 合计 12：Unit 7、Component 5。Level 精化不改变生产 Module 目录或断言。

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

| Gap ID | Module / Interface | 当前风险 | 目标 Level | 计划归属 | 目标 lane |
| --- | --- | --- | --- | --- | --- |
| G-001 | Gate/Status CLI 与 Config | 已由 Plan 2.5-04 关闭：真实 EXE CLI/config/bind/ready/stop，15 秒 startup、5 秒 stop 与 scoped PID cleanup | Integration | Phase 2.5-04 complete | develop |
| G-002 | 跨服务 protobuf | 已由 Plan 2.5-02 关闭：canonical Module、初始 descriptor、旧 wire fixture、生成漂移与 C++→Node loopback | Unit/Integration | Phase 2.5-02 complete | develop |
| G-003 | C++ gRPC clients/pools | 已由 Plan 2.5-05 关闭：统一有限 deadline、实际池生命周期、动态 loopback failure mapping、启动配置边界与共享生产库 | Unit/Integration | Phase 2.5-05 complete | develop |
| G-004 | Gate 响应脱敏 | 已由 Plan 2.5-03 关闭：四端点精确 allowlist、稳定 JSON/业务/RPC/异常 envelope、响应与日志 secret regression | Component | Phase 2.5-03 complete | develop |
| G-005 | Qt connection/session reset | 已由 Plan 2.5-06 关闭：decoder/reconnect reset、pending batch、账号 transient state、owned 页面/MessageModelStore 销毁、幂等与关闭分类 | Unit/Component | Phase 2.5-06 complete | develop |
| G-006 | PowerShell JUnit | 已由 Plan 2.5-01 关闭：13 个逻辑用例逐 Test ID 呈现 | reporting | Phase 2.5-01 complete | develop |
| G-007 | Chat Logic dispatcher | FIFO、容量、shutdown 与未知 ID 未保护 | Unit | Phase 3A | develop |
| G-008 | Chat CSession/session registry | 身份、发送顺序、并发关闭和旧 session 清理未保护 | Component/Integration | Phase 3A/3B | develop |
| G-009 | Gate request Module | 注册/登录/重置与依赖错误编排未保护 | Component/Integration | Phase 3A/3B | develop |
| G-010 | Status selector/token | 当前选择策略未定，空列表和 token 写失败未保护 | Unit/Component | Phase 3A | develop |
| G-011 | Qt UserMgr/TcpMgr/HttpMgr/UI state | 网络结果到客户端状态和页面转换未保护 | Unit/Component/Integration | Phase 3A/3B | develop |
| G-012 | Redis Adapters | 真实命令、断线、锁和 TTL 未证明 | Integration | Phase 3C | master |
| G-013 | MySQL Adapters | 无可重复 schema/migration/事务测试 | Integration | Phase 3C | master |
| G-014 | SMTP Adapter | 真实发送参数和错误映射未证明 | Integration | Phase 3C | master |
| G-015 | 四进程生命周期 | Gate/Status 单进程本地 ready/stop 已覆盖；四发布单元依赖编排、共享状态与业务恢复仍未证明 | Integration | Phase 3B/3C | master |
| G-016 | 双 ChatServer 业务流 | 跨实例好友/消息/重连/历史没有公开 E2E | E2E | Phase 3D | master/release |
| G-017 | 版本兼容 | 当前版与上一发布版无自动矩阵 | Compatibility | Phase 3C/3D | master |
| G-018 | artifact/UAT | 尚无同产物 smoke 与版本化人工清单 | Release | Release gate | release |

## 8. 矩阵维护规则

- 新 testcase 合并时必须先分配 Test ID，并更新本矩阵及所属 Module README。
- 删除、合并或改写 Test ID 必须遵守 D-04 合同变更流程。
- `CheckTestStructure` 负责验证测试文件和 runner 注册；本矩阵负责语义、lane 和合同归属，二者不能互相替代。
- 每次发布后将“上一已发布版本”指针更新到本次 artifact，并保存兼容 fixture 和测试结果。
- 数量变化必须同时说明新增/删除 Test ID、runner testcase 和报告变化，禁止只报告一个总覆盖率百分比。
