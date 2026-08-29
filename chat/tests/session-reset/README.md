# Client authenticated-session reset tests

## Production Module and Interface

`ClientSession::beginSession` and `ClientSession::resetSession` form the owning authenticated-session lifecycle Interface. `MainWindow` registers the real `ChatDialog` as the owned session root; reset delegates connection state to `TcpMgr`, account state to `UserMgr`, and destroys that root so its `ChatPage`, `MessageModelStore`, timers, selection, scroll anchors, avatar cache, and loading flags cannot survive into another account.

`TcpMgr::resetConnection` is the connection lifecycle Interface. It stops new sends, aborts an outstanding connect/socket, clears endpoint state, resets the `TcpFrameDecoder`, and removes pending text batches. Expected close and unexpected disconnect remain distinct through `SessionResetReason` and `sig_connection_close(bool expectedClose)`.

Domain is Architecture/Business and Level is Component. These tests compose the real in-process production Modules and Qt signals, but never establish a real `QTcpSocket` connection, so they are not Integration tests.

## Contracts

| Test ID | CTest testcase | Contract |
| --- | --- | --- |
| Q02-SESSION-01..04 | `session_reset.account_state` | Logout clears user/token, friend/apply/chat maps, contact and chat cursors/loading state while preserving application-level server configuration. |
| Q02-SESSION-05 | `session_reset.owned_ui_and_idempotence` | Reset destroys the owned session UI/model tree, emits the exact reason once, and repeated reset is a no-op. |
| Q02-SESSION-06 | `session_reset.pending_batch` | Reset removes an old pending text batch and rejects a post-reset send; an old failure therefore carries no client IDs into the next session. |

The decoder's old-half-frame isolation is T07-FRM-04 in the adjacent network-state Unit module. Together these contracts cover active logout/switch-account, kicked, and abnormal-disconnect reset semantics without a `clearForTest`, test-only build flag, copied state object, fixed port, public network, or credential.

## Execution and evidence

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -R "^session_reset\\." --output-on-failure
```

The three cases are written to `build/test-results/client_component.xml`; the unchanged seven Unit cases remain in `client_unit.xml`. Every CTest case has a 10-second hard timeout and uses `QT_QPA_PLATFORM=minimal`.

RED evidence was recorded for the missing owning Module/User reset, retained owned UI, retained pending batch, and unsafe no-user UID read. GREEN uses the same `chat_session_core` library linked by the production executable. The regression mutation removes the `ClientSession` reset call from `MainWindow`; `CheckTestStructure` and the session wiring gate must reject it.

## Deliberate retention and remaining gaps

- Retained: theme/style, window policy, and Gate/server configuration because they are application-level rather than account-level.
- Future local cache must be a separate Module keyed by account and schema version; it must not rely on accidental `UserMgr` retention.
- A real socket timing/reconnect test would be Integration and must receive a separate `client_integration.xml`; no such dependency is claimed here.
