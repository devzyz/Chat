---
phase: "2.5"
plan: "01"
subsystem: testing
tags: [ctest, junit, powershell, test-governance]
requires:
  - phase: phases-1-and-2-local-baseline
    provides: 95 registered runner testcases and initial local runners
provides:
  - Exact Qt Unit/Component labels and reports for all nine client testcases
  - Per-Test-ID JUnit reports for all thirteen PowerShell testcases
  - Structural validation for report groups, registrations, and Module README ownership
affects: [phase-2.5, clean-ci, test-reporting]
tech-stack:
  added: []
  patterns: [declarative report groups, per-Test-ID PowerShell JUnit, module-owned test documentation]
key-files:
  created: [tests/plans/PHASE-2.5-01-SUMMARY.md]
  modified:
    - chat/CMakeLists.txt
    - scripts/windows-local.ps1
    - tests/scripts/validation/chatserver-instances.tests.ps1
    - tests/scripts/lifecycle/chatserver-instances.tests.ps1
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - "Q01-MODEL-01..06 are Unit; Q01-MODEL-07..08 remain Component without moving the message-model Module."
  - "Each PowerShell script owns and writes its own per-Test-ID JUnit report; the unified runner validates report existence and count."
  - "tests/server/lifecycle is the sole source and documentation owner for the shared Asio pool contracts."
patterns-established:
  - "Runner report groups are declarative and reused by execution and structural validation."
  - "PowerShell console PASS/FAIL and JUnit testcase identity use the same explicit Test ID."
requirements-completed: [G-006]
duration: 33min
completed: 2026-08-25
---

# Phase 2.5 Plan 01: Test Metadata and Report Integrity Summary

**Nine Qt testcases are split accurately into seven Unit and two Component results, while thirteen PowerShell contracts now produce independently diagnosable JUnit testcases.**

## Performance

- **Duration:** 33 min
- **Started:** 2026-08-25T13:50:00Z
- **Completed:** 2026-08-25T14:22:54Z
- **Tasks:** 6
- **Files modified:** 14

## Accomplishments

- Marked Q01-MODEL-01..06 Unit and retained Q01-MODEL-07..08 as Component, with `RunClientTests` producing accurate `client_unit.xml` and `client_component.xml` reports.
- Added explicit A02-VAL-01..09 and A02-LIFE-01..04 identities to console output and JUnit, preserving all 13 baseline behaviors and nonzero failure propagation.
- Extended `CheckTestStructure` to validate exact Qt levels/report groups, npm and runner file lists, PowerShell Test ID counts/groups, Module README presence, and sole Asio source ownership.
- Consolidated the Asio contracts under `tests/server/lifecycle`; `tests/server/concurrency` now points to that sole owner without duplicating Test IDs or source.

## Task Commits

No commits or staging were created, as required for the shared dirty workspace.

## Files Created/Modified

- `chat/CMakeLists.txt` - Assigns the six Unit and two Component message-model CTest labels.
- `chat/tests/README.md` and `chat/tests/message-model/README.md` - Record per-Test-ID levels and reports.
- `scripts/windows-local.ps1` - Uses level report groups, validates structure, and consumes per-script JUnit.
- `tests/scripts/validation/chatserver-instances.tests.ps1` - Emits nine Test-ID JUnit cases.
- `tests/scripts/lifecycle/chatserver-instances.tests.ps1` - Emits four Test-ID JUnit cases.
- `tests/scripts/README.md`, `tests/scripts/validation/README.md`, and `tests/scripts/lifecycle/README.md` - Document IDs, levels, reports, isolation, and failure behavior.
- `tests/server/README.md`, `tests/server/concurrency/README.md`, and `tests/server/lifecycle/README.md` - Establish sole lifecycle ownership for the shared Asio contract source.
- `tests/README.md` and `tests/TEST-CONTRACT-MATRIX.md` - Record accurate report counts and close G-006.

## Decisions Made

- The PowerShell test scripts write their own JUnit because they alone know individual assertion outcomes; the outer runner validates the resulting report instead of synthesizing one aggregate testcase per process.
- Structural checks enforce the known Qt level contract but continue to allow future explicitly labelled CTest targets.
- Existing user and earlier-phase edits in dirty target files were preserved and extended incrementally.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Recovered an incomplete generated CMake cache without deleting build output**

- **Found during:** Qt runner verification
- **Issue:** The first configure left `CMAKE_MAKE_PROGRAM=CMAKE_MAKE_PROGRAM-NOTFOUND`, while repository constraints prohibited deleting the generated build directory.
- **Fix:** `Build-Client` now distinguishes an incomplete `*-NOTFOUND` cache from a genuinely different toolchain and safely reconfigures the former with the resolved Qt/Ninja paths.
- **Files modified:** `scripts/windows-local.ps1`
- **Verification:** A second full `RunClientTests -Configuration Release` configured, built 73 steps, and passed Unit 7/7 plus Component 2/2.
- **Committed in:** Not committed by instruction.

---

**Total deviations:** 1 auto-fixed blocking issue.
**Impact on plan:** Verification became reproducible without deleting generated directories or weakening any test.

## Verification

- `CheckTestStructure`: GREEN; 8 Server, 2 Qt, 6 VarifyServer, and 2 PowerShell sources registered with report/document checks.
- Controlled RED: changing `message_model.unknown_ids` to Component returned exit 1 with `must be unit`; restoring it returned GREEN.
- `RunClientTests -Configuration Release`: GREEN; `client_unit.xml` 7/0 and `client_component.xml` 2/0.
- `RunScriptTests`: GREEN; `script_component.xml` 9/0 and `script_integration.xml` 4/0, with zero error elements.
- `git diff --check` on all Plan 2.5-01 paths: GREEN; only informational LF-to-CRLF worktree warnings were printed.

## Known Stubs

None.

## Issues Encountered

- Direct invocation of `.ps1` was blocked by the machine execution policy. Verification used the repository-compatible Windows PowerShell 5.1 process with `-ExecutionPolicy Bypass`, matching the runner's own child-process strategy.

## User Setup Required

None.

## Next Phase Readiness

- Plan 2.5-01 report and structure contracts are ready for later Phase 2.5 work.
- No Plan 2.5-02-or-later behavior was implemented, and no production behavior changed.

## Self-Check: PASSED

- All declared implementation and documentation files exist.
- Expected XML reports exist with testcase counts 7, 2, 9, and 4.
- No commit claims were made because commits were explicitly prohibited.

---
*Phase: 2.5 regression-baseline-hardening*
*Completed: 2026-08-25*
