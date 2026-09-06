---
phase: 3B
plan: 01
subsystem: process-integration-test-foundation
tags: [run-context, process-harness, win32, deterministic-cleanup, evidence-sanitization, dg-25]
requires:
  - Plan 3B-00 IntegrationHost contract and twelve-report 238-testcase baseline
  - Phase 3A shared production Modules
  - DG-25 fixed read-only vcpkg roots
provides:
  - RunContext ownership for run identity, dynamic loopback ports, temporary paths, deadlines, and reverse cleanup
  - Win32 ProcessHarness with protocol readiness, PID plus creation-time identity, bounded graceful stop, and bounded escalation
  - Twelve T09-PROC contracts in server_integration.xml and a twelve-report 250-testcase manifest
  - Sanitized separation of primary, child-output, and cleanup-failure evidence
affects: [phase-3b-02, phase-3b-03, phase-3b-04, phase-3b-05, windows-ci]
tech-stack:
  added: []
  patterns: [run-owned resource ledger, identity-checked child process, protocol-ready probe, bounded stop escalation, evidence redaction]
key-files:
  created:
    - tests/support/RunContext.h
    - tests/support/RunContext.cpp
    - tests/support/ProcessHarness.h
    - tests/support/ProcessHarness.cpp
    - tests/support/Win32ProcessAdapter.h
    - tests/support/Win32ProcessAdapter.cpp
    - tests/support/EvidenceSanitizer.h
    - tests/server/process-harness/run_context_tests.cpp
    - tests/server/process-harness/process_harness_tests.cpp
    - tests/server/process-harness/process_harness_fault_tests.cpp
    - tests/server/process-harness/process_harness_child.cpp
    - tests/server/process-harness/ProcessHarnessChild.vcxproj
    - tests/server/process-harness/README.md
  modified:
    - Chat.sln
    - tests/server/ServerIntegrationTests.vcxproj
    - scripts/windows-local.ps1
    - tests/README.md
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - Every process, port, and temporary path is adopted only after RunContext reserved it and is released through an observable reverse-order ledger.
  - Child ownership requires both PID and creation-time identity; graceful stop is attempted first and force termination is bounded and identity-checked.
  - Process stdout, stderr, primary failure, and cleanup failure are sanitized before they become retained evidence.
  - The owning public runner is RunServerTests only; RunAllTests remains reserved for Plan 3B-05.
requirements-completed: []
metrics:
  duration: 3h20m
  completed: 2026-09-06
  tasks: 3
  files_modified: 19
  server_cases: 184
  regression_manifest_cases: 250
---

# Phase 3B Plan 01: Deterministic Run-Owned Process Harness Summary

RunContext and the Win32 ProcessHarness now provide identity-checked child ownership, protocol-ready probing, bounded stop escalation, sanitized evidence, and complete process/port/temp cleanup through twelve deterministic T09-PROC contracts.

## Performance

- **Duration:** 3h20m from the first RED commit through closeout
- **Started:** 2026-09-06T02:08:14Z
- **Completed:** 2026-09-06T05:28:00Z
- **Tasks:** 3/3
- **Plan files changed:** 19, including this Summary

## Accomplishments

- Added a unique immutable run identity, synthetic identity namespace, run-owned temporary root, dynamic loopback reservation, absolute deadline, and reverse-order idempotent cleanup ledger.
- Added a Win32 child-process Adapter and ProcessHarness that preserve PID plus creation-time identity, require protocol readiness, drain bounded output, attempt graceful stop, and escalate only within an owned deadline.
- Proved refused readiness, expired deadlines, occupied ports, late output, separate primary/cleanup failures, sanitization, and zero process/port/temp residue.
- Registered the twelve emitted T09-PROC contracts in the existing `server_integration.xml`, raising that report from 40 to 52 cases, Server from 172 to 184, and the twelve-report manifest from 238 to 250.

## Task Commits

Each TDD task was committed with separate RED and GREEN gates:

