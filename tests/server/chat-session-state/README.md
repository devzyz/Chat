# Chat Session runtime contracts

Production: `ChatSessionRuntime.vcxproj`, shared by ChatServer and the quick test executables.
The runtime is CSession (strand/FSM/I/O), CServer (accept/ownership), UserSessionDirectory
(weak current mapping), and SessionLifecycleCoordinator (asynchronous presence/replacement).
Tests inject an in-memory UserPresenceStore; no personal Redis/MySQL or service configuration is used.

## Component contracts (10, server_component.xml)

T08-SESSION-01..06 cover identity, directory registration/replacement, conditional deletion,
first-close-wins, and concurrent close. The remaining cases cover weak directory ownership,
Created/Closing send rejection, closed authentication rejection, and Close-before-Start.

Migration approved with the Session redesign: the old opaque handle and synchronous Closed send
result are retired. Admission now completes on the strand with Accepted/Full/NotActive.
Old FIFO (T08-SESSION-07), capacity (08), and write-failure (10) contracts move to production
TCP tests T09-CTCP-04, 05, and 13 respectively; they are not removed or tested through a second FSM.

## Loopback Integration contracts (13, server_integration.xml)

| ID | Contract |
| --- | --- |
| T09-CTCP-01 | Split header/body, adjacent frames, empty body, maximum receive body |
| T09-CTCP-02 | Oversized body rejected before dispatch |
| T09-CTCP-03 | Repeated Start creates only one read chain |
| T09-CTCP-04 | FIFO writes and admission semantics |
| T09-CTCP-05 | Exact MAX_SENDQUE capacity, close during write, buffer lifetime |
| T09-CTCP-06 | Replacement, stale kick, old close cannot erase new presence; UID cannot change |
| T09-CTCP-07 | Targeted kick during publication cancels binding and rolls back presence |
| T09-CTCP-08 | Storage unavailable cannot authenticate or register locally |
| T09-CTCP-09 | Slow presence cleanup does not block local close/send rejection |
| T09-CTCP-10 | Remote kick carries the previous Session ID |
| T09-CTCP-11 | Interrupted body read cannot dispatch or restart after close |
| T09-CTCP-12 | Stop drains accept/I/O, releases Session ownership and listening port |
| T09-CTCP-13 | Actual write failure closes and cleans matching presence once |

Both I/O threads run the same context. Futures/barriers have three-second deadlines; fixtures
cancel sockets/timers, drain lifecycle tasks, and join threads. Tests execute real Asio socket
operations; storage is a fake and does not prove Redis Lua or real cross-process replacement.

Owning quick runner (not RunAllTests):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release
```

The Server family contains 179 cases: 68 Unit, 56 Component, 47 main Integration,
4 Chat gRPC, and 2 each Gate/Status lifecycle. No full E2E or external dependency lane is included.
