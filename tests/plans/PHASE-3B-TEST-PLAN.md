# Phase 3B Test Contract Freeze

Status: **Plan 3B-00 contract freeze; T09-HOST is the first implementation slice**

This document freezes Phase 3B identifiers, ownership and shared production target paths before transport behavior is added. The Phase 3A completion baseline is **12 reports / 232 runner testcases**. Planned Phase 3B identifiers are not part of that count until a real source, target registration and emitted JUnit testcase all exist.

## Phase 3A completion prerequisite

- `PHASE-3A-PLAN.md` marks Phase 3A complete on 2026-09-03 and routes execution to Plan 3B-00.
- `PHASE-3A-06-SUMMARY.md` records one green 232/232 local Release lane, clean PR #2 checks, protected merge `d04512e864890303f4c38ec8ca0a78e35e8c671c`, and the successful exact-SHA develop run `33750918123`.
- The exact report manifest remains twelve reports. `server_integration.xml` currently owns 34 cases; no Phase 3B case is counted before it is emitted.
- G-008/G-009/G-011 retain only their completed Phase 3A portions. Their real transport work, G-010 process transport work and G-015 composition work remain open.

## Upstream production Interface and target map

| Interface | Public header | Shared production target | Formal caller | Existing test caller |
| --- | --- | --- | --- | --- |
| `LogicDispatcher::Submit/Stop` | `ChatServer/ChatServer/LogicDispatcher.h` | `ChatServer/ChatServer/LogicDispatcher.vcxproj` (`LogicDispatcher`) | `ChatServer.vcxproj` through `LogicSystem`/`CSession` | `ServerUnitTests.vcxproj` |
| `StatusRouting::Assign/Validate` | `StatusServer/StatusServer/StatusRouting.h` | `StatusServer/StatusServer/StatusRouting.vcxproj` (`StatusRouting`) | `StatusServer.vcxproj` through `StatusServiceImpl` | `ServerUnitTests.vcxproj` and `ServerComponentTests.vcxproj` |
| `ChatSessionState::Create/RegisterCurrent/FindCurrent/Close/Send` | `ChatServer/ChatServer/ChatSessionState.h` | `ChatServer/ChatServer/ChatSessionState.vcxproj` (`ChatSessionState`) | `ChatServer.vcxproj` through `CServer`/`CSession`/`UserMgr` | `ServerComponentTests.vcxproj` |
| `gate::GateRequest::Handle` | `GateServer/GateServer/GateRequest.h` | `GateServer/GateServer/GateRequest.vcxproj` (`GateRequest`) | `GateServer.vcxproj` through `LogicSystem`/`GateResponse` | `ServerComponentTests.vcxproj` |
| `AuthFlowCoordinator::Reduce` | `chat/authflowcoordinator.h` | CMake target `chat_auth_flow` in `chat/CMakeLists.txt` | Qt `chat` executable | `auth_flow_tests` and `auth_flow_component_tests` |

These names are the actual upstream names. Phase 3B must not introduce aliases or wrapper Modules for cosmetic consistency. The MSVC Server composition path and the Qt MinGW composition path remain separate toolchain targets while reusing their respective production Modules.

## Planned Test ID registry

