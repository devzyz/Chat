---
phase: 3B-integration
plan: 05
subsystem: integration-closeout
tags: [formal-executables, production-composition, junit, regression, powershell, dg-25]

requires:
  - phase: 3B-00..04
    provides: IntegrationHost, process harness, Gate HTTP, Status gRPC, Chat TCP, and Qt HTTP/TCP transport contracts
  - phase: 3A-01..05
    provides: shared LogicDispatcher, ChatSessionState, GateRequest, StatusRouting, and Qt auth-state Modules
provides:
  - Six actual T09-COMP black-box formal executable contracts without fake dependencies
  - Exact thirteen-report/313-case local regression manifest preserving the twelve-report/232-case floor
  - Report failure/error/skip/unavailable/timeout, cleanup, residue, secret, and workflow closeout gates
  - Single successful local Release RunAllTests closeout evidence
affects: [phase-3c, G-008, G-009, G-010, G-011, G-015, develop-ci]

tech-stack:
  added: []
  patterns: [black-box formal executable composition, explicit real-dependency boundary, emitted-report accounting, reproducible dependency-tree fingerprint]

key-files:
  created:
    - tests/server/integration-host/production_composition_tests.cpp
    - tests/plans/PHASE-3B-05-SUMMARY.md
    - tests/plans/PHASE-3B-SUMMARY.md
  modified:
    - tests/server/ServerIntegrationTests.vcxproj
    - scripts/windows-local.ps1
    - tests/CI-GOVERNANCE.md
    - tests/REGRESSION.md
    - tests/TEST-CONTRACT-MATRIX.md
    - tests/server/integration-host/README.md

key-decisions:
  - "Gate and Status formal EXEs prove real protocol readiness and graceful bounded shutdown; Chat records the G-015/Phase 3C real-dependency stop instead of fabricating readiness."
  - "Only T09-COMP-01..06 are emitted; frozen T09-COMP-07..08 remain planned-only structure identifiers."
  - "Local report truth is thirteen reports and 313 cases while the original twelve-report/232-case floor remains explicit."
  - "Local success does not substitute for the four remote PR checks or post-merge develop evidence."

patterns-established:
  - "Formal composition tests launch production EXEs through owned processes, dynamic numeric loopback, hard deadlines, and residue-free teardown."
  - "Closeout audits emitted XML rather than planned counts and rejects incomplete or credential-shaped evidence."

requirements-completed: []

duration: 55m
completed: 2026-09-07
---

# Phase 3B Plan 05: Formal Composition and Local Closeout Summary

**Production Gate/Status/Chat executable composition is black-box protected by six actual contracts and a single green thirteen-report/313-case local Release closeout, without fake dependencies or external services.**

## Performance

- **Duration:** approximately 55 minutes
- **Started:** 2026-09-07T00:46:18+08:00
- **Completed:** 2026-09-07T01:41:00+08:00
- **Tasks:** 3
- **Execution branch:** `worktree-agent-3b-05-execution`
- **Starting parent:** `94a03c0`

## Accomplishments

- Added six actual `T09-COMP-01..06` cases. Gate and Status prove explicit config selection, fail-closed invalid config, real HTTP/gRPC readiness, graceful signal, bounded stop, and port release.
- Kept Chat honest at the G-015/Phase 3C boundary: the formal EXE receives no fake dependency switch or credential, never reports synthetic ready, terminates nonzero under an owned bound, and releases pipes/ports/temp state.
- Bound formal and Integration projects to the same Gate/Status/Chat production transport and Phase 3A business targets; production composition selects real adapters.
- Advanced emitted evidence from 13 reports/307 cases to 13 reports/313 cases, while retaining the original 12-report/232-case floor and the four unchanged workflow check names.
- Completed the phase's only full local `RunAllTests -Configuration Release` invocation successfully.

## Task Commits

1. **Task 1 RED:** `7b537878f3feaad407f70460b1ad4fc29b720af9` — `test(3B-05): register failing formal composition contracts`
2. **Task 1 GREEN:** `e7eb8caa3ab3d44f4d1104731086b5aced6a13c4` — `feat(3B-05): protect formal production composition`
3. **Task 2:** `cff45674e3caafa7b6aa54c3f6fa1a6bf759f813` — `test(3B-05): close formal regression reporting`
4. **Task 3:** this Summary/closeout commit

## TDD and Mutation Evidence

- Initial RED registration failed because `production_composition_tests.cpp` did not exist.
- The first scoped behavioral run exposed an incorrect Chat expectation; the corrected contract explicitly records the real-dependency boundary and the final focused suite passed 6/6.
- A fake-dependency token inserted into `GateServer.cpp` made the structure guard fail precisely, then the file was restored to SHA-256 `A648C44A203A05A50021092EEB9CD428E3275AADA7856E0BC7D311C73273E07B`.
- The Task 2 closeout probe disconnected `ServerIntegrationTests` from shared `GateTransport`; the registration assertion returned nonzero, then the project restored exactly to SHA-256 `56D34BD20692F6203667075EEAAF71084A60B996B6EB936F7EFD25F4DACDCCC3`.

## Verification and Reports

The phase's only full local `RunAllTests` ran once in Release with the fixed DG-25 roots and exited 0 in 106.3 seconds. No automatic retry occurred.

