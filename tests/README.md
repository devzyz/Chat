# Automated test structure

Tests are organized on two independent axes:

1. **Domain** identifies the responsibility being protected: `Foundation`,
   `Architecture`, or `Business`.
2. **Level** identifies the boundary exercised: `Unit`, `Component`,
   `Integration`, or `E2E`.

Production-module ownership determines the directory. Test level does not.
For example, Redis pool lifecycle tests stay in `tests/server/data` even though
they are Component tests, while ChatServer process startup tests stay in
`tests/server/startup` and are Integration tests.

## Governance and plans

- [`CI-GOVERNANCE.md`](CI-GOVERNANCE.md) is the authority for `develop`,
  `master`, release, failure, coverage, compatibility, feature-flag, and
  contract-change gates.
- [`TEST-CONTRACT-MATRIX.md`](TEST-CONTRACT-MATRIX.md) maps the current 173
  runner testcases to Test IDs, production Module ownership, Level, report,
  required lane, and known gaps.
- [`plans/PHASE-2.5-PLAN.md`](plans/PHASE-2.5-PLAN.md) is the executable
  baseline-hardening plan that must complete before Phase 3 implementation.
- [`plans/DG-DECISIONS.md`](plans/DG-DECISIONS.md) records the implementation
  contracts that must be confirmed before the corresponding Phase 2.5 plan.
- [`REGRESSION.md`](REGRESSION.md) defines the permanent regression baseline
  and future-Module admission strategy.

## Level contract

| Level | Boundary | PR dependency rule |
| --- | --- | --- |
| Unit | One constructible logic unit; no socket, child process, or external service | Required |
| Component | Several real in-process modules with fake/in-memory external boundaries | Required when deterministic |
| Integration | Real loopback protocol, child process, or disposable service dependency | Required only when setup and cleanup are deterministic |
| E2E | Complete public user or protocol flow across release units | Separate job after Integration foundations exist |

A fake Redis, MySQL, SMTP, or gRPC stub can isolate a Unit or Component test,
but cannot be reported as Integration coverage for that adapter. A child
process remains Integration even if it is fast and does not access the public
network.

## Current suites and reports

| Suite | Level | Report |
| --- | --- | --- |
| ChatServer deterministic logic plus Gate/Status Asio contracts | Unit | `server_unit.xml`, `server_gate_unit.xml`, `server_status_unit.xml` |
| ChatServer service-free Redis pool lifecycle plus Gate response allowlist | Component | `server_component.xml` |
| Chat/Gate/Status CLI, config, bind, ready, shutdown plus C++→Node Varify and production gRPC-client loopback | Integration | `server_integration.xml`, `server_chat_grpc_integration.xml` |
| Qt frame decoder plus message-model rules Q01-MODEL-01..06 | Unit | `client_unit.xml` |
| Qt message store/delegate Q01-MODEL-07..08 plus authenticated-session reset Q02-SESSION-01..06 | Component | `client_component.xml` |
| Varify protocol, injected handler, fake startup transition | Unit | `varify_unit.xml` |
| Varify config subprocess, loopback gRPC, direct-process bind failure | Integration | `varify_integration.xml` |
| ChatServer instance validation | Component | `script_component.xml` |
| ChatServer instance process lifecycle | Integration | `script_integration.xml` |

All reports are written beneath `build/test-results`. The public local/CI
entry points remain `RunServerTests`, `RunClientTests`, `RunVarifyTests`, and
`RunScriptTests`; `RunAllTests` invokes all four. Each entry runs every level
currently owned by that toolchain. `TestPhase1` is retained only as a
compatibility alias.

`CheckTestReports` is the no-build integrity audit for the current baseline. It
requires all 12 reports, exactly 173 testcases, and zero failure/error nodes.
Every owning runner performs the same per-report checks before returning; the
aggregate runner repeats the exact whole-baseline audit.

Protocol generation and compatibility use the additional public entries
`GenerateProtocols` and `CheckProtocols`. `RunServerTests` also runs the
compatibility/drift check after restoring/building its pinned protobuf tools.

## Module documentation

Every module directory must contain a `README.md` that records:

- production entry and observable contract;
- Domain and Level for every Test ID;
- dependencies, fakes, timeout, and cleanup;
- local runner, CI job, and report;
- known gaps that are not being claimed.

New tests must be explicitly registered in MSBuild, CMake/CTest, Node's exact
level file lists, or the PowerShell runner. Directory glob discovery is not
accepted. Run `scripts/windows-local.ps1 -Task CheckTestStructure` to verify
source registration, exact Qt Level/report mapping, npm and runner file-list
parity, per-Test-ID PowerShell registration, and Module README presence.

The permanent regression policy, current coverage gaps, and admission rules
for cache, group chat, file/media, audio/video, and LAN modules are defined in
[`REGRESSION.md`](REGRESSION.md).
