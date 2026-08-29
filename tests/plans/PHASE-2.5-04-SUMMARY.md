---
phase: "2.5"
plan: "04"
subsystem: server-startup-lifecycle
tags: [gate, status, cli, config, grpc, http, process, integration, windows]
requires:
  - phase: "2.5-01"
    provides: layered JUnit reports, exact report guards, and Module ownership checks
provides:
  - Strict Gate/Status CLI and validated CLI-over-environment-over-cwd configuration selection
  - Real HTTP/gRPC process readiness, bind-failure, graceful-stop, and port-release contracts
  - Owned Windows process-group cleanup with 15-second startup and 5-second stop limits
affects: [phase-2.5, gate-startup, status-startup, clean-ci]
tech-stack:
  added: []
  patterns: [real-exe black-box integration, protocol readiness probe, owned-process identity cleanup, joined signal owner]
key-files:
  created:
    - tests/server/startup/gate_status_startup_tests.cpp
    - tests/plans/PHASE-2.5-04-SUMMARY.md
  modified:
    - GateServer/GateServer/GateServer.cpp
    - GateServer/GateServer/ConfigMgr.cpp
    - GateServer/GateServer/CServer.h
    - GateServer/GateServer/CServer.cpp
    - StatusServer/StatusServer/StatusServer.cpp
    - StatusServer/StatusServer/ConfigMgr.cpp
    - tests/server/ServerIntegrationTests.vcxproj
    - scripts/windows-local.ps1
    - tests/server/startup/README.md
    - tests/server/README.md
    - tests/README.md
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - "Gate/Status startup tests use the real production EXEs and real HTTP/gRPC probes; no test-only Interface, parser copy, clearForTest, or production switch was added."
  - "Each service-owned ConfigMgr validates its own schema; a shared CLI Module was not extracted because the black-box tests do not reuse a production parsing Interface."
  - "Windows graceful stop uses CREATE_NEW_PROCESS_GROUP plus scoped CTRL_BREAK_EVENT, mapped by Boost.Asio through SIGBREAK; Linux and Windows Service control remain separate platform verification obligations."
patterns-established:
  - "Process Integration cleanup verifies the held process handle, PID, and executable image before any fallback termination."
  - "Ready means an HTTP response or connected gRPC transport after successful bind, never only a live PID or log line."
requirements-completed: [G-001]
duration: 38m
completed: 2026-08-27
---

# Phase 2.5 Plan 04: Gate/Status Startup and Shutdown Contracts Summary

Gate and Status now fail fast on malformed startup input, report ready only after real local protocol availability, and stop through a bounded Windows console-control path that joins owners and releases listener ports.

## Performance

- **Started:** approximately 2026-08-27T09:29:00+08:00
- **Completed:** 2026-08-27T10:07:00+08:00
- **Duration:** 38 minutes
- **Tasks:** 4 vertical slices completed
- **Test delta:** 122 to 142 total; 71 to 91 Server

## Accomplishments

- Made Gate and Status reject missing `--config` values and unknown arguments with service-specific usage and nonzero exit.
- Preserved and black-box verified configuration precedence: CLI, `CHAT_CONFIG`, cwd `config.ini`.
- Added service-owned validation for required listeners, remote endpoints, logging fields, normalized ports, and Status chat-server entries before listeners, threads, or remote Adapter calls.
- Replaced Gate's silent `atoi` port conversion with validated conversion; bind happens before its worker pool is created.
- Made Gate explicitly stop its acceptor and Asio pool, suppress accept restart after cancellation, and log completed shutdown.
- Made Status reject null/zero-port `BuildAndStart`, retain and join its signal thread, and unwind server/thread ownership on exceptions.
- Added 20 real-process Integration cases using dynamic loopback ports, real HTTP/gRPC readiness, scoped Windows console process groups, 15-second startup limits, 5-second stop limits, captured stdout/stderr, and verified process identity.
- Wired Gate and Status production builds/app-local deployment plus the exact 28-case Integration count into the public Server runner and structural gate.

## TDD RED/GREEN Evidence

### Production process contracts

