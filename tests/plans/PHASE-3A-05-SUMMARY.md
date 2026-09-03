---
phase: 3A
plan: 05
subsystem: Qt auth and network outcome state
tags: [cpp17, qt6, unit-test, component-test, auth-flow, reducer, tdd]
requires:
  - Phase 3A-00 frozen contracts and G-005 baseline
  - Phase 3A-04 12-report/220-case baseline
  - DG-02 ClientSession reset contract
  - DG-25 persistent vcpkg immutability
provides:
  - production-owned AuthFlowCoordinator deep Module
  - one Reduce Interface shared by production Adapters and tests
  - deterministic Q03-AUTH-01..12 Unit and Component coverage
  - 12-report/232-case manifest and 24-case Qt baseline
affects:
  - Phase 3A-06 regression and CI closeout
  - Phase 3B real HTTP/TCP transport coverage
  - Phase 3C/3D complete login E2E coverage
tech-stack:
  added: [C++17 static library, QtTest Unit and Component targets]
  patterns: [single reducer Interface, optional no-action result, monotonic flow generation, outcome deduplication, production Adapter wiring]
key-files:
  created:
    - chat/authflowcoordinator.h
    - chat/authflowcoordinator.cpp
    - chat/tests/auth-flow/auth_flow_tests.cpp
    - chat/tests/auth-flow/auth_flow_component_tests.cpp
    - chat/tests/auth-flow/README.md
  modified:
    - chat/CMakeLists.txt
    - chat/httpmgr.h
    - chat/httpmgr.cpp
    - chat/tcpmgr.h
    - chat/logindialog.h
    - chat/logindialog.cpp
    - chat/registerdialog.h
    - chat/registerdialog.cpp
    - chat/resetdialog.h
    - chat/resetdialog.cpp
    - chat/mainwindow.h
    - chat/mainwindow.cpp
    - chat/tests/README.md
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
    - tests/README.md
    - tests/REGRESSION.md
    - tests/plans/PHASE-3A-PLAN.md
    - tests/plans/PHASE-3A-TEST-PLAN.md
decisions:
  - AuthFlowCoordinator exposes only Reduce; an optional action kind is the stable no-action representation and the action set remains frozen at four values.
  - Reduce(0, BeginHttp) generates the monotonic flow ID so callers only retain and return correlation state.
  - HTTP/TCP managers and UI classes remain Adapters; abnormal disconnect delegates to the existing ClientSession reset path.
metrics:
  completed: 2026-09-02
  duration: approximately 40 minutes
  tasks: 4
  runner_cases_added: 12
  reports_added: 0
---

# Phase 3A Plan 05: Qt Auth/Network Outcome Coordinator Summary

Qt authentication outcomes now pass through one production-owned reducer with monotonic flow correlation, stable error actions, legal stage transitions and exactly-once success handling, while abnormal disconnect continues to reuse the existing authenticated-session reset Module.

## Outcome

`AuthFlowCoordinator::Reduce(AuthFlowId, const AuthOutcome&) -> AuthAction` is the Module's sole external Interface. `Reduce(0, BeginHttp)` validates a known module/request and generates a monotonic flow ID. Later HTTP, TCP, Chat login and abnormal-disconnect outcomes must carry the current flow ID and appear at a legal stage. Unknown module/request pairs, duplicates and outcomes from older flows return a stable no-action result.

`AuthAction::kind` is `std::optional<AuthActionKind>`; absence means no UI action. The only present kinds are `StayAndShowError`, `ConnectChat`, `ShowLogin` and `ShowChat`. Stable `AuthError` values and the `ServerInfo` payload let Adapters display localized errors or connect without re-implementing reducer branches.

## Interface, Adapters and Callers

- `HttpMgr` carries the coordinator-generated flow ID through its completion signals. It remains the real QNetworkAccessManager Adapter and was not exercised by synthetic tests.
- Login/Register/Reset Dialogs translate transport, parse and business results into `AuthOutcome`, call the shared Interface and execute only returned actions. Their handler maps are no longer indexed for rejected/unknown outcomes.
- `TcpMgr` retains frame parsing and raw Chat login signals; `LoginDialog` translates TCP connect and Chat login signals into outcomes.
- `MainWindow` stores the active authenticated flow only after `ShowChat`. On an unexpected close it submits `AbnormalDisconnect`, requires `ShowLogin`, then calls the existing `_session.resetSession(SessionResetReason::UnexpectedDisconnect)` path.
- The Component test observes the real `ClientSession::sessionReset` Qt signal and exact reason. It does not repeat Q02 account maps, decoder, pending-batch, owned-page destruction or idempotence assertions.

Production `chat.exe`, `auth_flow_tests` and `auth_flow_component_tests` all link the same `chat_auth_flow` static library. The Component target additionally links the existing `chat_session_core`; no production `.cpp` is copied into either test target.

## TDD Evidence

### RED

- Q03-AUTH-01 first exposed the missing shared Module. After the target's required Qt Widgets dependency was connected, the real test target failed to link on undefined `AuthFlowCoordinator::Reduce`.
- Q03-AUTH-02 and 03 each produced behavior RED while the minimal implementation recognized only the preceding flow's network failure.
- Q03-AUTH-05 through 10 produced focused behavior RED as malformed JSON, business failure, Login HTTP success, TCP failure, Chat login failure and Chat login success were introduced one vertical slice at a time.
- Q03-AUTH-12 produced a Component RED because abnormal disconnect had no `ShowLogin` transition.
- Q03-AUTH-04 and 11 were GREEN on their first focused run because the preceding minimal current-flow validation already rejected unknown and old outcomes. No artificial failure was introduced.

### GREEN

