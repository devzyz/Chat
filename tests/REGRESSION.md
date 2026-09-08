# Regression test strategy

Branch and release enforcement is defined in
[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md). The authoritative inventory of current
Test IDs, reports, lanes, and gaps is
[`TEST-CONTRACT-MATRIX.md`](TEST-CONTRACT-MATRIX.md). Phase 2.5 and Plans
3A-01..05 are complete. The next planned item is Plan 3A-06 in
[`plans/PHASE-3A-PLAN.md`](plans/PHASE-3A-PLAN.md). The fixed local vcpkg tree
was verified read-only for 3A-02. DG-25 remains in force: a missing or inconsistent
dependency fails closed and never authorizes an automatic restore.

This document turns the phase-one and phase-two suites into a permanent
regression baseline. Phase names describe when a test was introduced; they do
not limit how long it remains required. Existing tests must keep running when
later modules are added.

## What the regression gate can prove

The gate proves that every registered, asserted contract still behaves as
recorded. It cannot prove that untested behavior is unchanged. A production
change therefore has two independent obligations:

1. all existing regression suites remain green;
2. every new or changed observable contract receives a test at the closest
   stable Interface.

Implementation-only refactors should normally leave Interface-level tests
unchanged. If a test must be rewritten merely because private classes or call
order changed, the test is crossing the wrong Seam.

`CheckTestStructure` is the registration gate. It fails when a Server, Qt,
VarifyServer, or PowerShell test source exists but is absent from its real
MSBuild/CMake/npm/PowerShell runner, or when a CTest target has no Level label.
It prevents silent tests; it does not replace behavioral assertions.

`CheckTestReports` is the result-integrity gate. It requires exactly thirteen
registered XML reports and 313 runner testcases while preserving the original
twelve-report/232-testcase floor. It rejects a missing/invalid report, count
drift, any failure/error/skipped/disabled/unavailable/timeout result, a
credential-shaped assignment, missing cleanup evidence, or an incomplete aggregate.
`RunAllTests` calls the same audit after all four public toolchain runners.

## Permanent baseline from phases one and two

| Capability | Protected contracts | Current confidence | Next missing layer |
| --- | --- | --- | --- |
| Build and startup | pinned toolchains, app-local packaging, config precedence, invalid argument/port fail-fast | Baseline | clean-runner parity and graceful service shutdown |
| Server foundation | config validation, message nodes, protobuf round-trip, frame validation, Asio lifecycle | Baseline | protocol compatibility fixtures and cancellation races |
| Chat routing foundation | peer mapping, dispatcher FIFO/capacity/shutdown, deterministic Status selection/token fail-closed, in-memory session identity/send FIFO/capacity/close, and startup bind cleanup | Partial | real TCP partial-write/peer-disconnect, Redis presence, two-instance RPC and unavailable-peer behavior |
| Gate request orchestration | verification, registration, reset, and login ordering; early-return call suppression; dependency failure/exception mapping | Partial | real Gate HTTP composition and Redis/MySQL/Varify/Status/SMTP Adapters |
| Data foundation | Redis pool wait/close without a service | Partial | disposable Redis/MySQL adapter and transaction tests |
| Qt messaging model/session/auth | stable indexes, ack/status/removal, history order/dedup, Unicode, store state, delegate layout, connection/session reset isolation, auth outcome ordering/dedup and abnormal reset delegation | Baseline | real HTTP/TCP timing, broader network-to-model mapping and account-keyed persistent cache |
| VarifyServer | configuration, protocol descriptor, injected handler, loopback gRPC, bind failure | Baseline | real Redis/SMTP adapters and shutdown signals |
| Instance management | validation plus Start/Status/Stop process identity | Baseline | graceful stop and rollback after a partial batch start |

Baseline means the listed contracts are protected, not that the entire owning
executable is covered. In particular, real `CSession` TCP behavior, Gate HTTP transport,
Status transport/persistence, Qt network managers, and cross-service flows remain outside
the current baseline.

## Required lanes

| Lane | Runs | Purpose |
| --- | --- | --- |
| Fast regression | every PR; Unit and deterministic Component | pure rules, state machines, serialization, in-process composition |
| Integration regression | every PR when loopback/process-only; otherwise a provisioned job | real protocols, processes, temporary databases and owned adapters |
| E2E smoke | release candidate, and PRs that change a public workflow | a small set of complete user/protocol journeys across release units |
| Stress/soak | scheduled or explicit | long-running concurrency, media, reconnect, resource and race behavior |

The original Phase 3A floor remains 232/232 across twelve reports. Phase 3B adds
six host, twelve process-harness, seven Gate HTTP, twelve Status gRPC, sixteen Chat
TCP, six formal-composition, ten Qt HTTP, and twelve Qt TCP cases, plus the
thirteenth `client_integration.xml` report. The current local baseline is therefore
313/313 across thirteen reports. Remote clean-PR checks, artifact evidence, and
post-merge `develop` evidence remain closeout requirements; local evidence does not
substitute for them.

