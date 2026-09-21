---
phase: 3B
plan: 02
subsystem: gate-http-and-qt-network-integration
tags: [boost-beast, qnetworkaccessmanager, loopback, deadlines, cancellation, junit, dg-25]
requires:
  - Plan 3B-00 shared IntegrationHost and production-target contract
  - Plan 3B-01 run-owned process, port, deadline, and cleanup foundation
  - Phase 3A GateRequest and AuthFlowCoordinator production Modules
  - DG-25 fixed read-only vcpkg roots
provides:
  - Shared GateTransport production target used by GateServer and integration tests
  - Seven real Beast loopback T09-GHTTP contracts in server_integration.xml
  - GateHttpTransport production Qt Module with bounded response, deadline, cancel, generation, and one terminal result
  - Ten real QNetworkAccessManager loopback Q04-HTTP contracts in client_integration.xml
  - Thirteen-report 267-testcase manifest registration
affects: [phase-3b-03, phase-3b-04, phase-3b-05, windows-ci]
tech-stack:
  added: []
  patterns: [shared production transport target, numeric loopback fixture, generation-scoped completion, one-shot terminal result, bounded HTTP body]
key-files:
  created:
    - GateServer/GateServer/GateTransport.vcxproj
    - tests/server/integration-host/gate_http_transport_tests.cpp
    - chat/gatehttptransport.h
    - chat/gatehttptransport.cpp
    - chat/tests/http-transport/http_transport_tests.cpp
    - chat/tests/http-transport/README.md
  modified:
    - GateServer/GateServer/CServer.h
    - GateServer/GateServer/CServer.cpp
    - GateServer/GateServer/HttpConnection.cpp
    - GateServer/GateServer/LogicSystem.cpp
    - chat/httpmgr.h
    - chat/httpmgr.cpp
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - Refused connections map immediately to NetworkError, while DeadlineExceeded means the transport-owned timer won the race.
  - GateServer, Server Component tests, and Server Integration tests link one GateTransport target instead of compiling transport sources privately.
  - HttpMgr consumes GateHttpTransport results; QNetworkAccessManager, QNetworkReply, timers, and generation state remain private to the transport.
  - RunServerTests is the sole owning public runner for this plan; RunAllTests remains reserved for Plan 3B-05.
patterns-established:
  - Every transport operation carries flow and request identity plus a finite deadline and exactly one terminal outcome.
  - Real network errors, explicit cancellation, deadlines, duplicate signals, and late generations are distinct observable contracts.
requirements-completed: []
metrics:
  duration: 3h18m
  completed: 2026-09-06
  tasks: 3
  files_modified: 23
  server_cases: 191
  qt_cases: 34
  regression_manifest_cases: 267
---

# Phase 3B Plan 02: Gate HTTP and Qt QNAM Integration Summary

Shared Beast and QNetworkAccessManager production transports now cross real numeric loopback with bounded bodies, finite deadlines, generation-safe cancellation, one terminal result, and seventeen deterministic HTTP integration cases.

## Performance

- **Duration:** 3h18m from the first RED commit through Task 3 closeout
- **Started:** 2026-09-06T06:20:45Z
- **Completed:** 2026-09-06T09:38:36Z
- **Tasks:** 3/3
- **Plan files changed:** 23 before this Summary

## Accomplishments

- Extracted `CServer`, `HttpConnection`, `LogicSystem`, and Gate response shaping into one `GateTransport.vcxproj` shared by the formal GateServer and tests.
- Added seven T09-GHTTP cases covering endpoint publication, four GateRequest-backed routes, fragmented exact-limit bodies, max+1 rejection, malformed input, interruption, and idempotent cleanup.
- Added a pImpl `GateHttpTransport` used by `HttpMgr`, with an 8 KiB response limit, transport-owned deadline, cancellation/reset, generation protection, and exactly one terminal result.
- Added ten Q04-HTTP cases over real QNetworkAccessManager loopback, then registered `client_integration.xml` and the actual 13-report/267-case manifest.

## Task Commits

Each behavior task retains separate RED and GREEN commits:

1. **Task 1: Share the production Gate HTTP path and prove success/body lifecycle**
   - RED `058769b` - `test(3B-02): add failing Gate HTTP transport contracts`
   - GREEN `79e6b91` - `feat(3B-02): share Gate HTTP transport over loopback`
2. **Task 2: Deepen Qt HTTP into a bounded production transport**
   - RED `3ef34ac` - `test(3B-02): add failing Qt HTTP transport contracts`
   - GREEN `7281cc9` - `feat(3B-02): add bounded Qt Gate HTTP transport`
3. **Task 3: Register reports, counts, structure gates, and owning-runner evidence**
   - `979a0d3` - `chore(3B-02): register HTTP integration evidence`

