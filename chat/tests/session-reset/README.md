# Client authenticated-session reset tests

## Production Module and Interface

`ClientSession::beginSession` and `ClientSession::resetSession` form the owning authenticated-session lifecycle Interface. `MainWindow` registers the real `ChatDialog` as the owned session root; reset delegates connection state to `TcpMgr`, account state to `UserMgr`, and destroys that root so its `ChatPage`, `MessageModelStore`, timers, selection, scroll anchors, avatar cache, and loading flags cannot survive into another account.

`TcpMgr::resetConnection` stops sends, aborts an outstanding connect/socket, clears endpoint state and resets the decoder. Expected logout/kick/account-switch clears pending batches. Unexpected disconnect retains at most 128 original message payloads in memory, bound to their sender UID; the next successful login retries only matching-account batches with unchanged UUIDs. A different authenticated account drops them. Nothing is persisted across application exit. Expected close and unexpected disconnect remain distinct through `SessionResetReason` and `sig_connection_close(bool expectedClose)`.

Domain is Architecture/Business. The five state cases are Component; the authenticated retry case is Integration and uses real ephemeral loopback sockets with the production transport. It compares original/retried frame bytes without implementing another wire parser.

## Contracts

| Test ID | CTest testcase | Contract |
| --- | --- | --- |
| Q02-SESSION-01..04 | `session_reset.account_state` | Logout clears user/token, friend/apply/chat maps, contact and chat cursors/loading state while preserving application-level server configuration. |
| Q02-SESSION-05 | `session_reset.owned_ui_and_idempotence` | Reset destroys the owned session UI/model tree, emits the exact reason once, and repeated reset is a no-op. |
| Q02-SESSION-06 | `session_reset.pending_batch` | Reset removes an old pending text batch and rejects a post-reset send; an old failure therefore carries no client IDs into the next session. |
| Q02-SESSION-07 | `session_reset.uncertainBatchSurvivesDisconnectAndMatchesExactUuid` | Out-of-order replies match exact UUID sets; transient storage errors and malformed success keep pending; valid acknowledgement or terminal conflict removes only the matching batch. |
| Q02-SESSION-08 | `session_reset.retryDoesNotCrossAuthenticatedAccounts` | Account-bound pending is dropped when a different account authenticates. |
| Q02-SESSION-09 | `session_reset.authenticated_wire_retry` | Unexpected socket close retains the original bytes; reconnect does not replay before authentication and reuses the exact UUID/body afterwards. |

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
- Future local cache must be a separate Module keyed by account and schema version; it must not rely on accidental `UserMgr` retention.
- Replay is triggered once per successful matching-account login, not an unbounded timer loop. A legacy error response without UUID correlation never removes an arbitrary FIFO batch. Server-ID model deduplication prevents duplicate rows; no network exactly-once guarantee is implied.