Execution depth follows the canonical proportional tiers in
[`CI-GOVERNANCE.md` section 2.1](CI-GOVERNANCE.md#21-比例化执行合同). Focused
behavior evidence belongs to its owning plan; aggregate report, secret, residue,
diff and full-lane evidence is collected once at phase closeout.

No test may connect to a developer's fixed Redis/MySQL instance, personal
credentials, public SMTP account, microphone, camera, or shared filesystem.
Integration dependencies must be disposable and identified by a run ID.

Local vcpkg state follows DG-25. `D:\vcpkg\test-vcpkg` and
`D:\git\Chat\vcpkg_installed` are persistent, protected, and read-only by
default. All ordinary local builds/tests must disable MSBuild manifest install,
use the fixed installed directory, and fail nonzero when a dependency is absent
or inconsistent. They must not invoke `RestoreServers`, change a root/triplet/
baseline, select a substitute install tree, or delete/recreate packages. Any
such mutation requires the user's explicit approval after the exact command,
target, reason, impact, and rollback boundary are shown. GitHub-hosted runners
use isolated run-owned install roots; changing their dependency identity or
install-root contract also requires prior approval and D-04 review.

## New-module admission contract

Before a new module is merged, record the following in its test README:

1. the module's Interface, including invariants, error modes and timeouts;
2. its Domain and test Level;
3. each dependency category: in-process, local-substitutable, remote-owned, or
   true external;
4. the production Adapter and test Adapter at every real Seam;
5. the Test IDs and report that protect the Interface;
6. cleanup and deterministic timeout behavior;
7. remaining Integration/E2E gaps.

Prefer a production library linked by both the executable and its tests over
compiling a second, drifting list of production `.cpp` files into a test target.
Qt's existing decoder and message-model suites already use this shape through
`chat_network_core` and `chat_message_model`. Existing Server targets may
migrate incrementally when their owning modules change; a broad no-behavior
refactor is not required just to rename the old phases.

Do not create empty future test directories. Add a module directory when its
production Interface exists, then register its tests explicitly. The static
registration gate ensures the test is not silently omitted.

## Future capability map

| Capability | Recommended module Interface and Seam | Unit/Component regression | Integration/E2E regression |
| --- | --- | --- | --- |
| Local cache | conversation/message cache Interface; SQLite or filesystem Adapter | ordering, dedup, eviction, schema mapping, conflict and corruption rules using a temporary store | restart recovery, migration, offline history then server reconciliation |
| Group chat | group membership/role Interface plus message fan-out Interface | role transitions, permission matrix, idempotent membership events, ordering and duplicate delivery | two ChatServer instances, membership propagation, create/join/leave/send E2E |
| File transfer | transfer state machine and content-store Interface separated from transport Adapter | chunk boundaries, hash, retry, resume, cancellation, quotas and path safety | loopback stream, interrupted resume, temporary filesystem, sender-to-recipient E2E |
| Voice messages | immutable media metadata plus the file-transfer Interface; codec as an Adapter | duration/format validation, state transitions and metadata serialization | encode fixture, upload/download, cache recovery and playback-readiness result |
| Audio/video calls | call-signaling state machine separated from media/device Adapters | offer/answer/cancel/timeout/reconnect state transitions with fake clock and devices | loopback signaling, synthetic media, two-client connect/disconnect E2E; hardware tests remain manual |
| LAN communication | discovery Interface and peer transport Adapter | TTL, dedup, identity collision, route selection and trust policy with fake clock/socket | loopback UDP/TCP first; multicast/broadcast only on a dedicated capable runner |

Media payloads, file chunks and cache rows must not be pushed through the
existing text-message model as ad-hoc fields. Shared conversation ordering and
delivery status may live behind a deeper conversation Interface, while each
payload type owns its validation and Adapter-specific behavior.

## Change workflow

For every production change:

1. run the read-only toolchain/dependency preflight and stop if the fixed local
   vcpkg tree is incomplete; never auto-restore it;
2. identify which existing Interface contracts can be affected;
3. run their focused test while developing;
4. add a red-capable regression test for a new contract or bug;
5. register every new test in its owning target/runner; the plan or phase closeout
   verifies registration without repeating the same structure gate per task;
6. run the changed Module's owning public runner once after its plan stabilizes;
7. let the phase closeout run `RunAllTests` once and reconcile aggregate evidence;
8. add Integration/E2E coverage when a protocol, process, persistence Adapter,
   or public workflow changed.

Coverage metrics may be added as a secondary signal for changed code, but a
line-coverage percentage must never replace contract assertions or RED-GREEN
verification.