1. **Task 1: Own every run resource through RunContext**
   - RED `527af18` - `test(3B-01): add failing RunContext contracts`
   - GREEN `66640e3` - `feat(3B-01): implement run-owned integration resources`
2. **Task 2: Start, probe and stop child processes with the Win32 Adapter**
   - RED `32c076f` - `test(3B-01): add failing process harness contracts`
   - GREEN `8a3bb3b` - `feat(3B-01): add bounded Win32 process harness`
3. **Task 3: Prove deadline, port-conflict and cleanup failure propagation**
   - RED `4da8ff1` - `test(3B-01): add failing process fault contracts`
   - GREEN `5eb035a` - `feat(3B-01): complete process fault coverage`

The required RED-before-GREEN ordering is present in git history for all three tasks.

## Contract and Mutation Evidence

- Focused GREEN reports contain 5 RunContext, 5 ProcessHarness, and 2 fault cases: 12/12 total.
- The Task 3 cleanup mutation disabled the run-owned temp-root ledger cleanup. The single focused contract failed 1/1 with the precise diagnostic `run-owned temp root remained`.
- The mutation residue was removed only after validating that the resolved path remained inside the owned temp-root boundary.
- The mutation was restored exactly with `RunContext.cpp` SHA-256 `0E1667BE931D61E197FE2C74AF1B0F746B5DBE392D8FB34D0B031591E614B89B`.
- After restoration, the fixed-scope Release build completed with zero warnings/errors, `T09_PROC_Faults` passed 2/2, and the three focused suites remained 5+5+2=12/12 with no process or temp residue.

## Final Owning Runner Evidence

The authoritative public runner was executed exactly once after explicit user approval:

`powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg`

It returned exit 0. The six emitted Server reports were parsed from JUnit after completion:

| Report | Cases | Failures | Errors |
| --- | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 |
| `server_component.xml` | 56 | 0 | 0 |
| `server_integration.xml` | 52 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 |
| **Server total** | **184** | **0** | **0** |

- `server_integration.xml` contains all 12 T09-PROC cases: 5 RunContext, 5 ProcessHarness, and 2 fault cases.
- The registered manifest contains 12 reports totaling 250 cases: Server 184, Qt 24, VarifyServer 29, and PowerShell 13.
- RunServerTests intentionally emits only the six Server reports. The six non-Server reports are owned by their respective runners and were not treated as outputs of this execution.
- JUnit contained no failure, error, system-out, or system-err nodes. The T09-PROC sanitization contract proved that raw synthetic secret values do not survive in collected process or teardown evidence.
- No `server_integration_tests`, `process_harness_child`, or `chat-process-harness-*` residue remained after the runner. Port/thread/handle ownership is covered by the passing teardown contracts, and no task-owned process remained to retain those resources.
- A recursive modification-time audit over the runner window found zero writes under both DG-25 protected roots: `D:\vcpkg\test-vcpkg` and `D:\git\Chat\vcpkg_installed`.

## Approval and Interrupted-Run Record

- An earlier owning runner began while the approval exchange was still resolving and rewrote the six Server reports, but it was forcibly interrupted before a final exit status existed. That run is explicitly inconclusive and is not counted as success evidence.
- The user then approved the exact command recorded above. The later exit-0 run is the sole authoritative owning-runner result for Plan 3B-01.
- The five previously observed MSBuild node-reuse PIDs `13476`, `15772`, `21952`, `27256`, and `31496` were re-queried by PID, creation time, and command line. All five were already absent, so no process was terminated. No mismatched, pre-existing, or unknown process was touched.

## Files Created/Modified

