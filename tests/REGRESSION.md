# Regression test strategy

Branch and release enforcement is defined in
[`CI-GOVERNANCE.md`](CI-GOVERNANCE.md). The authoritative inventory of current
Test IDs, reports, lanes, and gaps is
[`TEST-CONTRACT-MATRIX.md`](TEST-CONTRACT-MATRIX.md). The next executable
baseline-hardening work is planned in
[`plans/PHASE-2.5-PLAN.md`](plans/PHASE-2.5-PLAN.md).

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

`CheckTestReports` is the result-integrity gate. It requires exactly twelve
registered XML reports and 173 runner testcases, and rejects a missing/invalid
report, count drift, any `<failure>`/`<error>` node, or an incomplete aggregate.
`RunAllTests` calls the same audit after all four public toolchain runners.

## Permanent baseline from phases one and two

| Capability | Protected contracts | Current confidence | Next missing layer |
| --- | --- | --- | --- |
| Build and startup | pinned toolchains, app-local packaging, config precedence, invalid argument/port fail-fast | Baseline | clean-runner parity and graceful service shutdown |
| Server foundation | config validation, message nodes, protobuf round-trip, frame validation, Asio lifecycle | Baseline | protocol compatibility fixtures and cancellation races |
| Chat routing foundation | peer name/address mapping and startup bind cleanup | Partial | real two-instance RPC and unavailable-peer behavior |
| Data foundation | Redis pool wait/close without a service | Partial | disposable Redis/MySQL adapter and transaction tests |
| Qt messaging model/session | stable indexes, ack/status/removal, history order/dedup, Unicode, store state, delegate layout, connection/session reset isolation | Baseline | broader network-to-model result mapping and account-keyed persistent cache |
| VarifyServer | configuration, protocol descriptor, injected handler, loopback gRPC, bind failure | Baseline | real Redis/SMTP adapters and shutdown signals |
| Instance management | validation plus Start/Status/Stop process identity | Baseline | graceful stop and rollback after a partial batch start |

Baseline means the listed contracts are protected, not that the entire owning
executable is covered. In particular, `LogicSystem`, `CSession`, Gate routing,
Status selection, Qt network managers, and cross-service flows remain outside
the current baseline.

## Required lanes

| Lane | Runs | Purpose |
| --- | --- | --- |
| Fast regression | every PR; Unit and deterministic Component | pure rules, state machines, serialization, in-process composition |
| Integration regression | every PR when loopback/process-only; otherwise a provisioned job | real protocols, processes, temporary databases and owned adapters |
| E2E smoke | release candidate, and PRs that change a public workflow | a small set of complete user/protocol journeys across release units |
| Stress/soak | scheduled or explicit | long-running concurrency, media, reconnect, resource and race behavior |

The current Phase 2.5 local baseline is 173/173 across twelve reports. Clean
GitHub PR parity and branch-protection registration remain external acceptance
steps until a PR run proves all four stable Windows check names on the submitted SHA.

No test may connect to a developer's fixed Redis/MySQL instance, personal
credentials, public SMTP account, microphone, camera, or shared filesystem.
Integration dependencies must be disposable and identified by a run ID.

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

1. identify which existing Interface contracts can be affected;
2. run their focused test while developing;
3. add a red-capable regression test for a new contract or bug;
4. run `CheckTestStructure` so every new test is registered;
5. run `RunAllTests` before handoff;
6. add Integration/E2E coverage when a protocol, process, persistence Adapter,
   or public workflow changed.

Coverage metrics may be added as a secondary signal for changed code, but a
line-coverage percentage must never replace contract assertions or RED-GREEN
verification.
