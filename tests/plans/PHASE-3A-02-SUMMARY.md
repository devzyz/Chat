# Phase 3A Plan 02: Status selection/token Summary

Status routing now provides deterministic least-load assignment and fail-closed token persistence/validation through one production `Assign/Validate` Interface shared by gRPC and 14 Unit/Component contracts.

## Scope delivered

- Added the production `StatusRouting` Module with the only caller-facing operations `Assign(uid)` and `Validate(uid, token)`.
- Hid count reads, strict non-negative decimal parsing, deterministic selection, token generation, token store access, and public error mapping behind the Module.
- Added internal `StatusStore` and token-source seams because each has both a production Adapter (`RedisMgr`/UUID) and deterministic thread-safe test Adapters.
- Rewired `StatusServiceImpl` to delegate to `StatusRouting` and only shape the existing protobuf reply. No proto or public error-code value changed.
- Added a shared `StatusRouting.vcxproj` static library referenced by StatusServer, ServerUnitTests, and ServerComponentTests, and registered it in `Chat.sln`.
- Added T08-STATUS-01..14: eight Unit cases in `server_unit.xml` and six Component cases in `server_component.xml`.
- Updated the public runner and structure gate to require DG-25 build properties, exact Module wiring, exact Test IDs/case split, 68 Unit cases, 30 Component cases, 140 Server cases, and 12 reports / 194 total cases.

## Interface and behavior

- Valid counts are strict non-negative decimals. Missing, empty, negative, malformed, or overflowed values are unknown and rank after valid counts.
- The smallest valid count wins. Equal valid counts and all-unknown sets use runtime `Name` lexicographic order. Empty server lists fail closed.
- `Assign` succeeds only with a non-empty token after `PutToken` returns true. False or exception maps to `RPCFailed` with empty token/host/port.
- `Validate` distinguishes missing UID (`UidInvalid`), mismatch (`TokenInvalid`), and success. Dependency exceptions map to the stable `RPCFailed` envelope.
- Tests use scoped in-memory state and a condition-variable start barrier; no fixed sleep, real Redis, socket, or gRPC transport is used.

## TDD evidence

### RED

1. `T08-STATUS-01` was registered before the Module existed. The focused Release build failed with `C1083: StatusRouting.h: No such file or directory`, proving the empty-list Interface was absent.
2. After selection GREEN, `T08-STATUS-09` ran against the intentionally still-unfixed store path. It failed because `PutToken(false)` produced `Success` and non-empty host/port/token.

### GREEN

- Empty-list focused case: 1/1 passed in 0 ms.
- Selection/concurrency focused Unit group: 8/8 passed in 1 ms.
- Token/validation focused Component group: 6/6 passed in 0 ms.
- Combined focused execution: 14/14 passed under a shared two-second hard bound.

### Meaningful mutation

- Temporarily removed the `PutToken(false)` guard. The focused T08-STATUS-09 case failed nonzero with `Success` and populated public fields.
- Restored the guard exactly; source audit found `if (!store_->PutToken(uid, token))`, and the owning runner passed afterward.
- No package, structure-only, or trivial registration mutation was performed.

## Build and runner evidence

All MSBuild invocations explicitly used:

