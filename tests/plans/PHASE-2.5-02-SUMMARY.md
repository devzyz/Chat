---
phase: "2.5"
plan: "02"
subsystem: protocol-testing
tags: [protobuf, grpc, descriptor, compatibility, cpp, node]
requires:
  - phase: "2.5-01"
    provides: layered JUnit reports and structural registration checks
provides:
  - Three service-owned canonical proto authorities consumed by Gate, Status, Chat, and Varify
  - Reproducible C++ generation with committed-output drift detection
  - Initial descriptor and old-wire fixture compatibility baseline
  - Real C++ client to Node Varify dynamic-loopback interoperability coverage
affects: [phase-2.5, grpc-deadlines, clean-ci, release-compatibility]
tech-stack:
  added: []
  patterns: [service-owned canonical proto, semantic descriptor comparison, independent wire golden, bounded cross-language loopback]
key-files:
  created:
    - proto/varify.proto
    - proto/status.proto
    - proto/chat.proto
    - scripts/protocol-compatibility.js
    - scripts/generate-protocol-fixtures.js
    - tests/server/protocol/fixtures/initial-release-descriptor.pb
    - tests/server/protocol/cpp_node_varify_loopback_tests.cpp
  modified:
    - scripts/windows-local.ps1
    - GateServer/GateServer/GateServer.vcxproj
    - StatusServer/StatusServer/StatusServer.vcxproj
    - ChatServer/ChatServer/ChatServer.vcxproj
    - VarifyServer/proto.js
    - VarifyServer/test/protocol/protocol.test.js
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - "DG-04=A: proto/varify.proto, status.proto, and chat.proto are the only editable authorities; generated code is centralized output."
  - "The initial baseline is a binary descriptor generated from migration-preexisting Git blob a8a34ebd17d5378376cf611762e5943a4c1bff45."
  - "C++ client to Node Varify is owned by Server Integration so the pinned C++ toolchain and Node production loader meet in one deterministic lane."
patterns-established:
  - "CheckProtocols regenerates C++ in isolation, compares bytes, and then compares descriptor semantics."
  - "Compatibility fixtures are derived from the prior descriptor, contain only reserved .test identity data, and exercise unknown fields."
requirements-completed: [G-002]
duration: 1h 05m
completed: 2026-08-25
---

# Phase 2.5 Plan 02: Cross-Service Protocol Compatibility Summary

**Three service-owned canonical protobuf contracts now drive every C++/Node consumer, with semantic release-baseline checks, old-wire fixtures, generated drift detection, and a bounded C++→Node Varify loopback.**

## Performance

- **Duration:** 1h 05m
- **Started:** 2026-08-25T14:27:00Z
- **Completed:** 2026-08-25T15:32:16Z
- **Tasks:** 6
- **Runner testcases:** 101 total, up from 95

## Accomplishments

- Replaced four independently editable `message.proto` files with `proto/varify.proto`, `proto/status.proto`, and `proto/chat.proto` while retaining `package message` and every existing service/RPC/message/field wire identity.
- Routed Gate to Varify+Status generated code, Status to Status only, Chat to Status+Chat, and Node Varify directly to the canonical Varify source. All three production executables linked in Release.
- Added pinned generation and compatibility commands. `CheckProtocols` validates protobuf 6.33.4/protoc 33.4, gRPC 1.76.0, Node loader lock versions, all twelve generated C++ outputs, and descriptor semantics.
- Generated an initial binary descriptor from the migration-preexisting Git blob because no independently auditable prior-release descriptor existed.
- Added descriptor-derived `.test` wire fixtures consumed by both C++ and Node, including an unknown field 99 payload.
- Added a real C++ Stub → Node production `createServer` gRPC call on `127.0.0.1:0` with a 2-second client deadline and bounded teardown.

## TDD RED/GREEN Evidence

1. **Canonical authority:** RED with `missing canonical proto/varify.proto` (2 pass, 1 fail); GREEN after the three-source migration (3/3).
2. **Generation/descriptor command:** RED with `MODULE_NOT_FOUND scripts/protocol-compatibility.js`; GREEN after pinned generation, byte-drift, and semantic compatibility implementation.
3. **Breaking mutations:** an isolated `GetVarifyReq.email` field-number change and isolated `GetVarifyCode` RPC rename both returned nonzero with field/RPC diagnostics; restored canonical sources returned GREEN.
4. **Old wire/unknown fields:** Node RED with fixture `ENOENT`; C++ RED with `input.is_open() == false`; descriptor-derived fixtures made both consumers GREEN.
5. **Cross-language loopback:** RED because the C++ client wiring/helper was missing; final Server Integration GTest built and passed against the Node production server in 362 ms.