The required RED-before-GREEN ordering is present in git history. Task 1 RED could not compile the planned production Gate HTTP Interface; Task 2 RED failed at the planned missing `gatehttptransport.h` boundary.

## Contract and Mutation Evidence

- Restored Server focused verification passed T09-GHTTP 7/7; its XML has seven cases and zero failure/error/skipped nodes.
- GateRequest-bypass mutation changed the verification route away from `GateRequest::Handle`; the focused suite failed exactly 1/7, then restoration returned it to 7/7.
- Body-bound mutation widened the Beast parser limit by one byte. `OverLimitMalformedAndInterruptedRequestsNeverDispatch` failed 1/1 because max+1 was neither rejected nor prevented from dispatch. Restoration returned `HttpConnection.cpp` to SHA-256 `955C7E32C2121433956AB65DE503C2547B3AC81B0A2AE39E5992210ACD59D7EF`, followed by a rebuilt 7/7 GREEN run.
- Qt focused CTest passed Q04-HTTP 10/10 and emitted ten Integration cases with zero failure/error/skipped nodes.
- Qt second-completion mutation made `explicitCancelHasExactlyOneTerminalOutcome` fail 1/1 with two results instead of one. Restoration returned `gatehttptransport.cpp` to SHA-256 `1C893D108A2DB0514E951C0363DCFB824CEF044A116BEACF72FAD93FEB9AD6AC`, followed by the full 10/10 GREEN run.
- Closed-port behavior on this Windows/Qt runtime did not deliver a network error before the owned timer. A deterministic real loopback peer therefore accepts and immediately aborts the connection; `errorOccurred` produces `NetworkError`, while a silent accepted peer remains the separate `DeadlineExceeded` contract.

## Final Owning Runner Evidence

After structure and both focused suites were green, the plan's sole owning public runner was invoked exactly once:

`powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg -ServerIntermediateRoot D:\git\Chat\Cache\worktrees\phase-3b-integration-20260904\build\windows-servers-obj`

It returned exit 0. The six emitted Server reports were parsed after completion:

| Report | Cases | Failures | Errors | Skipped |
| --- | ---: | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 | 0 |
| `server_component.xml` | 56 | 0 | 0 | 0 |
| `server_integration.xml` | 59 | 0 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 | 0 |
| **Server total** | **191** | **0** | **0** | **0** |

- `server_integration.xml` contains the seven new T09-GHTTP cases in its exact total of 59.
- `client_integration.xml` contains ten Q04-HTTP cases with zero failures/errors/skips. The registered manifest is 13 reports / 267 cases: Server 191, Qt 34, VarifyServer 29, and PowerShell 13.
- `RunServerTests` intentionally owns only the six Server reports. The full thirteen-report regeneration remains exclusive to Plan 3B-05; no other public runner or `RunAllTests` was invoked here.
- HTTP focused, Server Integration, Qt Integration, and Qt mutation artifacts contained zero password/token/verification-code/email markers.
- The runner command compared recursive path/length/last-write fingerprints before and after for `D:\vcpkg\test-vcpkg` and `D:\git\Chat\vcpkg_installed`; both roots were unchanged, so DG-25 writes were zero.
- Two same-run MSBuild workers briefly remained after the runner shell returned, then exited naturally during the bounded recheck. No task process or run-owned temp residue remained and no process was killed.

## Files Created/Modified

- `GateServer/GateServer/GateTransport.vcxproj` and Gate transport sources - shared formal/test Beast HTTP implementation with bound endpoint, finite parser limit, and observable stop.
- `tests/server/integration-host/gate_http_transport_tests.cpp` - seven T09-GHTTP real-loopback contracts.
- `chat/gatehttptransport.h/.cpp` - private QNAM/reply/timer ownership and typed one-shot transport results.
- `chat/httpmgr.h/.cpp` - GUI manager delegation to the production transport while preserving AuthFlow identity.
- `chat/tests/http-transport/http_transport_tests.cpp` - ten Q04-HTTP real-loopback Integration contracts.
- `scripts/windows-local.ps1`, `tests/TEST-CONTRACT-MATRIX.md`, and Module READMEs - exact report ownership, counts, IDs, and structure enforcement.
- `tests/server/ServerComponentTests.vcxproj` - shared GateTransport reference instead of private GateResponse compilation.

## Decisions Made

- `NetworkError` and `DeadlineExceeded` remain semantically distinct. The former requires an observed QNetworkReply error; the latter means only the transport deadline completed first.
- One shared GateTransport library owns production Gate HTTP sources. Executable, Component, and Integration targets may link it but may not compile private copies.
- Qt's platform transfer timeout was not used as a second competing deadline. The transport timer is the sole deadline authority, while `errorOccurred` reports genuine network failures immediately.
- The current 267 figure is a registered manifest total. This plan's public-runner evidence is the six-report, 191-case Server slice plus the ten-case Qt focused Integration report.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Supplied the correct Qt runtime to focused CTest**

