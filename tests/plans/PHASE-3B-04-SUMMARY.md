---
phase: 3B-integration
plan: 04
subsystem: transport-integration
tags: [boost-asio, tcp, qt, qtcpsocket, framing, generations, junit, tdd, mutation]

requires:
  - phase: 3A-01
    provides: LogicDispatcher accepted/full/closed production contract
  - phase: 3A-03
    provides: ChatSessionState lifecycle and in-memory adapter boundary
  - phase: 3B-01
    provides: run-owned numeric loopback, deadlines, and cleanup conventions
provides:
  - Shared production ChatTransport target used by ChatServer and formal Server tests
  - Sixteen raw-byte T09-CTCP production transport contracts
  - Generation-safe ChatTcpTransport used by TcpMgr with twelve real-QTcpSocket Q04-TCP contracts
  - Exact Server Integration 87, Client Integration 22, and thirteen-report 307-case registrations
affects: [3B-05, G-008, G-011, G-015, server-integration, client-integration]

tech-stack:
  added: [ChatTransport.vcxproj, ChatTcpTransport]
  patterns: [shared production transport target, pImpl-owned Qt transport resources, generation-scoped terminal outcomes, run-owned numeric loopback]

key-files:
  created:
    - ChatServer/ChatServer/ChatTransport.vcxproj
    - chat/chattcptransport.h
    - chat/chattcptransport.cpp
    - tests/server/integration-host/chat_tcp_transport_tests.cpp
    - chat/tests/tcp-transport/tcp_transport_tests.cpp
    - chat/tests/tcp-transport/README.md
  modified:
    - ChatServer/ChatServer/CServer.h
    - ChatServer/ChatServer/CServer.cpp
    - ChatServer/ChatServer/CSession.h
    - ChatServer/ChatServer/CSession.cpp
    - chat/tcpmgr.h
    - chat/tcpmgr.cpp
    - chat/tcpframedecoder.h
    - chat/tcpframedecoder.cpp
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md

key-decisions:
  - "Chat acceptor/session/frame sources have one production owner, ChatTransport.vcxproj; formal and test consumers reference it without compiling private copies."
  - "ChatTcpTransport owns QTcpSocket, TcpFrameDecoder, timers, and write queue behind pImpl while TcpMgr retains only business handlers and acknowledgement identity."
  - "Every Qt terminal result snapshots generation and flow identity before cleanup, so late old-generation events cannot complete a retry."
  - "The regression manifest advances only from emitted cases: Server Integration 87, Client Integration 22, total 307 across thirteen reports."

patterns-established:
  - "Transport integration tests send raw bytes through the exact production codec/session/dispatcher path over run-owned numeric loopback."
  - "Fault tests use hard deadlines and identity-scoped cleanup; no sleeps, fixed ports, public network, or external service adapters."

requirements-completed: []

duration: 1h59m
completed: 2026-09-07
---

# Phase 3B Plan 04: Chat TCP and Qt QTcpSocket Summary

**Shared production Chat TCP ownership plus generation-safe Qt QTcpSocket transport, proven by 16 raw-byte Server and 12 real-socket Client Integration contracts.**

## Performance

- **Duration:** 1h 59m
- **Started:** 2026-09-06T14:29:10.8889146Z
- **Completed:** 2026-09-06T16:27:50.7488545Z
- **Tasks:** 3
- **Files created or modified by commits:** 27
- **Execution branch guard:** `worktree-agent-3b-04-execution`
- **Starting HEAD:** `f2428cf798fcc056833acd6cfa40e1734c2ce40a`

## Accomplishments

- Extracted `ChatFrameCodec`, `CServer`, `CSession`, and `MsgNode` into the single `ChatTransport.vcxproj` production owner. `ChatServer`, `ServerUnitTests`, and `ServerIntegrationTests` formally reference that target; none compiles a private copy.
- Drove raw loopback bytes through the production Chat acceptor/session/frame/dispatcher path for split/coalesced/zero/max/malformed frames, interruptions, backpressure, refusal, deadlines, occupied ports, pending accept, and total lifecycle release.
- Added `ChatTcpTransport`, with real `QTcpSocket`, the existing `TcpFrameDecoder`, finite connect/write deadlines, queued writes, generation/flow identity, and one terminal outcome. `TcpMgr` delegates transport ownership without exposing socket, decoder, queue, map, or timer test surfaces.
- Registered exact emitted counts: T09-CTCP 16 in `server_integration.xml` (87 total), Q04-TCP 12 in `client_integration.xml` (22 total), and 307 cases in the unchanged thirteen-report manifest.

## Task Commits

Each task followed an explicit RED then GREEN gate and was committed atomically:

