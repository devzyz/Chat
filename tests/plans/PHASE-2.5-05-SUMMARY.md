---
phase: "2.5"
plan: "05"
subsystem: grpc-client-pools
tags: [grpc, deadline, pool, gate, chat, integration, regression]
requires:
  - phase: "2.5-02"
    provides: canonical protobuf generation and C++/Node loopback foundation
  - phase: "2.5-04"
    provides: real production executable startup and cleanup harness
provides:
  - Shared finite-deadline unary RPC runtime and bounded pool lifecycle
  - Real Gate and Chat production-client dynamic-loopback contracts
  - Startup validation for configurable 100..60000 ms gRPC limits
  - Production static libraries shared by release executables and tests
affects: [phase-2.5, gate-rpc, chat-rpc, develop-ci, phase-3]
tech-stack:
  added: []
  patterns: [bounded RAII lease, dynamic loopback adapter, shared production static library, stable internal failure category]
key-files:
  created:
    - common/grpc/GrpcClientRuntime.h
    - GateServer/GateServer/GateGrpcClients.vcxproj
    - ChatServer/ChatServer/ChatGrpcClients.vcxproj
    - tests/server/ChatGrpcClientTests.vcxproj
    - tests/server/rpc/bounded_grpc_pool_tests.cpp
    - tests/server/rpc/gate_grpc_pool_contract_tests.cpp
    - tests/server/rpc/chat_grpc_pool_contract_tests.cpp
    - tests/server/rpc/gate_grpc_client_tests.cpp
    - tests/server/rpc/chat_grpc_client_tests.cpp
    - tests/plans/PHASE-2.5-05-SUMMARY.md
  modified:
    - GateServer/GateServer/VerifyGrpcClient.h
    - GateServer/GateServer/VerifyGrpcClient.cpp
    - GateServer/GateServer/StatusGrpcClient.h
    - GateServer/GateServer/StatusGrpcClient.cpp
    - ChatServer/ChatServer/StatusGrpcClient.h
    - ChatServer/ChatServer/StatusGrpcClient.cpp
    - ChatServer/ChatServer/ChatGrpcClient.h
    - ChatServer/ChatServer/ChatGrpcClient.cpp
    - GateServer/GateServer/ConfigMgr.cpp
    - ChatServer/ChatServer/ConfigMgr.cpp
    - Chat.sln
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - "One production GrpcClientRuntime owns deadline application, internal failure classification, and bounded pool lifecycle; no test-only helper or copied parser was exposed."
  - "Gate and Chat release executables and Integration tests link the same production client static libraries."
  - "StatusServer has no outbound gRPC Adapter after the canonical-protocol migration, so no artificial Status client was added."
patterns-established:
  - "Every project unary RPC passes through rpc::InvokeUnary and therefore receives a finite ClientContext deadline."
  - "Pool leases return by RAII; close is idempotent, wakes waiters, rejects future acquire, and safely drops returns after close."
requirements-completed: [G-003]
duration: 55m
completed: 2026-08-27
---

# Phase 2.5 Plan 05: Bounded gRPC Clients and Pool Lifecycle Summary

Gate and Chat owned-remote adapters now have finite acquisition and RPC limits, deterministic pool shutdown, stable failure mapping, and real production-client loopback regression coverage in the required develop runner.

## Accomplishments

- Added `rpc::BoundedPool` with finite acquire, move-only RAII leases, `PoolExhausted`/`Closed` outcomes, idempotent close, waiter wakeup, and safe return-after-close behavior.
- Added `rpc::InvokeUnary`, which applies a finite `ClientContext` deadline to every project unary call and classifies `DeadlineExceeded`, `Unavailable`, `Cancelled`, pool exhaustion, and pool closure internally.
- Preserved the existing public compatibility contract: all transport/pool failures map to `ErrorCodes::RPCFailed`; there is no automatic retry.
- Applied production defaults from DG-01: 1000 ms pool acquisition, 3000 ms Status/Chat RPC, and 15000 ms Varify RPC.
- Added optional Gate/Chat `[Grpc]` configuration validation. Explicit values must be integer milliseconds in 100..60000 and are rejected before listener startup.
- Replaced the duplicated Gate and Chat queue/flag implementations with the shared runtime while retaining four actual production pool types.
- Added Gate and Chat production static libraries so release executables and tests consume the same client implementation instead of test copies.
- Added dynamic `127.0.0.1:0` loopback tests for success, deadline exceeded, unavailable endpoint, and peer shutdown. Fixtures use bounded server shutdown and no public or external service.

## TDD and Mutation Evidence

- Initial pool RED failed compilation because `common/grpc/GrpcClientRuntime.h` did not exist; the first exhausted-pool case then passed after the minimum runtime was introduced.
- The first Gate client RED failed compilation because the desired configurable production client policy/constructor did not exist; the real unavailable-endpoint case passed after production wiring.
- Focused common/actual-pool GREEN: 18/18 in 299 ms.
- Focused Gate production-client/config GREEN: 6/6 in 1.19 seconds.
- Focused Chat production-client GREEN: 4/4 in 794 ms.
- Isolated removal of `context.set_deadline` made `CheckTestStructure` RED with the finite-deadline diagnostic; restoring it returned GREEN.
- Isolated `_closed = false` mutation made `CloseWakesWaitingBorrowerWithoutWaitingForAcquireTimeout` RED at its 500 ms assertion. The source was restored, rebuilt by the public runner, and all pool tests returned GREEN.