- `tests/support/RunContext.h/.cpp` - run identity, deadline, loopback-port, path, process-slot, failure, and reverse-cleanup ownership.
- `tests/support/ProcessHarness.h/.cpp` and `tests/support/Win32ProcessAdapter.h/.cpp` - bounded Win32 process lifecycle and collected evidence.
- `tests/support/EvidenceSanitizer.h` - central redaction of password, token, code, and email assignments before evidence retention.
- `tests/server/process-harness/*.cpp`, `ProcessHarnessChild.vcxproj`, and `README.md` - deterministic child fixture and twelve T09-PROC contracts.
- `tests/server/ServerIntegrationTests.vcxproj` and `Chat.sln` - real source and helper-target registration.
- `scripts/windows-local.ps1`, `tests/README.md`, and `tests/TEST-CONTRACT-MATRIX.md` - exact 52/184/250 counts and runner/report ownership.

## Decisions Made

- The harness does not adopt arbitrary PIDs or directories. Ownership must originate from the same RunContext and process identity must still match at cleanup time.
- Readiness is a successful protocol probe, not process existence or a fixed sleep.
- Primary failure remains distinct from cleanup failure, while both and all child output pass through the same evidence sanitizer.
- The current 250-case figure is a manifest total. This plan's authoritative runner evidence is the six-report, 184-case Server slice; the complete twelve-report lane remains a Plan 3B-05 responsibility.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Sanitized retained process and teardown evidence**

- **Found during:** Task 3 fault and failure-propagation contracts.
- **Issue:** Child stdout/stderr and RunContext primary/cleanup failure details crossed the evidence boundary without a shared redaction step.
- **Fix:** Added `EvidenceSanitizer.h` and applied it before retaining process output or failure details.
- **Verification:** T09-PROC-12 passed and asserted that raw synthetic values were absent while redacted markers remained.
- **Committed in:** `5eb035a`.

### Execution-Control Deviation

**2. Approval-race runner was interrupted and discarded as inconclusive**

- **Found during:** Task 3 owning-runner checkpoint.
- **Issue:** A runner started before the approval exchange had conclusively completed and was stopped without a final exit status.
- **Resolution:** Its rewritten reports were not accepted as evidence. After explicit approval, the exact command was run once and returned exit 0.
- **Files modified:** No source files; build reports were regenerated by the authoritative run.

---

**Total deviations:** 1 auto-fixed correctness/security issue and 1 execution-control incident.

**Impact on plan:** Evidence safety was completed within the planned threat model, and the inconclusive run was never used to overwrite or claim a passing result. Product and later transport scope did not expand.

## Known Stubs

None. All created test sources, the helper executable, and the Win32 process Adapter are registered and exercised. POSIX process support remains explicitly owned by Phase 3C and is not represented by a placeholder here.

## Threat Flags

None. Process identity, child input/output, temporary-file ownership, denial-of-service bounds, and evidence disclosure are all explicit Plan 3B threat-model surfaces covered by T-3B-01 and T-3B-05..07.

## Next Phase Readiness

- Plan 3B-01 is complete on the 12-report/250-case manifest with authoritative 184/184 Server evidence.
- Plans 3B-02 through 3B-04 can reuse RunContext and ProcessHarness for deterministic transport processes, ports, deadlines, evidence, and cleanup.
- `RunAllTests`, full twelve-report regeneration, clean PR, and remote develop evidence remain reserved for Plan 3B-05.
- No task process, run-owned temporary directory, dependency-tree mutation, staged change, or unresolved verification failure remains.

## Self-Check: PASSED

- Branch `worktree-agent-3b-01-execution` contains Task 3 GREEN `5eb035a`, and all six RED/GREEN commits exist in the required order immediately before this Summary commit.
- All 19 plan files, including this Summary, exist; the accidental main-checkout Summary path is absent.
- The six Server reports contain exactly 184 cases with zero failures/errors, including all 12 T09-PROC contracts.
- `RunContext.cpp` matches the post-mutation SHA-256, and staged index, task processes, and run-owned temp residue are all empty.
- The Task 3 commit contains no tracked deletion, and the pre-Summary source worktree was clean.

---
*Phase: 3B*
*Completed: 2026-09-06*