No test was deleted, weakened, retried for green, or changed to use same-version round-trip as its only expected source.

## Toolchain and Generation Sources

| Tool | Fixed version/source |
| --- | --- |
| protobuf / protoc | vcpkg baseline `fc3be1ebea7eaeb3071fe716ac65713af1f3a146`; protobuf 6.33.4 port 1; `libprotoc 33.4` |
| gRPC / grpc_cpp_plugin | same vcpkg baseline; gRPC 1.76.0 port 1 |
| Node loader | `@grpc/grpc-js` 1.14.3; `@grpc/proto-loader` 0.8.0; `protobufjs` 7.5.5 from `package-lock.json` |
| Node runtime | CI pins Node 22; local verification used Node 24.18.1 |
| Initial descriptor | migration-preexisting `ChatServer/ChatServer/message.proto` blob `a8a34ebd17d5378376cf611762e5943a4c1bff45` |

Public commands:

```powershell
.\scripts\windows-local.ps1 -Task GenerateProtocols
.\scripts\windows-local.ps1 -Task CheckProtocols
node.exe .\scripts\protocol-compatibility.js create-initial-baseline
node.exe .\scripts\generate-protocol-fixtures.js
```

## Testcase and Report Changes

- Server: 48 → 50. `server_unit.xml` is 35 (adds F02-PROTO-03); `server_integration.xml` is 8 (adds T05-GRPC-02).
- VarifyServer: 25 → 29. `varify_unit.xml` is 18 (adds V02-PROTO-03..06); Integration remains 11.
- Qt remains 9 and PowerShell remains 13.
- All 11 reports total 101 testcases, 0 failures, and 0 errors.

## Task Commits

No staging or commits were created, as required for the shared dirty workspace.

## Files Created/Modified

- `proto/*` and `generated/proto/cpp/*` - canonical sources, module documentation, and mechanically generated C++ outputs.
- Gate/Status/Chat project files and protocol include headers - exact consumer mapping; service-local proto/generated copies removed.
- `VarifyServer/proto.js` and `VarifyServer/test/protocol/*` - canonical Node loading, descriptor/fixture tests, and the owned loopback server helper.
- `scripts/protocol-compatibility.js`, `scripts/generate-protocol-fixtures.js`, `scripts/windows-local.ps1`, and `ChatServer/ChatServer/start.bat` - reproducible public generation/check entry points.
- `tests/server/protocol/*`, `ServerUnitTests.vcxproj`, and `ServerIntegrationTests.vcxproj` - C++ fixture and cross-language tests.
- `tests/README.md`, `tests/server/README.md`, and `tests/TEST-CONTRACT-MATRIX.md` - IDs, levels, reports, lane, and exact 101-testcase baseline.

## Decisions Made

- Centralized generated C++ files under `generated/proto/cpp` so consumers share deterministic outputs without reintroducing editable protocol copies.
- Removed StatusServer's unreferenced `ChatGrpcClient.h/.cpp`: repository-wide reference search found only self/project registration, and DG-04 requires Status to consume Status alone. Retaining it would force an unowned Chat dependency for dead code.
- Kept the cross-language case in Server Integration rather than Varify's Node-only job so real generated C++ and the Node production loader execute together without adding a second C++ restore to the Varify lane.
- Used a repository-local descriptor decoder in the compatibility command so Server CI does not depend on `npm ci`; Node loader versions are still audited from the lockfile.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 2 - Missing Critical] Removed a dead Status-to-Chat generated dependency**

- **Found during:** consumer migration
- **Issue:** `StatusServer/ChatGrpcClient` had no production references but depended on the old empty `ChatService`; keeping it contradicted DG-04's Status-only ownership.
- **Fix:** Deleted the two precisely verified dead files and their project/filter registration.
- **Verification:** StatusServer Release linked successfully using only `status.pb`/`status.grpc.pb`.
- **Committed in:** Not committed by instruction.

**2. [Rule 3 - Blocking] Made descriptor checks independent of installed Node packages**

