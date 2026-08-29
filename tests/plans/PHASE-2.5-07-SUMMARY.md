---
phase: "2.5"
plan: "07"
subsystem: develop-regression-gate
tags: [ci, github-actions, junit, clean-runner, regression, governance]
requires:
  - phase: "2.5-01..06"
    provides: 173 registered deterministic regression testcases and twelve reports
provides:
  - Exact twelve-report/173-testcase local integrity gate
  - Shared public Run*Tests paths for local and Windows CI execution
  - Missing-report, count, failure/error, and new temporary-residue failure propagation
  - Stable candidate check names and documented branch-protection procedure
affects: [develop-ci, phase-3, windows-release]
tech-stack:
  added: []
  patterns: [public runner parity, exact report manifest, incremental residue guard, always-uploaded junit]
key-files:
  created:
    - tests/plans/PHASE-2.5-07-SUMMARY.md
  modified:
    - .github/workflows/windows-ci.yml
    - scripts/windows-local.ps1
    - tests/CI-GOVERNANCE.md
    - tests/REGRESSION.md
    - tests/TEST-CONTRACT-MATRIX.md
    - tests/README.md
    - docs/Quality.md
    - docs/Build.md
    - WINDOWS_BUILD.md
key-decisions:
  - "A single declarative report manifest owns all twelve names and exact counts; each runner validates its reports and RunAllTests validates the complete 173-case set."
  - "Cleanup guards compare known test-owned temporary prefixes before and after a runner, so current-run leaks fail without deleting or claiming ownership of pre-existing developer state."
  - "No commit or PR is allowed without an authenticated GitHub CLI even when local and origin/develop baselines are aligned."
requirements-completed: [local-plan-2.5-07]
duration: 45m
completed: 2026-08-29
---

# Phase 2.5 Plan 07: Clean-runner Convergence and Develop Gate Summary

The local develop regression gate now rejects incomplete or failing evidence and proves all 173 registered contracts through the same four public entry points used by Windows CI; remote clean-PR acceptance remains blocked because GitHub CLI is unavailable on this machine.

## Accomplishments

- Added `CheckTestReports`, backed by one declarative manifest of twelve report names and exact counts. It rejects a missing/invalid report, count drift, any XML failure/error node, or an aggregate other than 173 cases.
- Made `RunAllTests` invoke all four owning runners and the same complete report audit. Server, Qt, VarifyServer, and PowerShell runners retain their own failure exit codes and per-report validation.
- Added incremental cleanup guards for known Server, VarifyServer, and PowerShell temporary-directory prefixes. A successful runner fails if it creates and leaves new owned state; pre-existing machine state is not deleted or adopted.
- Strengthened `CheckTestStructure` so the aggregate cannot disconnect a toolchain, the workflow must call all public runner entries, test reports must upload with `if: always()` and `if-no-files-found: error`, and `continue-on-error` is forbidden.
- Extended workflow project parsing for both production gRPC libraries and the Chat client Integration test project. Varify packaging now copies canonical `proto/varify.proto` rather than the removed legacy service-local proto.
- Preserved the historical CI constraints: registry baseline `fc3be1...` remains separate from vcpkg tool identity `4b3e4c...`; tolerated native nonzero handling still clears `LASTEXITCODE`; Qt 6.5.3 MinGW `windeployqt` still auto-detects Release and verifies `platforms/qwindows.dll`.
- Documented the stable candidate check names and the remaining Phase 3 gaps without claiming remote enforcement.

## RED-GREEN Evidence

Each owning runner has recoverable missing-report evidence through the public no-build audit:

| Lane | Controlled missing report | RED | Restored GREEN |
| --- | --- | --- | --- |
| Server | `server_unit.xml` | exit 1 | included in 173/12 audit |
| Qt | `client_unit.xml` | exit 1 | included in 173/12 audit |
| VarifyServer | `varify_unit.xml` | exit 1 | included in 173/12 audit |
| PowerShell | `script_component.xml` | exit 1 | included in 173/12 audit |

All report files were moved back in `finally` paths; no RED backup remains. The final GREEN audit reports exactly 173 testcases across twelve reports.

## Local Verification