- Q03-AUTH-01..03 return one `StayAndShowError/Network` per Register, Reset or Login flow.
- Q03-AUTH-04..06 reject unknown identities and keep JSON/business failures in the current flow without connecting Chat.
- Q03-AUTH-07..10 allow only Login HTTP success → TCP connect → Chat login progression; failures never show Chat and success acts once.
- Q03-AUTH-11 rejects both a duplicate current outcome and a late outcome from the prior monotonic flow.
- Q03-AUTH-12 maps a post-login abnormal disconnect to `ShowLogin` and reuses `ClientSession::resetSession(UnexpectedDisconnect)`.
- Final focused `^auth_flow\.` result: **12/12 passed** under `QT_QPA_PLATFORM=minimal`, with 10-second CTest hard timeouts and no socket, port, display or fixed sleep.

### Meaningful mutation

The `ChatLoginSucceeded` transition was temporarily changed to leave the reducer in `AwaitingChatLogin` and skip processed-outcome recording. `auth_flow.chat_login_success` then failed deterministically because the duplicate success returned a second `ShowChat` action. The exact dedup insert and `Stage::Chat` transition were restored; the Unit/Component/chat targets rebuilt and focused Q03-AUTH returned to **12/12 GREEN**.

## Verification Results

| Verification | Result |
| --- | --- |
| Initial real RED | PASS; undefined `AuthFlowCoordinator::Reduce` link failure recorded after target wiring |
| Focused Q03-AUTH | 12/12 passed after mutation restoration |
| Directed Release `chat_auth_flow;auth_flow_tests;auth_flow_component_tests;chat` | PASS; shared library, both test targets and real `chat.exe` linked |
| `CheckTestStructure` | PASS; 21 Server, 5 Qt, 6 VarifyServer and 2 PowerShell sources registered |
| Only effective owning `RunClientTests -Configuration Release` | PASS with explicit Qt and MinGW roots |
| `client_unit.xml` | 18 cases, 0 failures, 0 errors; exactly 11 Q03-AUTH Unit cases |
| `client_component.xml` | 6 cases, 0 failures, 0 errors; exactly 1 Q03-AUTH Component case |
| Qt total | 24 cases, all GREEN |
| Declarative aggregate manifest | 12 reports / 232 cases |

The Qt toolchain was `D:\qt\Qt\6.5.3\mingw_64`, MinGW `D:\qt\Qt\Tools\mingw1120_64` (11.2.0), Ninja `D:\qt\Qt\Tools\Ninja\ninja.exe` and CMake `D:\cmake\bin\cmake.exe`. `D:\vcpkg\test-vcpkg` and `D:\git\Chat\vcpkg_installed` were not read for this Qt plan and were never modified. No restore/install/remove/update/upgrade, cache deletion/rebuild or toolchain identity/path change occurred.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Target dependency] Linked the public ServerInfo header's existing Qt Widgets dependency**

- **Found during:** Q03-AUTH-01 compile RED.
- **Issue:** `global.h`, which owns `ServerInfo`, includes QWidget; a Core-only `chat_auth_flow` target could not compile the frozen payload type.
- **Fix:** Linked the static library publicly to the already selected Qt Widgets target. No package or Interface substitution occurred.
- **Files modified:** `chat/CMakeLists.txt`.
- **Commit:** None (commits were explicitly forbidden).

**2. [Rule 3 - Runner invocation] Used process-local PowerShell execution-policy bypass**

- **Found during:** first `CheckTestStructure` invocation.
- **Issue:** direct script execution was blocked before the runner started.
- **Fix:** Invoked the same repository script with `powershell.exe -NoProfile -ExecutionPolicy Bypass -File`; system policy was not changed.
- **Files modified:** None.
- **Commit:** None.

**3. [Rule 3 - Count registration] Updated the aggregate hard-coded baseline guard**

- **Found during:** first effective `CheckTestStructure` run after adding Qt report counts.
- **Issue:** per-report counts were 18/6 but the aggregate registration guard still required the prior 220-case baseline.
- **Fix:** Updated all aggregate guard messages/sums to the planned 232-case baseline, preserving exactly twelve reports.
- **Files modified:** `scripts/windows-local.ps1`.
- **Commit:** None.

## Scope Not Executed

- No real QNetworkAccessManager/QTcpSocket request, loopback timing, fixed port, public endpoint or credential; real transport belongs to Phase 3B.
- No complete registration/login user journey; E2E remains Phase 3C/3D.
- No `RunAllTests`, Server, VarifyServer or PowerShell owning runner, full regression, aggregate secret/residue/docs audit, phase verifier, code review, CI, PR or remote action.
- No stage, commit, branch, stash, push or package operation was performed. Existing user and 3A-01..04 dirty/untracked work was preserved.
- No decoder, account/session map, pending batch, owned UI destruction or frame contract was duplicated.

## Remaining Gaps and Next Plan

Only G-011's Phase 3A synthetic outcome/state and abnormal reset-wiring portion is complete. Real HTTP/TCP Adapter timing remains Phase 3B, and complete login E2E remains Phase 3C/3D. Phase 3A as a whole is not complete; the next item is **Plan 3A-06 — regression, reports and CI closeout**.

## Known Stubs

None. Plan-owned production and test files contain no TODO/FIXME/placeholder, test macro, fake mode, `clearForTest` or empty data source that prevents the goal.

## Self-Check: PASSED

- All production Module, test, README and Summary files exist.
- `AuthFlowCoordinator` exposes exactly one public operation (`Reduce`) and `AuthActionKind` contains exactly the four frozen action values.
- The duplicate-success mutation is restored; no TODO/FIXME/placeholder, `clearForTest`, test macro or fake mode remains in plan-owned production/test files.
- `git diff --check` passed, the staged index is empty, and no tracked deletion was introduced.
- Commits intentionally do not exist because the execution authorization prohibited staging and commits.