- **Found during:** Task 2 GREEN verification.
- **Issue:** Direct CTest launched with an unrelated MinGW runtime first on PATH, so all Qt cases timed out before exercising the transport.
- **Fix:** Added a target-scoped CTest PATH environment modification that prepends the configured Qt 6.5.3 runtime directory.
- **Verification:** Full Q04-HTTP passed 10/10.
- **Committed in:** `7281cc9`.

**2. [Rule 1 - Bug] Preserved refused-versus-deadline terminal semantics**

- **Found during:** Task 2 refused-connection contract.
- **Issue:** Consuming only `finished` allowed the owned timer to report `DeadlineExceeded` before a genuine reply error was observed on this runtime.
- **Fix:** Connected `QNetworkReply::errorOccurred` to one-shot `NetworkError` completion and used a real accept-then-abort loopback peer for deterministic refusal evidence.
- **Verification:** Refused selector passed 1/1 and full Q04-HTTP passed 10/10; the silent peer still reports `DeadlineExceeded`.
- **Committed in:** `7281cc9`.

**3. [Rule 3 - Blocking] Removed a private GateResponse compile from Component tests**

- **Found during:** Task 3 structure verification after GateTransport extraction.
- **Issue:** `ServerComponentTests.vcxproj` still compiled `GateResponse.cpp` directly, violating the single production-target contract, while old structure regexes expected pre-extraction ownership and call syntax.
- **Fix:** Replaced the direct source with a GateTransport project reference and updated structural assertions for all consumers, object-based GateRequest calls, and typed HttpMgr transport results.
- **Verification:** `CheckTestStructure` passed and the owning Server runner passed 191/191.
- **Committed in:** `979a0d3`.

---

**Total deviations:** 3 auto-fixed (1 product bug, 2 blocking integration issues).

**Impact on plan:** Each change was required to exercise the planned production paths and enforce their ownership or terminal semantics. No external dependency, public endpoint, or later-phase transport was added.

## Issues Encountered

- The restored Task 1 build initially lost its approval stream. No result from that interrupted exchange was accepted; the user freshly approved the identical fixed-scope build/focused command, which returned exit 0 and 7/7.
- A sandboxed MSBuild mutation build could not read the local Windows SDK registry/path. The identical fixed-root command was rerun with explicit elevation and no dependency mutation.
- One preliminary Qt focused command named a nonexistent ctest path and therefore launched no test. The installed `D:\cmake\bin\ctest.exe` was then used for the successful 10/10 run; no failing test result was overwritten.

## User Setup Required

None - no external service, credential, package installation, or public network access is required.

## Known Stubs

None. Both production transport Modules, both test Modules, report registration, and shared target references are non-empty and exercised. Status gRPC, Chat TCP, full-manifest regeneration, and remote CI remain explicitly owned by later 3B plans rather than placeholders here.

## Threat Flags

None. Loopback transport, parser bounds, completion identity, report evidence, and dependency supply-chain boundaries are explicit Plan 3B threat-model surfaces covered by T-3B-02 through T-3B-08 and T-3B-SC.

## Next Phase Readiness

- Plan 3B-02 is complete on the registered 13-report/267-case manifest with authoritative 191/191 Server and 10/10 Qt HTTP evidence.
- Plans 3B-03 and 3B-04 can reuse the same run-owned endpoint, deadline, generation, and one-terminal-outcome patterns.
- Plan 3B-05 still owns the only full `RunAllTests`, complete thirteen-report regeneration, closeout negative probe, clean PR, and remote `develop` evidence.
- No task-owned process, socket, port, temporary directory, dependency-tree mutation, staged file, or unresolved verification failure remains.

## Self-Check: PASSED

- Commits `058769b`, `79e6b91`, `3ef34ac`, `7281cc9`, and `979a0d3` exist in required order on `phase-3b-integration-20260904`.
- All 23 implementation/test/registration files and this Summary exist; no tracked file was deleted.
- T09-GHTTP is 7/7, Q04-HTTP is 10/10, and the sole owning Server runner emitted exactly 191 cases with zero failures/errors/skips.
- Restored production hashes match the recorded body-bound and Qt completion baselines; DG-25 changed roots, run-owned residue, and secret marker counts are all zero.
- Before this Summary commit the staged index was empty and the only remaining working-tree changes were the two explicitly deferred orchestrator-owned planning documents.

---
*Phase: 3B*
*Completed: 2026-09-06*
