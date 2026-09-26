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

## CI and local validation

Git and incremental naming/comment checks use `CheckConventions`; their dedicated
`RunConventionTests` and inventory-only `AuditConventions` entries are documented in
[build/conventions/README.md](build/conventions/README.md). These infrastructure checks run
in Windows static-check and do not change the business report counts below.

[CI-GOVERNANCE.md](CI-GOVERNANCE.md) defines quick/full regression and automatic master release.
Develop PR/push runs existing Windows unit, component and deterministic loopback/process tests.
Master PR/push, weekly develop and manual runs add Linux real-dependency and full business E2E.
Only master push can publish, after full checks and Windows package smoke succeed.

Local module commands remain below; release packaging tests are documented in
[release/contracts/README.md](release/contracts/README.md).
Tests and runners own executable registration; [TEST-CONTRACT-MATRIX.md](TEST-CONTRACT-MATRIX.md) records business coverage.
Historical phase plans remain historical evidence, not extra approval steps for normal development.
Local vcpkg remains read-only under [DG-25](CI-GOVERNANCE.md#101-本机-vcpkg-不可变门禁dg-25).

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
| ChatServer deterministic logic, Status selection, plus Gate/Status Asio contracts | Unit | `server_unit.xml`, `server_gate_unit.xml`, `server_status_unit.xml` |
| ResourceStore restart/resume, ownership, integrity and path validation on temporary files | Integration | `server_resource_integration.xml` |
| ChatServer service-free Redis pool, session registry/send state, Gate response/request orchestration, plus Status token/store behavior | Component | `server_component.xml` |
| Chat/Gate/Status CLI, config, bind, ready, shutdown plus C++→Node Varify, run-owned process harness, and production gRPC-client loopback | Integration | `server_integration.xml`, `server_chat_grpc_integration.xml` |
| Qt frame decoder, message-model rules Q01-MODEL-01..06, auth outcomes Q03-AUTH-01..11 | Unit | `client_unit.xml` |
| Qt message store/delegate Q01-MODEL-07..08, authenticated-session reset Q02-SESSION-01..06, auth abnormal-reset wiring Q03-AUTH-12 | Component | `client_component.xml` |
| Varify protocol, injected handler, fake startup transition | Unit | `varify_unit.xml` |
| Varify config subprocess, loopback gRPC, direct-process bind failure | Integration | `varify_integration.xml` |
| ChatServer instance validation | Component | `script_component.xml` |
| ChatServer instance process lifecycle | Integration | `script_integration.xml` |

All reports are written beneath `build/test-results`. The public local/CI
entry points remain `RunServerTests`, `RunClientTests`, `RunVarifyTests`, and
`RunScriptTests`; `RunAllTests` invokes all four. Each entry runs every level
currently owned by that toolchain. `TestPhase1` is retained only as a
compatibility alias.

Local entries validate registration by default; `RunAllTests` validates it once per process.
In CI, the prerequisite static job runs `CheckTestStructure` once and the four test lanes use
`-SkipTestStructureCheck`. This CI-only switch does not skip tests or report checks.
`RunServerTests` builds both production and test targets; no separate CI `BuildServers` call is needed.

`CheckTestReports` is the no-build integrity audit for the current baseline. It
requires every report and expected testcase count registered in `scripts/windows-local.ps1`,
with zero failure/error/skipped cases.
Every owning runner performs the same per-report checks before returning; the
aggregate runner repeats the exact whole-baseline audit.

Protocol generation and compatibility use the additional public entries
`GenerateProtocols` and `CheckProtocols`. `RunServerTests` also runs the
compatibility/drift check after restoring/building its pinned protobuf tools.

## Module documentation

发布包回归、实际冒烟和自动发布边界见 [发布包验证](release/contracts/README.md)。
发布测试单独报告，不加入 Server/Qt/Varify 计数；master 使用同一次 CI 的 Windows 包，
下载冒烟和上传回验成功后自动发布。旧 R-00 审批/版本占用机制不再使用。

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
