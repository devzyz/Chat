---
phase: 3A
plan: 04
subsystem: GateServer request orchestration
tags: [cpp, component-test, gate, orchestration, deep-module, tdd]
requires:
  - Phase 3A-00 frozen contracts and DG-25
  - Phase 3A-03 12-report/204-case baseline
  - Existing GateResponse and Gate gRPC client contracts
provides:
  - production-owned GateRequest deep Module
  - one Handle Interface shared by four Gate routes and Component tests
  - deterministic T08-GATE-01..16 Component coverage
  - 12-report/220-case manifest and 166-case Server baseline
affects:
  - Phase 3A-05 next execution
  - Phase 3B real Gate HTTP composition
  - Phase 3C real Redis/MySQL/gRPC/SMTP Adapter coverage
tech-stack:
  added: [C++17 static library, GoogleTest Component suite]
  patterns: [single orchestration Interface, internal ports, production/test Adapters, fail-closed early return]
key-files:
  created:
    - GateServer/GateServer/GateRequest.h
    - GateServer/GateServer/GateRequestInternal.h
    - GateServer/GateServer/GateRequestProduction.h
    - GateServer/GateServer/GateRequest.cpp
    - GateServer/GateServer/GateRequestProduction.cpp
    - GateServer/GateServer/GateRequest.vcxproj
    - GateServer/GateServer/GateRequest.vcxproj.filters
    - tests/server/gate-request/gate_request_component_tests.cpp
    - tests/server/gate-request/README.md
  modified:
    - GateServer/GateServer/LogicSystem.h/.cpp
    - GateServer/GateServer/GateServer.vcxproj
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
  - GateRequest alone owns the four POST business sequences, early returns and dependency-error mapping.
  - GateResponse remains the sole JSON parser, exception envelope and response allowlist owner.
  - Real HTTP and real Redis/MySQL/gRPC/SMTP behavior remain later-phase gaps.
metrics:
  completed: 2026-09-01
  tasks: 4
  runner_cases_added: 16
  reports_added: 0
---

# Phase 3A Plan 04: Gate Request Orchestration Summary

The four Gate POST workflows now share one production-owned `GateRequest::Handle` Interface, with ordered dependency calls, fail-closed early returns and successful login assignment protected by sixteen deterministic Component cases.

## Outcome

`gate::GateRequest::Handle(gate::Endpoint, const Json::Value&) -> gate::Result` is the only external Interface. The Module owns verification, registration, password-reset and login ordering and returns one stable result. It does not parse HTTP JSON or shape the response envelope: the existing `gate::HandleJsonRequest`/`GateResponse` Module remains the sole parser, exception boundary and DG-03 allowlist owner.

The implemented sequences are:

- verification: require email, then request a code;
- registration: compare confirmation, read/compare the code, then create the user;
- reset: read/compare the code, match username/email, then update the password;
- login: check credentials, then obtain the Status assignment.

Every early return stops all later Adapter calls. Dependency false/error/exception paths use existing public `ErrorCodes` and fail closed; no proto, ErrorCodes numeric value or allowlist changed.

## Interface, Ports, Adapters and Callers

- `VerificationPort` production Adapter reuses `VerifyGrpcClient`; the test Adapter injects an error/result or exception and records `verification`.
- `CodeStore` production Adapter reuses `RedisMgr`; the test Adapter injects optional code/exception and records `code.read`.
- `UserStore` production Adapter reuses `MysqlMgr` for create, identity match, password update and credential check; the test Adapter only injects outcomes/exceptions and records the invoked operation.
- `StatusPort` production Adapter reuses `StatusGrpcClient`; the test Adapter injects an assignment/exception and records `status.assign`.
- `LogicSystem` owns one production `GateRequest`. Each of `/get_varifycode`, `/user_register`, `/reset_pwd` and `/user_login` keeps the existing `GateResponse` callback and delegates the business request to the same `_gate_request->Handle` Interface.

`GateServer.vcxproj` and `ServerComponentTests.vcxproj` both reference `GateRequest.vcxproj`; tests do not compile a copied production implementation. The static library itself references the existing `GateGrpcClients.vcxproj` production client library.

## TDD Evidence

### RED

Vertical behavior slices ran through the real `ServerComponentTests` target:

- T08-GATE-01 first produced the shared-library compile/link RED: `CreateGateRequest` was unresolved (`LNK2001`/`LNK1120`).
- T08-GATE-02 then produced the first behavioral RED: the unimplemented Module returned `RPCFailed` without calling verification.
- T08-GATE-04..08 exposed registration behaviors in order: the default path returned `RPCFailed`, skipped code/user calls, or returned the wrong business error until each preceding check and call was implemented.
- T08-GATE-09..13 similarly exposed reset code, identity and update sequencing one behavior at a time.
- T08-GATE-14 first observed `RPCFailed` with no credential call; T08-GATE-15 observed no Status assignment; T08-GATE-16 observed no successful assignment fields.
- T08-GATE-03 was adjacent GREEN after T08-GATE-02 generalized verification result propagation; no artificial failure was introduced.

