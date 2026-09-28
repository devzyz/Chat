# Regression test strategy

Branch and release enforcement is defined in
[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md). The authoritative inventory of current
Test IDs, reports, lanes, and gaps is
[`TEST-CONTRACT-MATRIX.md`](TEST-CONTRACT-MATRIX.md); exact executable registration
and aggregate counts are owned by `scripts/windows-local.ps1`.
Current progress and next work are maintained only in [Status](../docs/Status.md).
Phase plans retain historical evidence. DG-25 remains in force: a missing or inconsistent
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

`CheckTestReports` is the result-integrity gate. It requires the exact XML report
groups and testcase counts defined by `$regressionReportGroups` in the public runner,
including `server_resource_integration.xml`, while preserving the original
twelve-report/232-testcase floor. It rejects a missing/invalid report, count
drift, any failure/error/skipped/disabled/unavailable/timeout result, a
credential-shaped assignment, missing cleanup evidence, or an incomplete aggregate.
`RunAllTests` calls the same audit after all four public toolchain runners.

## Historical foundation from phases one and two

The following table preserves the scope at the end of those phases. Its missing
layers are historical, not the current backlog. Later transport, dependency,
storage and resource coverage is recorded in the current contract matrix and module READMEs.

| Capability | Protected contracts | Confidence at that phase | Missing layer at that phase |
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
that historical baseline. They must not be described as absent from today's repository.

## Required lanes

| Lane | Runs | Purpose |
| --- | --- | --- |
| Fast regression | develop PR/push under the CI governance scope rules | existing Windows unit, component and deterministic loopback/process contracts |
| Full regression | master PR/push, weekly develop and manual runs | additional Linux real-dependency Integration and registered business E2E |
| Package startup smoke | master push after full checks | assembled Windows package startup and cleanup before publication |
| Focused local integration | owning module entry when relevant | resource and message-sync/receipt opt-in contracts outside the routine aggregate |
| Stress/soak | explicit work when a scenario exists | long-running concurrency, media, reconnect and resource measurements; no completed baseline is implied |

The original Phase 3A floor remains protected. Phase 3B added transport/process
contracts and `client_integration.xml`; later changes added storage, resources and
receipts. Current report groups and expected counts come from the public runner,
not historical phase totals. Local results do not substitute for remote checks
required by CI governance or prove that every opt-in suite was executed.

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

## Capability extension map

Local message persistence and file/resource transfer already exist; their rows
describe continuing regression requirements, not unimplemented modules. See
[MessageStorage](../docs/MessageStorage.md) and [Resources](../docs/Resources.md).
Group chat, voice messages, calls and LAN entries are future design guidance,
not claims of implemented or accepted capabilities.

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
7. collect aggregate evidence for phase closeout or an explicitly requested full validation, following CI governance;
8. add Integration/E2E coverage when a protocol, process, persistence Adapter,
   or public workflow changed.

Coverage metrics may be added as a secondary signal for changed code, but a
line-coverage percentage must never replace contract assertions or RED-GREEN
verification.
