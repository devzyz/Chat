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

The decoder's old-half-frame isolation is T07-FRM-04 in the adjacent network-state Unit module. Together these contracts cover active logout/switch-account, kicked, and abnormal-disconnect reset semantics without a `clearForTest`, test-only build flag, copied state object, fixed port, public network, or credential.

## Execution and evidence

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -R "^session_reset\\." --output-on-failure
```

State cases are written to `build/test-results/client_component.xml`; the real retry case goes to `client_integration.xml`. Counts are defined by the public runner. Every case has a 10-second hard timeout and uses `QT_QPA_PLATFORM=minimal`.

RED evidence was recorded for the missing owning Module/User reset, retained owned UI, retained pending batch, and unsafe no-user UID read. GREEN uses the same `chat_session_core` library linked by the production executable. The regression mutation removes the `ClientSession` reset call from `MainWindow`; `CheckTestStructure` and the session wiring gate must reject it.

## Deliberate retention and remaining gaps

- Retained: theme/style, window policy, and Gate/server configuration because they are application-level rather than account-level.
- Local storage is isolated by account and schema version; it does not rely on `UserMgr` retaining an in-memory queue.
- Retry follows MessageService's bounded attempt policy. UUID/attempt correlation prevents a late failure from removing another batch; model deduplication does not imply network exactly-once delivery.