No existing-behavior ambiguity required changing public errors: confirmation mismatch maps `PasswdErr`, absent code `VarifyExpired`, code mismatch `VarifyCodeErr`, create collision `UserExist`, identity mismatch `EmailNotMatch`, update false `PasswdUpFailed`, invalid credentials `PasswdInvalid`, and dependency exception/Status failure `RPCFailed`.

### GREEN

- T08-GATE-01..03 protect missing email, one verification call, success/failure and exception closure.
- T08-GATE-04..08 protect confirmation → code read/compare → create order and every registration early return.
- T08-GATE-09..13 protect code read/compare → identity → update order and every reset early return.
- T08-GATE-14..16 protect credentials → Status order, failure closure and complete successful UID/token/host/port assignment.
- The final focused filter passed **16/16 in 1 ms** under a two-second process hard limit. Tests use scoped synchronous in-memory Adapters, synthetic markers and no fixed sleep.

### Meaningful mutation

The registration code-mismatch branch was temporarily mutated to call `UserStore::CreateUser` after the failed comparison. T08-GATE-06 failed deterministically because the recorded calls became `{code.read, user.create}` instead of `{code.read}`. The extra call was removed exactly and the focused case returned to GREEN; no mutation remains.

## Verification Results

| Verification | Result |
| --- | --- |
| DG-25 read-only preflight | PASS; fixed tool root and `x64-windows-chat` installed artifacts were available; no dependency state changed |
| Focused T08-GATE | 16/16 passed in 1 ms |
| Directed Release `GateRequest;ServerComponentTests;GateServer` | PASS; shared library, Component executable and real `GateServer.exe` linked |
| `CheckTestStructure` | PASS; 21 Server, 3 Qt, 6 VarifyServer and 2 PowerShell sources registered |
| Only effective owning `RunServerTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg` | PASS; one invocation, exit 0 |
| `server_unit.xml` | 68 cases, 0 failures, 0 errors |
| `server_component.xml` | 56 cases, 0 failures, 0 errors; exactly 16 GateRequest cases |
| `server_integration.xml` | 34 cases, 0 failures, 0 errors |
| `server_chat_grpc_integration.xml` | 4 cases, 0 failures, 0 errors |
| `server_gate_unit.xml` | 2 cases, 0 failures, 0 errors |
| `server_status_unit.xml` | 2 cases, 0 failures, 0 errors |
| Server total | 166 cases, all GREEN |
| Declarative aggregate manifest | 12 reports / 220 cases |

All MSBuild paths explicitly used `VcpkgManifestInstall=false` and `VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\`; the directed commands also fixed the tool root and triplets. The initial sandboxed directed build could not access the installed Windows SDK under AppData and stopped before compilation; the same fixed-tree command was rerun outside the sandbox and passed. No restore/install/remove/update/upgrade, package cleanup/rebuild, root/triplet/baseline/tool-identity change or alternate install tree was attempted.

The component XML contains none of the synthetic password, code, token or email markers. No real service, socket, public endpoint or credential was used.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Build environment] Used the permitted host Windows SDK boundary**

- **Found during:** first directed Release build.
- **Issue:** the filesystem sandbox denied access to the installed Windows SDK under AppData before C++ compilation.
- **Fix:** reran the identical fixed-vcpkg, manifest-install-disabled command outside the sandbox; no dependency or source identity changed.
- **Files modified:** None.
- **Commit:** None.

## Scope Not Executed

- No real Gate HTTP transport/composition claim; that remains Phase 3B.
- No real Redis, MySQL, Varify gRPC, Status gRPC or SMTP Adapter Integration; those remain Phase 3C.
- No complete registration/login E2E; that remains Phase 3C/3D.
- No `RunAllTests`, Qt, VarifyServer, PowerShell owning runner, full regression, full secret/residue/docs audit, phase verifier, code review, CI, PR or remote action.
- No stage, commit, branch, stash, push or package mutation was performed. Existing user and 3A-01/02/03 dirty/untracked work was preserved.

## Remaining Gaps and Next Plan

Only G-009's Phase 3A in-process orchestration part is complete. Full G-009 remains open until real Gate HTTP and real dependency Adapters are proven. Phase 3A as a whole is not complete. The next item is **Plan 3A-05 — Qt auth/network outcome coordinator**.

## Known Stubs

None. The created/modified plan-owned files contain no TODO/FIXME/placeholder path that prevents the contract from operating.

## Self-Check: PASSED

- All production Module, project, test, README and Summary files exist.
- The mutation is restored: production has exactly one `CreateUser` call, public `GateRequest.h` has exactly one `Handle` operation, and the test source has exactly sixteen focused cases.
- The plan-owned source/test/README files contain no TODO/FIXME/placeholder, `clearForTest` or test-only production hook.
- `git diff --check` passed, the staged index is empty, and no plan-owned deletion was introduced.
- Commits intentionally do not exist because the execution authorization prohibited staging and commits.
