---
phase: 3B
plan: 03
subsystem: status-grpc-integration
tags: [grpc, protobuf, loopback, deadlines, cancellation, lifecycle, junit, dg-25]
requires:
  - Plan 3B-00 shared IntegrationHost and production-target contract
  - Plan 3B-01 run-owned process, port, deadline, and cleanup foundation
  - Phase 3A StatusRouting production Module and in-memory Adapters
  - DG-25 fixed read-only vcpkg roots
provides:
  - Shared StatusTransport production target used by StatusServer and integration tests
  - Twelve real generated-stub T09-SGRPC contracts in server_integration.xml
  - Restartable bounded StatusGrpcServer lifecycle with observable loopback endpoint
  - Thirteen-report 279-testcase manifest registration
affects: [phase-3b-04, phase-3b-05, windows-ci]
tech-stack:
  added: []
  patterns: [shared production transport target, generated gRPC stub, numeric loopback fixture, adapter-controlled fault schedule, bounded shutdown]
key-files:
  created:
    - StatusServer/StatusServer/StatusGrpcServer.h
    - StatusServer/StatusServer/StatusGrpcServer.cpp
    - StatusServer/StatusServer/StatusTransport.vcxproj
    - tests/server/integration-host/status_grpc_transport_tests.cpp
  modified:
    - Chat.sln
    - StatusServer/StatusServer/StatusServer.cpp
    - StatusServer/StatusServer/StatusServer.vcxproj
    - StatusServer/StatusServer/StatusServiceImpl.h
    - StatusServer/StatusServer/StatusServiceImpl.cpp
    - tests/server/ServerIntegrationTests.vcxproj
    - tests/server/integration-host/README.md
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - StatusServer and ServerIntegrationTests link one StatusTransport target instead of compiling private transport copies.
  - Every generated-stub call has a hard ClientContext deadline; test delay comes only from the Phase 3A in-memory store fault schedule.
  - A stopped StatusGrpcServer may start a new generation after its prior server has completed bounded shutdown and released its port.
  - RunServerTests is the sole owning public runner for this plan; RunAllTests remains reserved for Plan 3B-05.
patterns-established:
  - Formal executables and integration tests share the same Status gRPC service and server implementation.
  - Deadline, cancellation, shutdown, late completion, bind conflict, and restart are distinct bounded contracts.
requirements-completed: []
metrics:
  duration: 3h58m
  completed: 2026-09-06
  tasks: 3
  files_modified: 13
  server_cases: 203
  status_grpc_cases: 12
  regression_manifest_cases: 279
---

# Phase 3B Plan 03: Status gRPC Integration Summary

The formal Status server and integration suite now share one production gRPC transport, with twelve generated-stub loopback contracts proving routing, sanitized errors, bounded faults, restart generations, and complete listener release.

## Performance

- **Duration:** 3h58m from the first RED commit through Task 3 closeout
- **Started:** 2026-09-06T10:23:08Z
- **Completed:** 2026-09-06T14:21:13Z
- **Tasks:** 3/3
- **Plan files changed:** 13 before this Summary

## Accomplishments

- Extracted `StatusGrpcServer`, `StatusServiceImpl`, and generated Status protobuf objects into `StatusTransport.vcxproj`, shared by the formal `StatusServer` executable and `ServerIntegrationTests`.
- Added T09-SGRPC-01..05 for protocol-ready numeric loopback publication, real generated `GetChatServer`/`Login` calls, maximum UID, empty-list fail-closed behavior, and stable sanitized business errors.
- Added T09-SGRPC-06..12 for deadline, explicit cancellation, closed-port refusal, shutdown during an in-flight call, old-generation late completion, occupied-port failure, idempotent stop, restart, and listener release.
- Advanced the owning Server lane to 203 cases and registered the current thirteen-report manifest at 279 cases.

## Task Commits

Each behavior task retains separate RED and GREEN commits:

1. **Task 1: Share the production Status gRPC target and prove core behavior**
   - RED `92f2bfb` - `test(3B-03): add failing Status gRPC core contracts`
   - GREEN `24e927d` - `feat(3B-03): share Status gRPC transport over loopback`
2. **Task 2: Prove deadline, cancellation, refusal, shutdown, late completion, bind, and release faults**
   - RED `d76e50d` - `test(3B-03): add failing Status gRPC fault contracts`
   - GREEN `60352d2` - `feat(3B-03): complete Status gRPC fault lifecycle`
3. **Task 3: Register reports, counts, structure gates, and owning-runner evidence**
   - `a7caea6` - `chore(3B-03): register Status gRPC integration evidence`

The required RED-before-GREEN ordering is present in git history. Task 1 RED failed with the exact missing `StatusGrpcServer.h` compile boundary. Task 2 RED compiled, then produced the planned lifecycle failures: four of seven fault cases failed before restart/shutdown and runtime refusal expectations were completed.

## Contract and Mutation Evidence