- **RED:** the new 20-case focused suite ran in 34.2 seconds with 2 passing occupied-bind cases and 18 failures.
- Missing/unknown CLI cases lacked usage; configuration failures lacked stable stderr diagnostics; Gate invalid-port and missing-endpoint cases reached the 15-second timeout; Status delegated malformed listener diagnostics to gRPC; both normal processes exited with the Windows control exception code instead of graceful success.
- **GREEN:** after the production owner changes, the same 20 cases passed in 3.1 seconds. A final cleanup-hardening rebuild and rerun also passed 20/20.

### Runner structure

- **RED:** after adding the ownership guard, `CheckTestStructure` failed with `RunServerTests must build and deploy GateServer for its process Integration contracts.`
- **GREEN:** after runner wiring, the structure check passed with 11 Server, 2 Qt, 6 VarifyServer, and 2 PowerShell sources, and requires the Integration runner's exact count of 28.

No existing test was deleted, relabelled, weakened, or retried for an unexplained green result.

## Testcase and Report Changes

| Report | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Server Unit | 35 | 35 | 0 |
| Server Component | 24 | 24 | 0 |
| Server Integration | 8 | 28 | +20 |
| Gate lifecycle Unit | 2 | 2 | 0 |
| Status lifecycle Unit | 2 | 2 | 0 |
| Qt Unit / Component | 7 / 2 | 7 / 2 | 0 |
| Varify Unit / Integration | 18 / 11 | 18 / 11 | 0 |
| PowerShell Component / Integration | 9 / 4 | 9 / 4 | 0 |
| **Total** | **122** | **142** | **+20** |

The final eleven-report audit found exactly 142 testcase elements, zero failure elements, and zero error elements.

## Windows Termination Mechanism and Limits

The test creates each child with `CREATE_NEW_PROCESS_GROUP`, retains the returned process handle, and sends `CTRL_BREAK_EVENT` only to the child's PID/process-group ID. Gate and Status register `SIGBREAK` with their existing Boost.Asio signal sets, so the Windows console event enters the same production shutdown callback as `SIGINT`/`SIGTERM`.

Fallback termination is permitted only after the held process handle still resolves to the recorded PID and expected executable image path. This validates the current Windows console-process path and requires the caller and child to share a console. It does not claim Windows Service Control Manager coverage, and the Windows runner cannot prove Linux signal delivery.

## Files Created/Modified

- Gate/Status main and ConfigMgr files: CLI, schema validation, bind readiness, signal ownership, and bounded cleanup.
- Gate `CServer`: explicit idempotent acceptor stop and no accept restart after cancellation.
- `gate_status_startup_tests.cpp`: one shared owned-process harness driving two real production executables without a test-only production seam.
- Server Integration project and `windows-local.ps1`: explicit source/production target registration, app-local deployment, exact count, and disconnect guards.
- startup/Server/tests READMEs and contract matrix: Test IDs, Level, runner, dependencies, Windows mechanism, limitations, and 142-case baseline.

## Decisions Made

- Kept validation within each service-owned ConfigMgr because their required schema differs and tests observe only real EXE behavior. Extracting a shared parser would add a shallower Interface without a third production/test consumer.
- Used Gate's existing dependency-free `/get_test` route as the HTTP readiness probe and a gRPC channel handshake for Status. No Status business RPC is invoked, because those methods access Redis.
- Continued to classify every new case as Integration: each uses real sockets and child processes even though the endpoint is dynamic loopback and runtime is short.
- Left the workflow untouched. It was already dirty when execution began, and the existing `servers-release` job reaches the changed public runner.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Made cleanup failure a test failure**

- **Found during:** final lifecycle audit.
- **Issue:** owned process/tempdir destructors initially attempted cleanup but did not fail the case if cleanup itself failed.
- **Fix:** cleanup now records a GoogleTest failure when identity verification, termination, wait, or owned tempdir removal fails.
- **Files modified:** `tests/server/startup/gate_status_startup_tests.cpp`.
- **Verification:** final focused Gate/Status run passed 20/20 with zero owned tempdir residue.

**2. [Rule 2 - Missing Critical] Bound Gate before creating its worker pool**