| Plan | Planned IDs | Domain / Level | Hard timeout | Owning report | Focused RED trigger | Required meaningful mutation | Remaining 3C/3D gap |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 3B-00 | `T09-HOST-01..06` | Architecture / Integration | 10 s per case | existing `server_integration.xml` | Missing loopback/dependency/deadline validation or missing bound/ready/stop/cleanup observation | Accept a non-loopback endpoint or replace a shared target reference with a duplicate production `.cpp` compile | Real dependency processes and persistence remain 3C; cross-instance business E2E remains 3D |
| 3B-01 | `T09-PROC-01..12` | Architecture / Integration | 10 s per case; bounded child stop | existing `server_integration.xml` | Run identity, ready, evidence or cleanup contract is absent | Reuse an identity/port, adopt pre-existing state, or accept stale PID identity | Disposable dependency processes remain 3C |
| 3B-02 | `T09-GHTTP-01..12` | Architecture/Business / Integration | 10 s per case | existing `server_integration.xml` | Gate request cannot cross the production Beast transport into `GateRequest` | Accept max+1/malformed input, disconnect the shared target, or allow duplicate/late completion | Real Redis/MySQL/Varify/Status/SMTP Adapters remain 3C |
| 3B-02 | `Q04-HTTP-01..10` | Architecture/Business / Integration | 10 s per CTest | `client_integration.xml` only after real emission | QNAM transport cannot produce one bounded auth-flow outcome | Accept an oversized response or allow a cancelled/old flow to complete | Complete registration/login journey remains 3C/3D |
| 3B-03 | `T09-SGRPC-01..12` | Architecture/Business / Integration | 10 s per case | existing `server_integration.xml` | Generated Status stub cannot reach production `StatusRouting` | Ignore deadline/cancel, accept invalid bounds, or allow late completion | Real Redis Adapter and four-process topology remain 3C |
| 3B-04 | `T09-CTCP-01..16` | Architecture / Integration | 10 s per case | existing `server_integration.xml` | Raw loopback bytes cannot traverse production accept/session/frame/dispatcher | Break fragmentation/coalescing, max+1 rejection, generation suppression or stop cleanup | Redis presence Integration remains 3C; cross-instance chat E2E remains 3D |
| 3B-04 | `Q04-TCP-01..12` | Architecture/Business / Integration | 10 s per CTest | `client_integration.xml` only after real emission | QTcpSocket transport cannot produce bounded frame/auth outcomes | Accept an oversized frame or allow an old socket generation to complete | Complete login/reconnect and cross-instance chat remain 3C/3D |
| 3B-05 | `T09-COMP-01..08` | Architecture / Integration | 15 s per case | existing `server_integration.xml` plus structure gate | Formal EXE composition, ready/stop or registration integrity is disconnected | Remove one shared reference, report registration or cleanup gate in an isolated probe | Real disposable dependencies remain 3C; business E2E remains 3D |

Every range above is unique. Unused members remain planned-only; range width is never used to infer a runner testcase count.

## T09-HOST contract

| Test ID | Observable contract |
| --- | --- |
| `T09-HOST-01` | A host specification rejects any endpoint that is not numeric loopback. |
| `T09-HOST-02` | Construction requires the concrete upstream production dependency object for the selected host family. |
| `T09-HOST-03` | Construction rejects a missing, expired or unbounded owned deadline. |
| `T09-HOST-04` | A started handle exposes its actual bound loopback endpoint and a protocol-ready probe. |
| `T09-HOST-05` | `Stop(deadline)` is bounded and idempotent and preserves the first cleanup result. |
| `T09-HOST-06` | Destruction invokes bounded stop when needed and publishes cleanup success/failure instead of detaching. |

`integration::IntegrationHostFactory` is a test composition root. It may validate configuration, retain concrete production Modules, construct a registered production transport factory and own reverse teardown. It must not parse a protocol, shape a business error, select a server, expose a private container, add a fake formal-EXE mode, or compile a second copy of a production implementation.

## Registration and evidence rules

- `ServerIntegrationTests.vcxproj` must reference the same `LogicDispatcher`, `ChatSessionState`, `GateRequest` and `StatusRouting` projects as the formal Server executables and must not compile their implementation `.cpp` files directly.
- Qt transport tests must link the same CMake production targets as the Qt executable; MSVC does not substitute for the Qt MinGW target.
- T09-HOST implementation is registered only after its RED has compiled against the intended Interface. Later ranges remain documentation-only until their owning plans create real non-empty sources.
- The owning Server runner executes once after Plan 3B-00 code stabilizes. `RunAllTests` remains reserved for 3B-05.
- No test in this phase may reach real Redis, MySQL, SMTP, a public endpoint or a fixed shared port. Synthetic values must not enter JUnit or logs.