- **Found during:** CI-lane integration
- **Issue:** Server CI owns the pinned protoc/plugin tools but does not run `npm ci`; using protobufjs at runtime would make the compatibility gate unavailable there.
- **Fix:** Added a narrow repository-local FileDescriptorSet decoder and audited loader versions from `package-lock.json`.
- **Verification:** `CheckProtocols` and all six Varify protocol tests pass without using protobufjs for the compatibility process.
- **Committed in:** Not committed by instruction.

---

**Total deviations:** 2 auto-fixed (1 missing critical, 1 blocking).
**Impact on plan:** Both changes enforce DG-04 and keep the required develop gate reproducible; no later-phase business behavior was added.

## Issues Encountered

- Two aggregate three-target build attempts exceeded their explicit 180-second command limits without producing a terminal result. Only the exact owned MSBuild/compiler processes were stopped. No retry of the same aggregate command was made; isolated incremental builds then proved Gate (1.0s), Status (0.9s), and Chat (96.1s) Release linking separately.
- The first build invocation used `D:\vcpkg` rather than the actual pinned checkout `D:\vcpkg\vcpkg`; the isolated direct builds and final runner used the verified checkout.
- MSVC emitted existing third-party protobuf/gRPC DLL-interface/deprecation warnings; no errors were produced and the warnings were not suppressed.

## Verification

- `CheckTestStructure`: GREEN; 9 Server, 2 Qt, 6 VarifyServer, and 2 PowerShell sources registered.
- `GenerateProtocols` then `CheckProtocols`: GREEN; generated byte drift absent and descriptor compatible.
- GateServer, StatusServer, ChatServer isolated Release builds: GREEN; all three executables linked.
- `RunServerTests -Configuration Release`: GREEN in its single final invocation; Server 50/50.
- `RunVarifyTests -Configuration Release`: GREEN; VarifyServer 29/29.
- All report XML: 101 testcases, 0 failure elements, 0 error elements.
- Fixture/report audit: only schema identifier names, explicitly non-disclosure test names, and reserved `example.test` identities matched; no Token value, credential, verification value, or real email was found.
- `git diff --check`: GREEN; informational LF→CRLF worktree notices only.

## Known Stubs

None. The loopback Node handler intentionally returns an empty `code` so a verification value never enters test output; the test asserts this as a safety property and it does not flow into production behavior.

## User Setup Required

None. Tests use only pinned repository dependencies, dynamic loopback, temporary directories, and fake `.test` identities.

## Recovery Audit — 2026-08-26

- Confirmed the develop path is `windows-ci.yml` → `RunServerTests` → `Invoke-ProtocolCompatibility 'check'`; no duplicate workflow protocol step was added. `RunAllTests` reaches the same owning runner.
- Added `CheckTestStructure` contracts for both runner links, the develop push/pull-request triggers, and the CI invocations of `CheckTestStructure` and `RunServerTests`. An isolated runner mutation that removed the protocol call was accepted before the fix and rejected afterward.
- Hardened `CheckProtocols` to reject unregistered canonical `.proto` authorities, restored service-local `message.proto` copies, and unexpected stale generated `.pb.cc/.pb.h` files in addition to missing or byte-drifted outputs.
- Added a persistent Node mutation case for an unregistered canonical authority. Isolated mutations for an extra canonical proto and a thirteenth generated output were both accepted before hardening and rejected afterward.
- Focused recovery verification passed: `CheckProtocols`, `CheckTestStructure`, `node --check scripts/protocol-compatibility.js`, the 6-case Varify protocol module, and `git diff --check` excluding the explicitly protected local config/test-template paths.
- The supervising audit also ran the unchanged owning suites successfully: Server 50/50, VarifyServer 29/29, and all reports 101 testcases with 0 failures/errors.

## Next Phase Readiness

- G-002 is closed for the develop gate. Plan 2.5-05 can build deadline/pool contracts on the canonical generated services without changing this test-only 2-second deadline.
- A future formal release must replace/promote the initial migration descriptor with the audited release artifact and extend the current/previous release compatibility matrix.

## Self-Check: PASSED

- All canonical proto sources, the two repository tools, all twelve generated C++ files, the baseline and golden fixtures, the C++/Node loopback harness, and this Summary exist.
- The four former service-local editable proto sources are absent, and no stale project or production include references remain.
- The eleven JUnit XML files contain exactly 101 testcase elements, 0 failure elements, and 0 error elements, matching the updated matrix baseline.
- No commit hashes are claimed because the shared-workspace execution contract explicitly prohibited staging and commits.

---
*Phase: 2.5 regression-baseline-hardening*
*Completed: 2026-08-25*