`/p:VcpkgRoot=D:\vcpkg\test-vcpkg /p:VcpkgTriplet=x64-windows-chat /p:VcpkgHostTriplet=x64-windows /p:VcpkgManifestInstall=false /p:VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\`

| Evidence | Result |
| --- | --- |
| Single DG-25 phase-entry preflight | PASS: fixed tool/installed paths, 16 required header/lib/status artifacts, 5 package status entries; read-only |
| Initial empty-list RED build | Expected nonzero after 79.0 s; missing production Interface |
| Empty-list GREEN build/test | Build passed in 99.6 s; focused 1/1 passed |
| Unit selection build/test | Build passed in 20.7 s; focused 8/8 passed |
| Store-false RED build/test | Build passed in 25.8 s; focused 1/1 failed as expected |
| Component GREEN build/test | Build passed in 16.9 s; focused 6/6 passed |
| Directed `StatusRouting;StatusServer;ServerUnitTests;ServerComponentTests` Release build | PASS in 41.6 s after one compile-only correction |
| Mutation build/test | Build passed; T08-STATUS-09 failed nonzero in 0 ms; restored |
| `CheckTestStructure` | PASS: 19 Server, 3 Qt, 6 VarifyServer, 2 PowerShell sources |
| Single owning `RunServerTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg` | PASS, exit 0, 344.6 s; no retry |

## Report audit

| Report | Testcases | Failures | Errors |
| --- | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 |
| `server_component.xml` | 30 | 0 | 0 |
| `server_integration.xml` | 34 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 |
| **Server total** | **140** | **0** | **0** |

The report family remains six Server reports and twelve aggregate reports. The current aggregate manifest is 194 cases. Neither `SYNTHETIC_STATUS_TOKEN_3A02` nor the synthetic exception marker appeared in Server XML.

## DG-25 evidence

- `D:\vcpkg\test-vcpkg` and `D:\git\Chat\vcpkg_installed` were treated as read-only.
- No restore/install/remove/update/upgrade, manifest install, tree cleanup/rebuild, root/triplet/baseline/tool identity change, or substitute tree occurred.
- The initial sandbox-only build attempt was blocked by Windows SDK user-directory access. The same fixed-tree command was rerun outside the sandbox; this was not a dependency failure or package mutation.
- Focused Component runtime dependencies were copied from the fixed tree with read-only `vcpkg z-applocal`; no package state changed.

## Deviations and corrections

1. Placing the Status include directory before Chat initially shadowed existing `ConfigMgr.h`/`Const.h`. The Unit project was corrected to keep Chat first and Status second; Status tests use explicit stable numeric expectations to avoid ambiguous same-named headers.
2. `StatusServiceImpl` initially bound a `SectionInfo` copy as const, but its existing index operator is non-const. The local copy was made non-const; the directed build then passed.
3. The first marker-audit command used a PowerShell wildcard that `rg` did not expand on Windows. It was corrected to `rg -g 'server_*.xml'`; the corrected audit passed. No test or runner was repeated.

## Scope not verified here

- No real Redis command, disconnect, TTL, cleanup, or persistence Integration; these remain Phase 3C.
- No new real Status gRPC process/business composition; this remains Phase 3B/3C.
- No `RunAllTests`, Qt, Varify, PowerShell full lane, remote CI, PR, push, or Phase 3A closeout claim; Plan 3A-06 owns those checks.

## Cleanup and safety

- No owned build/test process or run-owned temporary residue remained.
- `git diff --check` passed.
- Staged index remained empty.
- Stub scan found no goal-blocking placeholder/TODO/FIXME or UI-flowing empty stub in created/modified production/test files.
- No `.planning/` file was created or modified, and no commit/branch/stash/push/PR/CI action occurred.

## Files

Created: `StatusRouting.h/.cpp`, `StatusRoutingInternal.h`, `StatusRoutingProduction.h/.cpp`, `StatusRouting.vcxproj/.filters`, `tests/server/status-routing/{README.md,status_routing_unit_tests.cpp,status_routing_component_tests.cpp}`.

Modified: `StatusServiceImpl.h/.cpp`, `StatusServer.vcxproj`, `ServerUnitTests.vcxproj`, `ServerComponentTests.vcxproj`, `Chat.sln`, `scripts/windows-local.ps1`, and the Phase 3A regression/plan documentation owned by this plan.

## Commits

None. Per execution instruction, Plan 3A changes remain unstaged and uncommitted for the later 3A-06 closeout.

## Self-Check: PASSED

- All 10 explicitly created Module/test/Summary files exist.
- Final `server_unit.xml` and `server_component.xml` contain 68 and 30 testcases respectively, with zero failure/error nodes.
- The staged index is empty and final `git diff --check` passes.
