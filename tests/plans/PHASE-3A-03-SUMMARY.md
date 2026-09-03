---
phase: 3A
plan: 03
subsystem: ChatServer session identity and send state
tags: [cpp, asio, component-test, session-registry, fifo, tdd]
requires:
  - Phase 3A-00 frozen contracts and DG-08
  - Phase 3A-01 LogicDispatcher production library
  - Phase 3A-02 12-report/194-case baseline
provides:
  - production-owned ChatSessionState deep Module
  - opaque session handle and unified caller wiring
  - deterministic T08-SESSION-01..10 Component coverage
  - 12-report/204-case manifest and 150-case Server baseline
affects:
  - Phase 3A-04 next execution
  - Phase 3B real Chat TCP transport coverage
  - Phase 3C Redis presence integration coverage
tech-stack:
  added: [C++17 static library, GoogleTest Component suite]
  patterns: [opaque handle, production/test adapter seam, manual async completion, matching cleanup]
key-files:
  created:
    - ChatServer/ChatServer/ChatSessionState.h
    - ChatServer/ChatServer/ChatSessionStateInternal.h
    - ChatServer/ChatServer/ChatSessionState.cpp
    - ChatServer/ChatServer/ChatSessionStateProduction.h
    - ChatServer/ChatServer/ChatSessionStateProduction.cpp
    - ChatServer/ChatServer/ChatSessionState.vcxproj
    - tests/server/chat-session-state/chat_session_state_tests.cpp
    - tests/server/chat-session-state/README.md
  modified:
    - ChatServer/ChatServer/CServer.h/.cpp
    - ChatServer/ChatServer/CSession.h/.cpp
    - ChatServer/ChatServer/UserMgr.h/.cpp
    - ChatServer/ChatServer/LogicSystem.h/.cpp
    - ChatServer/ChatServer/ChatServiceImpl.cpp
    - ChatServer/ChatServer/ChatServer.vcxproj
    - tests/server/ServerComponentTests.vcxproj
    - Chat.sln
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
    - tests/README.md
    - tests/REGRESSION.md
    - tests/server/README.md
    - tests/plans/PHASE-3A-PLAN.md
    - tests/plans/PHASE-3A-TEST-PLAN.md
decisions:
  - ChatSessionState alone owns UID/session matching, queue capacity, FIFO advancement and close state.
  - UserMgr owns one production registry; all Chat callers use the opaque handle Interface.
  - Real TCP faults remain Phase 3B and Redis presence integration remains Phase 3C.
metrics:
  completed: 2026-09-01
  tasks: 4
  runner_cases_added: 10
  reports_added: 0
---

# Phase 3A Plan 03: Chat Session Registry and Send State Summary

Production-owned session identity and ordered-send state now use one opaque-handle Interface shared by ChatServer and deterministic Component tests, with exact capacity, matching cleanup and writer-failure closure protected by ten cases.

## Outcome

`ChatSessionState::Create/RegisterCurrent/FindCurrent/Close/Send` is the only caller-facing Interface. It hides session IDs, UID mappings, close flags, queues and writer progress. A session ID is non-empty and unique; a UID has one current session; replacement is atomic inside the Module; old-session close cannot remove the replacement; repeated/concurrent close is idempotent; and matching presence cleanup occurs at most once.

`Send` returns `Accepted`, `Full` or `Closed`. Each session preserves FIFO, accepts exactly `MAX_SENDQUE` outstanding frames, rejects the next without overwrite, rejects immediately after close, advances only after writer success, and turns writer failure into exactly one close plus matching cleanup.

## Interface, Seams and Callers

- `UserMgr` owns the one production `ChatSessionState` instance.
- `CServer` owns transport objects keyed by opaque handles and delegates validation/close to the Module.
- `CSession` creates/registers/sends/closes through the Module; its production `SessionWriter` Adapter reuses `SendNode` and `boost::asio::async_write`.
- `LogicSystem` and `ChatServiceImpl` use `FindCurrent` and `Send` rather than a second UID/session decision path.
- The production presence Adapter reuses the existing Redis session registration and session/IP cleanup. The test Adapter records calls in memory. UID/session matching remains in `ChatSessionState`.
- Tests use a fixed ID source, a manual writer callback and a recorder. The concurrent-close case uses a barrier and two-second bounds; all threads are joined and every callback is completed before case exit.

The ChatServer executable and `ServerComponentTests` both reference `ChatSessionState.vcxproj`; no production `.cpp` is copied into the test target.

## TDD Evidence

### RED

Vertical slices were introduced one observable behavior at a time through the real `ServerComponentTests` project reference:

- T08-SESSION-01 first failed to compile because the production Module source did not exist (`C1083`).
- T08-SESSION-02 first failed to link because `RegisterCurrent/FindCurrent` were absent (`LNK2001`).
- T08-SESSION-04 first failed to link because `Close` was absent (`LNK2001`).
- T08-SESSION-07 first failed to link because `Send` was absent (`LNK2001`).
- T08-SESSION-08 produced a behavioral RED when the capacity guard was absent: frame 1001 was accepted and started instead of returning `Full`.
- T08-SESSION-10 produced a behavioral RED when writer-failure closure was absent: the session remained current, cleanup stayed zero, and another send was accepted.