1. **Task 3B-04-1 RED:** `acd620c03b1a5ea4cfb7b0c00b3e3c127f4ceb0b` — `test(3B-04): add failing Chat TCP stream contracts`
2. **Task 3B-04-1 GREEN:** `0b0e77cc85a78a9603bfb734907161bc763707e5` — `feat(3B-04): share production Chat TCP transport`
3. **Task 3B-04-2 RED:** `fb5454fec39137903ce5373509fbb98b8b7ad3a1` — `test(3B-04): add failing Qt TCP transport contracts`
4. **Task 3B-04-2 GREEN:** `01a338f400bb3d76a18b0b44ea8c996d49f12e3f` — `feat(3B-04): add generation-safe Qt TCP transport`
5. **Task 3B-04-3 RED:** `7c6d30f0a3894e32ee9b363bd33769442674545c` — `test(3B-04): add failing Chat TCP fault lifecycle contracts`
6. **Task 3B-04-3 GREEN:** `3a46727d9749954351aec5e6c17622db179f3b20` — `feat(3B-04): complete Chat and Qt transport fault matrix`

## TDD Gate Evidence

- **Task 1 RED:** T09-CTCP-01..10 were registered before production changes; the build failed with 19 missing-`CServer`-Interface compile errors.
- **Task 1 GREEN:** formal `ChatServer`, `ServerUnitTests`, and `ServerIntegrationTests` shared the new target; T09-CTCP-01..10 passed 10/10.
- **Task 2 RED:** Q04-TCP-01..12 were registered first; the build failed because `chattcptransport.h` did not exist.
- **Task 2 GREEN:** formal `chat.exe` built and Q04-TCP passed 12/12; the preserved auth/session focused set passed 15/15.
- **Task 3 RED:** T09-CTCP-11..16 failed compilation because `CServer::Stopped` did not exist.
- **Task 3 GREEN:** restored production rebuild passed with zero errors; T09-CTCP passed 16/16 and Q04-TCP passed 12/12.

## Mutation Evidence

| Mutation | Precise RED evidence | Restoration evidence |
| --- | --- | --- |
| Chat maximum body bound changed from 2048 to 65535 | `T09-CTCP-07` failed precisely on the max+1 contract (process exit `-1073741819`) | Full T09-CTCP-01..10 returned 10/10; final `Const.h` SHA-256 `396D1DCAFF40161A8F5DDD75203799D3BD2C36BBD142AA517A73CB1259DEB353` |
| Async write deterministically limited to half of each queued node | `T09-CTCP-13` timed out with `525312` bytes received versus `1050624` expected (exit 1) | T09-CTCP returned 16/16; `CSession.cpp` SHA-256 `60FF10B83689A80ACC4669278F63D010572E1C667CB2243502B89983DCF983DE` |
| Terminal cleanup cleared endpoint identity before publishing the outcome | `Q04-TCP-11` failed 0/1 with exit 8 because retry identity was lost | Q04-TCP returned 12/12; `chattcptransport.cpp` SHA-256 `E1E5BCF6611E660E3A0D4548477184EEA1287C950FE4D93F7FB84575B268D154` |
| `CServer::Stop` omitted acceptor cancel/close | `T09-CTCP-15` failed precisely with `Stopped=false` and rebind error (exit 1) | T09-CTCP returned 16/16; `CServer.cpp` SHA-256 `173BE9467FB7A8695E382B872D4865D099AD26FE49B0E5A4AA17277701B37A31` |

Two initially weak mutation forms were deliberately rejected as evidence: `async_write_some` completed each small buffer fully, and the first maximum-bound selector did not observe the changed branch. Both were restored immediately; the deterministic mutations above were then required to fail before GREEN restoration.

## Verification and Reports

### Focused suites

- `T09_CTCP_Stream.*`: 16/16 GREEN, `build/test-results/3b04_chat_stream.xml`, 0 failures.
- `tcp_transport`: 12/12 GREEN with real `QTcpSocket`, 0 failures.
- Combined HTTP+TCP Qt Integration emission: 22/22 GREEN, `build/test-results/client_integration.xml`, 0 failures.
- `CheckTestStructure`: GREEN; 29 Server, 7 Qt, 6 VarifyServer, and 2 PowerShell test source files registered.

### Single owning Server lane

The Plan's only `RunServerTests` invocation returned exit 0 in 324.5 seconds. `RunAllTests` was not invoked.

| JUnit report | Cases | Failures/errors |
| --- | ---: | ---: |
| `server_unit.xml` | 68 | 0 |
| `server_component.xml` | 56 | 0 |
| `server_integration.xml` | 87 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 |
| `server_gate_unit.xml` | 2 | 0 |
| `server_status_unit.xml` | 2 | 0 |

`server_integration.xml` contains all 16 T09-CTCP cases. The runner and matrix preserve the remaining unchanged report owners and require exactly 13 reports / 307 cases when the aggregate lane runs in 3B-05.

## DG-25 Dependency Immutability

Both dependency roots were accessed read-only. Every MSBuild command set `VcpkgManifestInstall=false` and used the fixed installed root; no restore, install, remove, update, upgrade, dependency clean, copy, or junction command ran.