- Task 1 restored focused verification passed T09-SGRPC Core 5/5. The routing-bypass mutation forced `EmptyServerListFailsClosedOverGeneratedStub` to fail exactly 1/5; restoration returned `StatusServiceImpl.cpp` to SHA-256 `6D86DC99FF102B880BCB867D81200F6FA981B4E1C9DD3CB9BBFEE0DAE4DF474C` and the rebuilt selector to 5/5.
- Task 2 restored focused verification passed T09-SGRPC Fault 7/7. Its in-memory fault schedule uses condition-variable entry/release signals and absolute deadlines, never a test RPC or sleep.
- The bind-result mutation made `StatusGrpcServer::Start` claim success after gRPC rejected an occupied port. `OccupiedPortStartupFailsWithoutStealingListener` failed exactly 1/1; restoration returned `StatusGrpcServer.cpp` to SHA-256 `7B2B0031AD9EA784274C47D36DF497DA32873C6EA1CB520235AA75CFCFE9BAB5`, followed by rebuilt Core 5/5 and Fault 7/7 runs.
- The structure mutation disconnected the formal `StatusServer.vcxproj` reference from `StatusTransport.vcxproj`. `CheckTestStructure` failed with `StatusServer must share and register the production StatusTransport target.` Restoration returned the project to SHA-256 `414D3A1ADEC3336F11CA42F41F51003A5121B1DB5381595D49FA150775647D61`, after which the structure gate and both focused selectors were green.
- The closed dynamic loopback endpoint is bounded by a 500 ms ClientContext deadline and is classified as `DeadlineExceeded` on this Windows gRPC runtime. The contract separately proves the result is bounded and does not expose synthetic evidence.

## Final Owning Runner Evidence

After structure and both focused suites were green, the plan's sole owning public runner was invoked exactly once:

`powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg -ServerIntermediateRoot D:\git\Chat\Cache\worktrees\phase-3b-integration-20260904\build\windows-servers-obj`

It returned exit 0 in 87.2 seconds. The six emitted Server reports were parsed after completion:

| Report | Cases | Failures | Errors | Disabled |
| --- | ---: | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 | 0 |
| `server_component.xml` | 56 | 0 | 0 | 0 |
| `server_integration.xml` | 71 | 0 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 | 0 |
| **Server total** | **203** | **0** | **0** | **0** |

- `server_integration.xml` contains exactly twelve T09-SGRPC cases inside its exact total of 71.
- Registration agrees on 13 reports / 279 cases: Server 203, Qt 34, VarifyServer 29, and PowerShell 13. `RunServerTests` intentionally owns only the six Server reports; no `RunAllTests` invocation occurred.
- A non-owning `CheckTestReports` read-only audit correctly refused to claim a full-manifest run because this worktree does not contain the earlier `client_unit.xml`. No aggregate or alternate owning runner was launched to manufacture that evidence; full thirteen-report regeneration remains exclusive to Plan 3B-05.
- All six Server XML files have zero failure, error, `system-out`, or `system-err` nodes, and contain zero raw `SYNTHETIC_STATUS_GRPC_TOKEN` or `SYNTHETIC_INVALID_TOKEN` markers.
- A recursive last-write audit from before the owning runner's build window found zero changed entries under `D:\vcpkg\test-vcpkg` and `D:\git\Chat\vcpkg_installed`; DG-25 writes were zero.
- After the runner, relevant server/test child process count and matching run-owned temporary-directory residue count were both zero. The passing shutdown/restart/occupied-port cases establish completion-queue, socket, and port release; no process was killed.

## Files Created/Modified

- `StatusServer/StatusServer/StatusGrpcServer.h/.cpp` - bounded start, actual numeric endpoint publication, readiness, wait, idempotent stop, restart, and listener release.
- `StatusServer/StatusServer/StatusTransport.vcxproj` - single production owner of the server, service, and generated Status protobuf implementation.
- `StatusServer/StatusServer/StatusServiceImpl.h/.cpp` - thin generated-service delegation to the Phase 3A `StatusRouting` Interface without public error-number changes.
- `tests/server/integration-host/status_grpc_transport_tests.cpp` - twelve generated-stub core and fault contracts over run-owned loopback endpoints.
- `Chat.sln`, `StatusServer.vcxproj`, and `ServerIntegrationTests.vcxproj` - shared target registration and formal/test references.
- `scripts/windows-local.ps1`, `tests/TEST-CONTRACT-MATRIX.md`, and the integration-host README - exact IDs, source ownership, report ownership, structure checks, and 203/279 counts.

## Decisions Made

- The synchronous gRPC server remains behind a deep lifecycle Interface; callers observe only start, bound endpoint, readiness, wait, and bounded stop.
- Formal and test consumers link the shared production target. Explicit gRPC/protobuf libraries precede the existing vcpkg wildcard in the formal link input so the same package set resolves consistently.
- Fault scheduling stays behind the existing Phase 3A in-memory store Adapter. Tests coordinate on observable entry/completion conditions rather than sleeps or test-only RPCs.
- The 279 figure is a registered manifest total. This plan's public-runner evidence is the six-report, 203-case Server slice; Plan 3B-05 owns complete manifest regeneration.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Fixed formal StatusServer gRPC/Abseil link ownership ordering**