No existing test was deleted, relabelled, weakened, or retried to obtain a green result.

## Testcase and Report Changes

| Report | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Server Unit | 35 | 53 | +18 |
| Server Component | 24 | 24 | 0 |
| Server Integration | 28 | 34 | +6 |
| Chat gRPC Integration | 0 | 4 | +4 |
| Gate lifecycle Unit | 2 | 2 | 0 |
| Status lifecycle Unit | 2 | 2 | 0 |
| Qt Unit / Component | 7 / 2 | 7 / 2 | 0 |
| Varify Unit / Integration | 18 / 11 | 18 / 11 | 0 |
| PowerShell Component / Integration | 9 / 4 | 9 / 4 | 0 |
| **Total** | **142** | **170** | **+28** |

The final twelve-report audit found exactly 170 testcase elements and zero failure/error elements. Server contributes 119 cases across six reports.

## Interface and Configuration Contract

`GrpcClientRuntime` is the single deep Module boundary: callers provide a production pool, request, deadline, and generated stub invocation; the Module hides acquisition, `ClientContext`, deadline application, response construction, and transport categorization. Tests construct the same public production clients with short explicit policies and dynamic endpoints. No private methods, `clearForTest`, test build flag, or alternate RPC implementation was introduced.

Gate validates `PoolAcquireTimeoutMs`, `StatusDeadlineMs`, and `VarifyDeadlineMs`; Chat validates `PoolAcquireTimeoutMs`, `StatusDeadlineMs`, and `ChatDeadlineMs`. Missing values retain production defaults. `StatusServer` owns only its inbound service in the current canonical mapping and therefore has no outbound policy.

## Issues and Deviations

**1. Generated protobuf objects remained in final consumers**

The first static-library shape included generated protobuf/gRPC objects and caused duplicate abseil/gRPC link ownership when the release executable also compiled its canonical generated set. The libraries were narrowed to production client/config/routing sources; each final executable/test retains the generated objects appropriate to its consumer mapping. This preserves one client implementation without duplicating generated symbol ownership.

**2. Chat and Gate client tests use separate executables**

Both legacy modules expose a global `StatusGrpcClient` type. Combining them in one test executable would create a source-level name collision. Gate remains in `ServerIntegrationTests`; Chat owns `ChatGrpcClientTests`. Both are called by the same public runner and reported separately.

Scoped Release builds retained existing third-party protobuf/gRPC/Boost warnings and completed with zero errors. No warning suppression was added.

## Verification

- `CheckTestStructure`: GREEN; 16 Server, 2 Qt, 6 VarifyServer, and 2 PowerShell sources.
- GateServer, ChatServer, ServerIntegrationTests, and ChatGrpcClientTests scoped Release build: GREEN in 156.5 seconds.
- Gate/Chat production static-library linkage and all three production executables: GREEN.
- Public `RunServerTests -Configuration Release`: GREEN in 199.1 seconds; 119/119 Server cases, protocol generation/compatibility check, app-local deployment, and exact report guards passed.
- Final reports: `server_unit` 53, `server_component` 24, `server_integration` 34, `server_chat_grpc_integration` 4, Gate lifecycle 2, Status lifecycle 2.
- All 12 XML reports: exactly 170 cases, zero failures, zero errors.
- Synthetic token/email marker scan of Server reports: zero matches; new production RPC paths contain no logging call.
- `git diff --check`: GREEN; only existing LF-to-CRLF checkout notices were emitted.

## Unverified Range

- Remote CI was not triggered.
- Real Redis, MySQL, SMTP, four-release-unit business E2E, multi-process cross-instance delivery, and retry policy were intentionally not added or accessed.
- Qt, Varify, and PowerShell sources were unchanged by this plan and were not rerun; their existing 9, 29, and 13-case zero-failure reports were included in the final 170-case audit.
- Windows Service Control Manager and non-Windows runtime behavior remain separate platform-compatibility work.

## User Setup Required

None. The tests use the existing pinned vcpkg/protobuf toolchain, dynamic loopback ports, and bounded in-process fixtures.

## Task Commits

None. No staging, commit, push, workflow dispatch, or remote mutation was performed.

## Next Phase Readiness

- G-003 is closed for the develop quick gate.
- Plan 2.5-06 can now build deterministic four-release-unit orchestration on bounded outbound clients without inheriting infinite RPC waits.
- Phase 3 Business tests can reuse the production client constructors and dynamic endpoint policy without creating test-only seams.

## Self-Check: PASSED

- All production, test, runner, project, documentation, matrix, and Summary artifacts declared above exist.
- The restored code passed the single final public Server runner; all twelve reports total 170 with zero failures/errors.
- No commit hash is claimed because no commit was created.
