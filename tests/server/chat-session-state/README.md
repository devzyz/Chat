# Chat session state contracts

Production ownership: `ChatServer/ChatServer/ChatSessionState.h`.

The single caller-facing Interface is `ChatSessionState::Create`,
`RegisterCurrent`, `FindCurrent`, `AuthenticatedUid`, `Close`, and `Send`. Its opaque handle hides
the session ID, UID mapping, send queue, active writer, and closed state.

`SessionWriter` and `SessionPresence` are internal seams with both production
and deterministic in-memory Adapters. Tests use a fixed ID source, a manually
completed writer, and a presence recorder. No test connects to TCP or Redis.
The production writer reuses `SendNode` and `boost::asio::async_write`; the
production presence Adapter delegates registration and session/IP cleanup to
the existing Redis manager. UID/session matching remains inside the Module.

| Test ID | Domain | Level | Contract |
| --- | --- | --- | --- |
| T08-SESSION-01 | Architecture | Component | New handles are non-empty and unique |
| T08-SESSION-02 | Architecture | Component | First registration becomes the UID's current session |
| T08-SESSION-03 | Architecture | Component | A new session atomically replaces the UID's current session |
| T08-SESSION-04 | Architecture | Component | Closing a replaced session cannot delete the replacement |
| T08-SESSION-05 | Architecture | Component | Closing current cleans up once; repeat close is idempotent |
| T08-SESSION-06 | Architecture | Component | Barrier-synchronized concurrent close cleans up at most once |
| T08-SESSION-07 | Architecture | Component | Accepted frames advance through the writer in FIFO order |
| T08-SESSION-08 | Architecture | Component | Exactly MAX_SENDQUE frames are accepted; the next is Full without overwrite |
| T08-SESSION-09 | Architecture | Component | Close rejects new frames immediately |
| T08-SESSION-10 | Architecture | Component | Writer failure closes and matching-cleans exactly once |
| T08-SESSION-11 | Architecture | Component | Only the current live handle owned by this state authenticates; closed sessions cannot re-register |

The owning public runner is
`scripts/windows-local.ps1 -Task RunServerTests -Configuration Release`. The
owning report is `server_component.xml`; report counts are defined by the public
runner. Every wait is bounded by two
seconds and each test leaves no callback or thread pending. Real TCP partial
writes, peer disconnects, and process cleanup remain Phase 3B; real Redis
presence commands, locks, and TTL remain Phase 3C.