| Root | Pre/post files | Pre/post metadata fingerprint | Pre/post max UTC mtime | Writes |
| --- | ---: | --- | --- | ---: |
| `D:\vcpkg\test-vcpkg` | 15714 | `D98B4CB1B0DF4EC400E3861BCD1A1D4868711688C8A2AC61CA9240CB9874827F` | `2026-08-21T14:14:40.0624754Z` | 0 |
| `D:\git\Chat\vcpkg_installed` | 25731 | `3A06E6B6E266438516C99B5222BB274036B962904AFD575F7D7606181DC7612F` | `2026-08-31T14:07:37.8361570Z` | 0 |

A post-run read initially reported 15686 files for `test-vcpkg` because it omitted `-Force`; the exact 28-file difference was the pre-existing hidden `.git` subtree. Re-running the identical preflight algorithm (`-Force`, relative path + length + UTC ticks, sorted) produced the exact original count, fingerprint, and max mtime shown above.

## Runtime Residue

- Owned build/test/server/process residue: 0.
- Run-owned temp residue for the registered prefixes: 0.
- Test-owned sockets, ports, QObjects, timers, sessions, and threads: released; the dedicated lifecycle cases and runner cleanup checks were GREEN.
- Three unrelated Node processes predating this plan were observed and deliberately left untouched.
- Three MSBuild node-reuse workers left by the owning runner were confirmed by command line, parent PID, and start time, then terminated exactly; post-cleanup worker residue was 0.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 3 - Blocking] Boost timer cancellation API mismatch (Task 1).** Corrected the production stop path to the installed Boost API and rebuilt.
2. **[Rule 1 - Bug] Global `CServer` ODR collision (Task 1).** Placed the Chat acceptor in `chat_transport` and updated formal callers, preventing collision with the Gate `CServer` in shared Integration tests.
3. **[Rule 3 - Blocking] ServerUnit duplicate transport ownership (Task 1).** Replaced direct Chat transport compilation with the formal `ChatTransport.vcxproj` reference.
4. **[Rule 1 - Bug] Qt peer read-throttle fixture did not deterministically create backpressure (Task 2).** Made the run-owned peer throttle reads under an owned deadline.
5. **[Rule 1 - Portability] Closed-loopback connect classified by Windows/Qt timing (Task 2).** Accepted the two valid bounded pre-connect outcomes while retaining exact generation/flow and one-terminal assertions.
6. **[Rule 3 - Blocking] Formal app AUTOMOC duplicated transport ownership (Task 2).** Kept MOC/source ownership in the shared production target and linked the app/tests to it.
7. **[Rule 1 - Test synchronization] Backpressure assertion raced the responder enqueue (Task 3).** Added a bounded yield until all 512 frames were accepted before reading them; no sleep was introduced.
8. **[Rule 1 - Structure guard] Decoder reset guard used the wrong pImpl member spelling (Task 3).** Required production `decoder.reset()` and `TcpMgr::_transport.reset()` instead of the retired direct decoder field.
9. **[Rule 3 - Verification environment] Scoped rebuild used the client triplet and a tool timeout left compiler workers (Task 3).** Corrected to `x64-windows-chat`, verified the orphan ownership, terminated only those owned workers, and completed the fixed rebuild.

**Total deviations:** 9 auto-fixed (4 blocking/environment, 4 correctness/test, 1 platform classification). No external dependency or architectural scope was added.

## Issues Encountered

- The first scoped restored build used `x64-windows` and failed before Task 3 compilation because Server headers live in `x64-windows-chat`. The corrected command succeeded.
- A 60-second tool timeout orphaned an MSBuild/compiler child and caused transient PDB-lock failures. The exact child was identified by start time and ownership, stopped, and the fixed build completed with `/nr:false`; this did not affect the single owning runner invocation.
- The owning runner had no test failure. Approval remained connected; no checkpoint was triggered.

## Known Stubs

None. Added and modified production paths have real data sources and no placeholder/TODO/FIXME behavior introduced by this plan.

## Threat Surface

No unplanned threat surface was introduced. The new TCP surfaces are the explicit T-3B-04 transport boundary and are limited to numeric loopback in tests, validated size bounds, generation-scoped completion, hard deadlines, sanitized diagnostics, and owned teardown.

## User Setup Required

None. No credentials, external services, dependency installation, or public network access is required.

## Next Phase Readiness

- 3B-05 can consume the shared Server and Qt transport Modules for formal composition closeout.
- The thirteen-report manifest now expects 307 cases; the single complete aggregate lane remains intentionally owned by 3B-05.
- No blockers remain.

## Self-Check: PASSED

- All six RED/GREEN task commits exist on `worktree-agent-3b-04-execution` in the required order.
- Every created key file exists, every mutation was restored, focused Server 16/16 and Qt 12/12 are GREEN, and the sole owning Server lane is GREEN.
- DG-25 fingerprints are unchanged, owned runtime residue is zero, staged state is empty, and only the two orchestrator-owned deferred plan documents remain modified before this Summary commit.

---
*Phase: 3B-integration / Plan 04*
*Completed: 2026-09-07*