- **Found during:** shutdown-order self-review.
- **Issue:** the first GREEN implementation created the Asio worker pool immediately before constructing the HTTP listener.
- **Fix:** listener construction/bind now precedes pool creation; the owned catch path stops both server and pool.
- **Files modified:** `GateServer/GateServer/GateServer.cpp`.
- **Verification:** final Gate/Status focused run passed 20/20, including occupied bind and graceful stop.

---

**Total deviations:** 2 missing-critical cleanup/order fixes. Both strengthen the planned contract without adding business behavior or architecture.

## Issues Encountered

- The first direct MSBuild attempt was denied read access to the machine's Windows SDK locator under the sandbox. The same scoped build was approved and completed successfully.
- A freshly relinked Gate executable lost app-local DLLs during MSBuild incremental cleanup before a direct focused run. Status remained green while Gate failed before `main` with no stderr. Running the same precise `z-applocal` step owned by `RunServerTests` restored the release directory; the unchanged focused suite then passed 20/20. No second build or dependency restore was performed.
- Production compilation retained existing third-party Boost/protobuf/gRPC/hiredis warnings; the scoped build completed with zero errors and no warning suppression.

## Verification

- `CheckTestStructure`: GREEN; 11 Server, 2 Qt, 6 VarifyServer, and 2 PowerShell sources.
- Scoped Gate/Status/Integration Release build: GREEN in 84.3 seconds, zero errors.
- Gate/Status focused real-process Integration: GREEN, 20/20 after final production and cleanup changes.
- Complete Server Integration executable: GREEN, 28/28.
- Public `RunServerTests -Configuration Release`: GREEN, 91/91; generated/descriptor protocol check and Gate/Status/Chat Release link/app-local deployment passed.
- Qt scoped Release CTest: GREEN, 9/9.
- Varify scoped handler and real loopback RPC: GREEN, 10/10.
- Eleven XML reports: exactly 142 cases, zero failures/errors.
- Owned tempdir residue: zero; process exit and listener rebind are asserted in the Integration cases.
- Stub scan, sensitive-log scan, synthetic fixture-to-report scan, explicit-path `git diff --check`, and new-file trailing-whitespace scan: GREEN.

## Known Stubs

None. The only placeholder-like value is an explicitly synthetic, local-only configuration fixture; it never reaches a report and does not flow into production behavior.

## User Setup Required

None. Tests use existing pinned dependencies, dynamic loopback ports, owned child processes, and unique temporary directories.

## Unverified Range

- Remote CI was intentionally not triggered.
- Real Redis, MySQL, SMTP, cross-service business RPCs, and four-process E2E were intentionally not accessed.
- Windows Service Control Manager and Linux signal delivery remain platform-specific gaps; the verified Windows mechanism is console `CTRL_BREAK_EVENT` to an owned process group.
- Full `RunServerTests` passed before the final cleanup-failure and Gate bind-order hardening; the final code then received a scoped Release rebuild and the directly affected 20-case real-process rerun. Unaffected Unit/Component/lifecycle reports remain from the successful public runner.
- The full Varify configuration/startup runner was not invoked because this execution was forbidden from reading protected runtime configuration; the handler and real loopback RPC subset passed 10/10.
- `RunClientTests` was not rebuilt because no client file changed; the existing Release CTest binaries passed 9/9. PowerShell sources were unchanged and were not rerun; their exact 9/4 XML reports passed the final audit.

## Task Commits

None. Staging, commit, and push were explicitly prohibited in the shared dirty workspace.

## Next Phase Readiness

- G-001 is closed for the `develop` quick gate with 20 real Gate/Status process contracts.
- Plan 2.5-05 can add bounded owned-remote gRPC client/pool behavior without changing these startup probes or introducing external dependencies.
- G-015 remains open for four-release-unit dependency orchestration and business recovery; this plan proves only deterministic Gate/Status single-process listener lifecycle.

## Self-Check: PASSED

- All production, test, runner, project, Module documentation, matrix, and Summary artifacts declared above exist.
- The final Integration report contains exactly 28 cases; all eleven reports total 142 with zero failures/errors.
- No commit hash is claimed because commits were prohibited.