T08-SESSION-03, 05, 06 and 09 were GREEN on their first focused run because the minimal preceding map/mutex/close state already implied those adjacent observations. No artificial failing condition was introduced; subsequent slices and mutation protected their behavior.

### GREEN

- Each compile/link RED was closed with the smallest production Interface/state transition required by that slice.
- The exact `frames.size() >= MAX_SENDQUE` guard closed the capacity RED without overwriting accepted frames.
- Writer callbacks carry a generation; duplicate or late completions cannot advance or close twice.
- A writer failure enters the same matching `CloseLocked` transition used by explicit close.
- Final focused filter `ChatSessionStateTests.*`: **10/10 passed**, zero pending callbacks or threads.

### Meaningful mutation

The matching-delete condition was temporarily changed from “UID current still equals this session” to UID unconditional erase. T08-SESSION-04 then failed deterministically: `FindCurrent(42)` was empty instead of the replacement and cleanup count was 1 instead of 0. The exact matching predicate was restored, the mutation residue scan found only the matching form, and the focused suite returned to **10/10 GREEN**.

## Verification Results

| Verification | Result |
| --- | --- |
| DG-25 read-only preflight | PASS; fixed tool root, installed root, triplet and required package/header/library state present |
| Focused T08-SESSION | 10/10 passed |
| Directed Release `ChatSessionState;ServerComponentTests;ChatServer` | PASS; shared library, Component executable and real `ChatServer.exe` linked |
| `CheckTestStructure` | PASS; 20 Server, 3 Qt, 6 VarifyServer and 2 PowerShell sources registered |
| Owning `RunServerTests -Configuration Release` | PASS with explicit `-VcpkgRoot D:\vcpkg\test-vcpkg` |
| `server_unit.xml` | 68 cases, 0 failures, 0 errors |
| `server_component.xml` | 40 cases, 0 failures, 0 errors; 10 Chat session cases |
| `server_integration.xml` | 34 cases, 0 failures, 0 errors |
| `server_chat_grpc_integration.xml` | 4 cases, 0 failures, 0 errors |
| `server_gate_unit.xml` | 2 cases, 0 failures, 0 errors |
| `server_status_unit.xml` | 2 cases, 0 failures, 0 errors |
| Server total | 150 cases, all GREEN |
| Declarative aggregate manifest | 12 reports / 204 cases |

The first public-runner invocation omitted its required `-VcpkgRoot` argument and failed closed immediately after the structure check, before any build or testcase ran. The corrected invocation above is the only effective owning runner execution. All MSBuild paths used `VcpkgManifestInstall=false` and `VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\` through the directed command or public runner. No restore/install/remove/update/upgrade, root/triplet/baseline/tool-identity change or alternate tree was attempted.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Lifecycle correctness] Added the production writer close hook**

- **Found during:** T08-SESSION-09/10 caller integration.
- **Issue:** Closing only the registry state could leave the real socket Adapter open, while a writer failure needed the same exactly-once transport shutdown as explicit close.
- **Fix:** Added internal `SessionWriter::Close`; `CloseLocked` chooses the one caller that invokes it, and the production Adapter closes the socket with an error-code sink.
- **Files modified:** `ChatSessionStateInternal.h`, `ChatSessionState.cpp`, `CSession.cpp`, Component tests.
- **Commit:** None (commits were explicitly forbidden).

**2. [Rule 3 - Runner invocation] Supplied the required fixed vcpkg root**

- **Found during:** Owning public runner startup.
- **Issue:** The first invocation omitted `-VcpkgRoot` and the runner failed closed before build/test.
- **Fix:** Reinvoked with `-VcpkgRoot D:\vcpkg\test-vcpkg`; no package mutation occurred.
- **Files modified:** None.
- **Commit:** None.

## Scope Not Executed

- No real TCP connection, partial-write, read-timing, peer-disconnect or process-lifecycle test; these remain Phase 3B.
- No real Redis command/lock/TTL/disconnect Integration; this remains Phase 3C.
- No `RunAllTests`, Qt, VarifyServer, PowerShell owning runner, full regression, secret/residue/docs aggregate audit, phase verifier, code review, CI, PR or remote action.
- No stage, commit, branch, stash or push was performed. Existing user and 3A-01/3A-02 dirty/untracked work was preserved.
- No proto, public error code, frame codec, Redis pool, gRPC or Qt `ClientSession` contract was duplicated or changed for this plan.

## Remaining Gaps and Next Plan

Only G-008's Phase 3A in-memory part is complete. Full G-008 remains open until the Phase 3B real TCP behavior and Phase 3C Redis presence behavior are proven. Phase 3A as a whole is not complete. The next item is **Plan 3A-04 — Gate request orchestration**.

## Known Stubs

None. The created/modified plan-owned files contain no TODO/FIXME/placeholder path that prevents the contract from operating.

## Self-Check: PASSED

- All production Module, project, test, README and Summary files exist.
- Matching-delete mutation is restored; no test macro or `clearForTest` exists in the public Module.
- `git diff --check` passed, the staged index is empty, and no tracked deletion was introduced.
- Commits intentionally do not exist because the execution authorization prohibited staging and commits.
