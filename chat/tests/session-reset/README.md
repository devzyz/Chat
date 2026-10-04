# Client authenticated-session reset tests

## Production Module and Interface

`ClientSession::beginSession` and `ClientSession::resetSession` form the owning authenticated-session lifecycle Interface. `MainWindow` registers the real `ChatDialog` as the owned session root; reset delegates connection state to `TcpMgr`, account state to `UserMgr`, and destroys that root so its `ChatPage`, `MessageModelStore`, timers, selection, scroll anchors, avatar cache, and loading flags cannot survive into another account.

`TcpMgr::resetConnection` stops sends and MessageService, then resets the transport and endpoint state. MessageService/SQLite owns account-scoped outgoing batches: explicit logout pauses them; unexpected disconnect preserves uncertain attempts for verification after authentication. Retries keep the original UUID/business payload and increment `attempt_id`. Expected close and unexpected disconnect remain distinct through `SessionResetReason` and `connectionClosed(bool expectedClose)`; see [message states](../../../docs/MessageStates.md).

Domain is Architecture/Business. The five state cases are Component; the authenticated retry case is Integration and uses real ephemeral loopback sockets with the production transport. It compares UUID/business payload and attempt metadata without implementing another wire parser.

## Contracts

| Test ID | CTest testcase | Contract |
| --- | --- | --- |
| Q02-SESSION-01..04 | `session_reset.account_state` | Logout clears user/token, friend/apply/chat maps, contact and chat cursors/loading state while preserving application-level server configuration. |
| Q02-SESSION-05 | `session_reset.owned_ui_and_idempotence` | Reset destroys the owned session UI/model tree, emits the exact reason once, and repeated reset is a no-op. |
| Q02-SESSION-06 | `session_reset.pending_batch` | Reset removes an old pending text batch and rejects a post-reset send; an old failure therefore carries no client IDs into the next session. |
| Q02-SESSION-07 | `session_reset.uncertainBatchSurvivesDisconnectAndMatchesExactUuid` | Out-of-order replies match exact UUID sets; transient storage errors and malformed success keep pending; valid acknowledgement or terminal conflict removes only the matching batch. |
| Q02-SESSION-08 | `session_reset.retryDoesNotCrossAuthenticatedAccounts` | Account-bound pending is dropped when a different account authenticates. |
| Q02-SESSION-09 | `session_reset.authenticated_wire_retry` | After reconnect and authentication, sync verification precedes retry with the same UUID/business payload and an incremented attempt ID. |
| Q02-SESSION-11 | `session_reset.accepted_submission` | 暂时断线保留已接受条目；SQL 已排队但回调未送达时保持原 UUID，恢复重试无重复，账号结束销毁任务。 |
| Q02-SESSION-12 | `session_reset.silent_peer` | 真实 TCP 对端可写但只给无效心跳回复时触发三十秒期限和有限恢复；成功码必须是数值零且连接已认证。 |

The decoder's old-half-frame isolation is T07-FRM-04 in the adjacent network-state Unit module. Together these contracts cover active logout/switch-account, kicked, and abnormal-disconnect reset semantics without a `clearForTest`, test-only build flag, copied state object, fixed port, public network, or credential.

## Execution and evidence

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -R "^session_reset\\." --output-on-failure
```

State cases are written to `build/test-results/client_component.xml`; real socket cases go to `client_integration.xml`. Counts are defined by the public runner. The social lifecycle case has a 30-second hard timeout; the silent-peer case has a 55-second timeout to cover the ten-second heartbeat interval plus thirty-second response deadline. Other cases use a 10-second timeout. All use `QT_QPA_PLATFORM=minimal`.

RED evidence was recorded for the missing owning Module/User reset, retained owned UI, retained pending batch, and unsafe no-user UID read. GREEN uses the same `chat_session_core` library linked by the production executable. The regression mutation removes the `ClientSession` reset call from `MainWindow`; `CheckTestStructure` and the session wiring gate must reject it.

## Deliberate retention and remaining gaps

- Retained: theme/style, window policy, and Gate/server configuration because they are application-level rather than account-level.
- Local storage is isolated by account and schema version; it does not rely on `UserMgr` retaining an in-memory queue.
- Retry follows MessageService's bounded attempt policy. UUID/attempt correlation prevents a late failure from removing another batch; model deduplication does not imply network exactly-once delivery.

`session_reset.account_state` 同时通过生产 `TcpMgr::handleMessage` 处理好友申请与审批回包，
验证目录提交后缓存更新、重复审批不重复联系人，以及清空内存后从原账号库恢复申请状态和会话。
该场景使用临时 SQLite，不访问真实服务。

`session_reset.social_lifecycle`（Q06-SOCIAL-02，Component）通过生产 `TcpMgr::handleMessage` 与临时 SQLite
验证带版本资料/联系人多页落盘、关系就绪不受申请目录失败阻断、错误编号及类型、重复回包、真实十秒超时、
接收者销毁、重复重置及换账号后的迟到结果隔离。该回归使用合成服务响应，不冒充真实网络端到端。

社交能力协商后，旧好友审批响应仍须准确发出一次 `requestCompleted`，同时刷新权威目录；成功和失败均有 Component 回归，真实双实例旅程验证模型最终一致。

## Authentication recovery and revocation

Q02-SESSION-09 now drives ClientSession's automatic reconnect after an actual TCP disconnect,
verifies reauthentication, preserved draft ownership, and replay of the original message UUID.
Q02-SESSION-10 (`session_reset.logout_revocation`) uses real loopback HTTP to verify that failed
revocation preserves account state, retry remains possible, and only confirmed revocation resets
it. Server-side revocation, renewal and reset races are owned by the hosted four-process suite.

`socialRequestsRespectSessionLifecycle` 还通过生产目录回包→SQLite→UserMgr 验证申请人的
姓名、头像和性别转换，防止向同一个 QJsonObject 插入键使 QJsonValueRef 读错字段。