| JUnit report | Cases | Failure | Error | Skipped/unavailable | Secret marker |
| --- | ---: | ---: | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 | 0 | 0 |
| `server_component.xml` | 56 | 0 | 0 | 0 | 0 |
| `server_integration.xml` | 93 | 0 | 0 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 | 0 | 0 |
| `client_unit.xml` | 18 | 0 | 0 | 0 | 0 |
| `client_component.xml` | 6 | 0 | 0 | 0 | 0 |
| `client_integration.xml` | 22 | 0 | 0 | 0 | 0 |
| `varify_unit.xml` | 18 | 0 | 0 | 0 | 0 |
| `varify_integration.xml` | 11 | 0 | 0 | 0 | 0 |
| `script_component.xml` | 9 | 0 | 0 | 0 | 0 |
| `script_integration.xml` | 4 | 0 | 0 | 0 | 0 |
| **Total** | **313** | **0** | **0** | **0** | **0** |

The report audit also found every required cleanup-evidence case. Phase-added diff lines contained zero credential-shaped assignments. Formal/test process residue was zero. The aggregate runner reported no new run-owned residue across all registered prefixes; one unrelated `chat-instance-validation-*` directory created on 2026-08-25 predates this plan and was deliberately left untouched.

## DG-25 Dependency Immutability

Every MSBuild path set `VcpkgManifestInstall=false` and used `D:\git\Chat\vcpkg_installed`. No restore, install, remove, update, upgrade, dependency clean/rebuild, identity change, substitute tree, dependency-tree copy, or junction command ran.

The reproducible cross-plan metadata projection uses resolved root, recursive `-File -Force`, `FileInfo` sort by full name, leading-separator-preserving relative full name, decimal length, decimal UTC ticks, LF join without trailing LF, UTF-8 without BOM, and SHA-256. Both current post trees exactly match the 3B-04 anchors that predate this plan:

| Root | Files | Prior/current fingerprint | Max UTC mtime |
| --- | ---: | --- | --- |
| `D:\vcpkg\test-vcpkg` | 15714 | `D98B4CB1B0DF4EC400E3861BCD1A1D4868711688C8A2AC61CA9240CB9874827F` | `2026-08-21T14:14:40.0624754Z` |
| `D:\git\Chat\vcpkg_installed` | 25731 | `3A06E6B6E266438516C99B5222BB274036B962904AFD575F7D7606181DC7612F` | `2026-08-31T14:07:37.8361570Z` |

The current-turn preflight also emitted `CC2939CECD4555210F25E5BA045295B5028A7FFD59852CDE17C4B39B7A96CB2A` and `85BFDD5494FB3E8428AC539B086AE5E0F1B1D4F40AE292EC5B080850138F3AFC`, but its serialization command was lost during context compaction. Those two values are recorded for transparency and are not claimed as reverified. The older reproducible anchors provide the stronger cross-plan no-change proof.

## Deviations from Plan

### Auto-fixed Issues

1. **[Rule 1 - Test correctness] Chat formal dependency expectation was too prescriptive.** The first test required a specific graceful/escalation detail not guaranteed by the real dependency failure path. The contract now requires the stable public boundary: no fake ready, owned bounded stop, stable nonzero outcome, sanitized evidence, and cleanup.
2. **[Rule 3 - Verification environment] Incremental cleanup removed app-local DLLs.** One invocation stopped at Windows loader error `0xC0000135` before GoogleTest entered or emitted a report. The fixed installed `bin` directory was prepended only to that process PATH; no dependency tree was copied or changed.
3. **[Rule 1 - Structure guard] Windows paths were interpreted as regex escapes.** The sole standalone `CheckTestStructure` call failed on malformed `\p`; all new path patterns now use `Regex.Escape`. An equivalent registration audit passed, and the corrected complete structure gate passed inside the single `RunAllTests`.
4. **[Rule 1 - Report guard] StrictMode rejected an absent optional JUnit attribute.** The sole standalone `CheckTestReports` call found that some valid testcase nodes omit `result`; the guard now uses DOM `GetAttribute()`. Independent 13/313 audit passed, and the corrected report gate passed inside `RunAllTests`.

**Total deviations:** four auto-fixed correctness/environment issues. None added a fake service, external dependency, public endpoint, fixed port, or architectural surface.

## Known Stubs

None. No placeholder, TODO/FIXME behavior, fake dependency mode, test macro, or test-only production Interface was introduced.

## Threat Surface

No unplanned threat surface was introduced. Formal processes are black-boxed only through explicit config, dynamic numeric loopback, owned deadlines, sanitized captured output, and bounded cleanup.

## Local/Remote Completion Boundary

Local Plan 3B-05 implementation and Phase 3B regression closeout are complete. Remote work is intentionally pending:

- the PR must run the unchanged `Static configuration checks`, `Server Release build`, `Qt client Release`, and `VarifyServer dependency and package check` checks;
- artifacts must remain `if: always()` with `if-no-files-found: error`, and no job may use `continue-on-error`;
- post-merge `develop` evidence remains pending.

This Summary does not claim remote success and does not close G-008, G-009, G-010, G-011, or G-015. Real Redis/MySQL/SMTP and four-process business orchestration remain owned by Phase 3C and later plans.

## Self-Check: PASSED

- All three pre-Summary commits exist in the required order, and every created key file exists.
- Focused T09-COMP is 6/6; the sole full Release `RunAllTests` is green at 13 reports / 313 cases.
- Both mutations were restored exactly, the reproducible DG-25 anchors match, production/runner stub scan is clean, staged state is empty, and only the two orchestrator-owned deferred documents remain modified outside this Summary commit.

---
*Phase: 3B-integration / Plan 05*
*Completed locally: 2026-09-07*