- **Found during:** Task 1 GREEN formal build.
- **Issue:** `StatusServer` exposed only the vcpkg wildcard before consuming `StatusTransport.lib`, so `abseil_dll.lib` was selected before server-side static gRPC objects and produced LNK2005/LNK1169 duplicate symbols. The integration target already linked because its explicit gRPC/protobuf inputs preceded the wildcard.
- **Fix:** Matched the working integration/Gate ownership pattern by adding explicit `grpc++.lib;grpc.lib;libprotobuf.lib` inputs ahead of inherited dependencies in the formal target. No package, root, triplet, macro identity, or generated source changed.
- **Verification:** Formal `StatusServer` and `ServerIntegrationTests` builds passed from the fixed DG-25 installation, followed by Core 5/5.
- **Committed in:** `24e927d`.

**2. [Rule 1 - Bug] Allowed a fully stopped Status server to start a new generation**

- **Found during:** Task 2 RED lifecycle verification.
- **Issue:** The server retained `stopped_` as a permanent Start rejection, so late-generation and resource-release contracts could not prove a clean restart on the same production object and port.
- **Fix:** Successful `Start` begins a fresh generation by clearing `stopped_` only after gRPC has bound a valid port; active/stopping starts still fail closed.
- **Verification:** Late completion and stop/restart cases passed, followed by Fault 7/7 and the 203/203 owning lane.
- **Committed in:** `60352d2`.

**3. [Rule 3 - Blocking] Updated stale structure ownership and report-count assertions**

- **Found during:** Task 3 registration.
- **Issue:** The structure gate still expected direct Status routing call syntax and lacked shared-target, exact ID, and new count assertions.
- **Fix:** Added single-owner StatusTransport checks, exact twelve-case/source-ID checks, formal/test reference checks, direct-compile prohibitions, and 71/203/279 report registration.
- **Verification:** The restored structure gate passed; its disconnected-target mutation failed precisely; the sole owning runner passed 203/203.
- **Committed in:** `a7caea6`.

---

**Total deviations:** 3 auto-fixed (1 product lifecycle bug, 2 blocking integration issues).

**Impact on plan:** All changes were required to exercise the planned production transport, restart lifecycle, and single-target ownership. No new public protocol, external dependency, public endpoint, or package-tree mutation was introduced.

## Issues Encountered

- A temporary `GRPC_DLL_IMPORTS` alignment experiment did not resolve the formal duplicate-symbol link and was fully reverted before the minimal link-order fix.
- Sandboxed MSBuild could not access the local Windows SDK path; the identical fixed-root build command was rerun with explicit approval. The final Task 3 Git commit likewise needed approval solely for the linked worktree's shared `.git/worktrees/.../index.lock`.
- The optional read-only full-manifest audit reported the absent older `client_unit.xml`. This was retained as a fail-closed result and did not trigger a forbidden aggregate rerun.

## User Setup Required

None - no real Redis, MySQL, SMTP, credential, package installation, public network, or fixed shared port is required.

## Known Stubs

None. The shared production server, generated service, formal/test target references, twelve contracts, report registration, and owning-runner evidence are implemented and exercised. Later Chat TCP/composition/full-manifest work remains explicitly assigned to Plans 3B-04/05 rather than represented by placeholders here.

## Threat Flags

None. The new numeric loopback listener, generated RPC boundary, deadline/cancellation behavior, bind ownership, evidence sanitization, and dependency roots are explicit Plan 3B threat-model surfaces covered by T-3B-02 through T-3B-07 and T-3B-SC.

## Next Phase Readiness

- Plan 3B-03 is complete on the registered 13-report/279-case manifest with authoritative 203/203 Server evidence and T09-SGRPC Core 5/5 plus Fault 7/7.
- Plan 3B-04 can reuse the run-owned endpoint, absolute-deadline, generation, mutation, and structural single-owner patterns for Chat TCP.
- Plan 3B-05 still owns the only full `RunAllTests`, complete thirteen-report regeneration, closeout negative probe, clean PR, and remote `develop` evidence.
- No task-owned process, thread, completion queue, socket, port, temporary directory, dependency-tree mutation, staged file, or unresolved required verification failure remains.

## Self-Check: PASSED

- Commits `92f2bfb`, `24e927d`, `d76e50d`, `60352d2`, and `a7caea6` exist in required order on `phase-3b-integration-20260904`.
- All 13 implementation/test/registration files and this Summary exist; no tracked file was deleted.
- T09-SGRPC Core is 5/5, Fault is 7/7, and the sole owning Server runner emitted exactly 203 cases with zero failures/errors/disabled cases.
- Restored mutation hashes match the Status routing, bind-result, and formal-target baselines; DG-25 changed roots, report payload/secret markers, owned processes, and temporary residue are all zero.
- Before this Summary commit the staged index was empty and the only remaining working-tree changes were this Summary plus the two explicitly deferred orchestrator-owned planning documents.

---
*Phase: 3B*
*Completed: 2026-09-06*