- `actionlint` 1.7.12 against `.github/workflows/windows-ci.yml`: GREEN.
- `CheckTestStructure`: GREEN; 16 Server, 3 Qt, 6 VarifyServer, and 2 PowerShell sources registered.
- Completed `RunAllTests -Configuration Release`: GREEN in 78.8 seconds on the warm local dependency/build baseline.
- Server: 119/119; Qt: 12/12; VarifyServer: 29/29; PowerShell: 13/13.
- Twelve XML reports: 173 testcase elements, zero failure elements, zero error elements.
- GateServer, StatusServer, and ChatServer app-local Release directories contain their EXE, config, and required core DLLs; Chat includes both example instance configs.
- Qt Release prerequisites include `chat.exe`, `config.ini`, and `platforms/qwindows.dll`; Varify dependencies and canonical proto are present.
- No Gate/Status/Chat or test executable process remained. The completed run introduced no new known-prefix test temporary directory.
- `VarifyServer/config.json` was inspected without echoing values: it contains no credential-shaped key. Configured credential values have zero matches in reports and added diff lines. One configured email identity appears only in a removed historical source line and is not introduced by this change.
- `git diff --check`: GREEN; line-ending conversion notices are informational.

## Deviations and Auto-fixed Issues

### 1. [Rule 1 - Runner bug] Allowed an empty cleanup baseline

The first Server-complete aggregate attempt exposed that a mandatory PowerShell array parameter rejected an empty baseline before comparing residue. `AllowEmptyCollection` was added; subsequent Server and full aggregate execution passed.

### 2. [Rule 1 - Cache recovery bug] Made incomplete Qt cache recovery real

An interrupted generated cache contained `CMAKE_MAKE_PROGRAM-NOTFOUND`. The previous runner printed a recovery warning but passed the damaged cache back to CMake. The runner now uses `cmake --fresh` only after positively identifying an incomplete cache. The documented minimum is CMake 3.24, the first supported baseline for this recovery path. The build directory itself is preserved.

### 3. [Rule 3 - Invocation preflight] Supplied documented tool roots explicitly

The first aggregate invocation stopped before Server build because the shell did not inherit `VCPKG_ROOT`. The final invocation used the documented pinned `D:\vcpkg\test-vcpkg` and Qt 6.5.3/MinGW 11.2 roots. The vcpkg executable reported tool identity `4b3e4c...`; it was not confused with the registry baseline.

## Clean CI Submission Gate

| Condition | Result |
| --- | --- |
| Local structure, full regression, reports, release prerequisites, actionlint, diff and secret checks | PASS |
| Fresh `origin/develop` relative to local HEAD | PASS; both `1718e037b6882653508b7e6e99563af3221d151f` after `git fetch` |
| Protected/user files excluded | PASS; candidate index excludes user, planning, quarantine, proxy, and build content |
| GitHub CLI authenticated and able to create branch/PR | PASS; authenticated repository administrator with SSH Git operations |

The original Plan 2.5-07 execution stopped before any remote mutation because GitHub CLI was unavailable. Submission resumed after authentication: the same local evidence, current `origin/develop` baseline, candidate scope, added-line secret scan, and whitespace checks were revalidated before creating a topic branch and PR. Local success is still not presented as clean-runner success; the PR run is the owning remote evidence.

Stable candidate check names, awaiting a real PR run, are:

- `Static configuration checks`
- `Server Release build`
- `Qt client Release`
- `VarifyServer dependency and package check`

After a clean PR establishes those names, an administrator must merge them into the existing `develop` protection rules without weakening any current rule. This task cannot be marked remotely complete before that evidence exists.

## Remaining Phase 3 Scope

G-001 through G-006 are locally closed. The matrix still correctly leaves G-007 through G-011 for deterministic Business/Architecture work, G-012 through G-015 for disposable real Adapter and multi-process Integration, G-016 for dual-ChatServer business E2E, G-017 for current/previous-release compatibility, and G-018 for artifact/UAT promotion.

## Known Environment State

One pre-existing empty-prefix-compatible directory, `chat-instance-validation-1b60afa800f84384a5a53da57e8ebdad`, dates from 2026-08-25 and predates this run. It was not created, modified, claimed, or deleted by Plan 2.5-07. The runner's before/after guard proves the completed run added no residue.

## Task Commits

The original execution made no remote change. A later explicitly authorized submission resumes from the unchanged `origin/develop` base and uses a topic branch plus PR; it does not push directly to `develop` or alter branch protection.

## Self-Check: PASSED

- All Plan 2.5-07 files and this Summary exist.
- Final public report audit is 173/173 across twelve reports with no failure/error nodes.
- Workflow syntax, runner structure, release prerequisites, credential-value checks, and diff whitespace checks passed.
- The isolated candidate index excludes protected user/planning/build content and passes added-line secret and whitespace checks.
- Branch protection remains unchanged; the clean PR run is required before the candidate check names can become Required Checks.
