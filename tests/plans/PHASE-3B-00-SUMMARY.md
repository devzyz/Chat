---
phase: 3B
plan: 00
subsystem: integration-test-foundation
tags: [integration-host, mutation-testing, protocol-compatibility, windows-runner, dg-25]
requires:
  - Phase 3A shared production Modules and twelve-report 232-testcase baseline
  - DG-25 fixed read-only vcpkg dependency tree
provides:
  - Six-case IntegrationHostFactory lifecycle and composition contract
  - Worktree-safe protocol and Windows local-runner compatibility
  - Twelve-report 238-testcase regression baseline
affects: [phase-3b-01, phase-3b-integration, windows-ci]
tech-stack:
  added: []
  patterns: [numeric-loopback host factory, owned absolute deadline, observable idempotent cleanup, fixed-root dependency resolution]
key-files:
  created:
    - tests/support/IntegrationHostFactory.h
    - tests/support/IntegrationHostFactory.cpp
    - tests/server/integration-host/integration_host_contract_tests.cpp
    - tests/server/integration-host/README.md
    - tests/plans/PHASE-3B-00-SUMMARY.md
  modified:
    - tests/server/ServerIntegrationTests.vcxproj
    - scripts/protocol-compatibility.js
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
    - tests/server/README.md
key-decisions:
  - IntegrationHostFactory composes the real Phase 3A Modules through their shared production targets while transport remains an explicit seam for later Phase 3B plans.
  - All dependency resolution remains pinned to the fixed read-only DG-25 root; a linked worktree must not infer a private vcpkg tree.
  - The completed Phase 3A baseline is 232 rather than the stale 180-case planning value, and Plan 3B-00 raises it to 238.
requirements-completed: []
metrics:
  duration: 2h22m
  completed: 2026-09-04
  tasks: 3
  files_modified: 13
  regression_cases: 238
---

# Phase 3B Plan 00: Integration Test Foundation Summary

IntegrationHostFactory now gives later Phase 3B transport plans a bounded, loopback-only composition seam over the real Phase 3A Modules, backed by six lifecycle contracts and a portable 238-case regression lane.

## Performance

- **Duration:** 2h22m
- **Started:** 2026-09-04T20:22:43+08:00
- **Completed:** 2026-09-04T22:43:55+08:00
- **Tasks:** 3/3
- **Plan files changed:** 13

## Accomplishments

- Reconciled the stale 180-case planning assumption with the completed 232-case Phase 3A upstream baseline, then registered the six new `T09-HOST-01..06` cases for a 238-case total.
- Added `IntegrationHostFactory` and its six-case contract over shared `LogicDispatcher`, `ChatSessionState`, `GateRequest`, and `StatusRouting` production targets.
- Enforced numeric loopback endpoints, owned absolute deadlines, protocol-ready observation, actual bound-endpoint exposure, and bounded observable idempotent cleanup.
- Made protocol compatibility and the Windows local runner operate correctly from the linked worktree while preserving the fixed DG-25 dependency root.

## Task Commits

Each task was committed atomically:

1. **Task 1: Freeze the integration test contract** - `c8c6d79` (`docs`)
2. **Task 2: Establish the IntegrationHostFactory contract** - `c53e2b9` (`test`)
3. **Task 3: Finalize integration harness compatibility and evidence** - `fc32b01` (`fix`)

## Contract and Mutation Evidence

- **RED:** The first contract build failed for the expected reason: exactly 10 unresolved `IntegrationHostFactory` implementation symbols.
- **GREEN:** The `T09-HOST` contract passed 6/6 after the factory implementation was added.
- **Meaningful mutation:** Weakening numeric-loopback enforcement produced 5/6 with the precise expected failure at `T09-HOST-01`; restoring the production guard returned the suite to 6/6.
- **Structure:** `CheckTestStructure` passed with 22 Server, 5 Qt, 6 VarifyServer, and 2 PowerShell test sources registered.

## Final Verification Evidence

No verification command was rerun during closeout; the final evidence captured before the checkpoint was accepted as authoritative.

| Gate | Result |
| --- | --- |
| `CheckProtocols` | PASS |
| Targeted `T09-HOST` plus C++ to Node protocol lane | PASS, 7/7 |
| `RunServerTests` | PASS, 172/172 |
| `server_integration.xml` | PASS, 40/40 |
| Twelve-report regression baseline | 238 testcases |
| DG-25 dependency-tree writes | 0 |
| Task processes and known-prefix residue | Clean |
| `git diff --check` | PASS |

## Files Created/Modified

- `tests/support/IntegrationHostFactory.h` and `tests/support/IntegrationHostFactory.cpp` - bounded host composition, readiness, endpoint, stop, and cleanup contract.
- `tests/server/integration-host/integration_host_contract_tests.cpp` - six `T09-HOST` lifecycle and composition cases.
- `tests/server/ServerIntegrationTests.vcxproj` - shared production target and host-contract registration.
- `scripts/protocol-compatibility.js` - robust ready-port handling for the cross-language protocol lane.
- `scripts/windows-local.ps1` - structure, report-count, dependency-root, and linked-worktree runner compatibility.
- `tests/TEST-CONTRACT-MATRIX.md`, `tests/server/README.md`, and `tests/server/integration-host/README.md` - frozen ownership, count, scope, and evidence documentation.
- Phase 3A/3B planning documents - corrected upstream baseline and recorded Phase 3B IDs and gates.

## Decisions Made

- The IntegrationHost contract proves composition and lifecycle through a controlled transport; it does not claim that later real HTTP, gRPC, or TCP transport plans are already complete.
- Dynamic numeric loopback binding and explicit readiness are mandatory; fixed shared ports are not part of the test contract.
- The fixed external DG-25 dependency installation remains read-only and authoritative across main and linked-worktree paths.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 3 - Blocking] Corrected linked-worktree vcpkg root resolution**

- **Found during:** Task 3 final runner verification.
- **Issue:** The first runner derived a worktree-local `vcpkg_installed` path, which does not own the pinned dependency installation.
- **Fix:** Routed the runner through the explicit fixed DG-25 root instead of deriving a private worktree root.
- **Verification:** The final server and protocol lanes passed with zero writes to the fixed dependency tree.
- **Committed in:** `fc32b01`.

**2. [Rule 1 - Bug] Stabilized Node ready-port discovery**

- **Found during:** Task 3 final runner verification.
- **Issue:** The second full runner reached 39/40 because the Node protocol-ready port was not discovered reliably.
- **Fix:** Used the targeted diagnostic to correct ready-port handling in the protocol compatibility runner.
- **Verification:** The targeted `T09-HOST` plus C++ to Node lane passed 7/7, followed by `RunServerTests` at 172/172 and `server_integration.xml` at 40/40.
- **Committed in:** `fc32b01`.

---

**Total deviations:** 2 auto-fixed (1 blocking issue, 1 runner bug).

**Impact on plan:** Both corrections were required to make the planned verification reproducible from the isolated worktree; neither expanded the product or transport scope.

## Known Stubs

None. The controlled transport is an intentional contract seam for later Phase 3B plans, not an unwired production placeholder.

## Threat Flags

None. Plan 3B-00 adds a loopback-only test host and runner validation; it does not add a public endpoint, authentication path, persistent schema, or production file-access boundary.

## Next Phase Readiness

- Plan 3B-00 is complete at the 238-case baseline and the IntegrationHost seam is ready for Plan 3B-01 and later real-transport ownership.
- No task process, temporary residue, dependency-tree mutation, or unresolved verification failure remains.

## Self-Check: PASSED

- Task commits `c8c6d79`, `c53e2b9`, and `fc32b01` exist in order on `phase-3b-integration-20260904`.
- All files listed as created or modified by this plan exist.
- Task 3 contains exactly the five reviewed files and no tracked deletion.
- Final verification evidence is complete, `git diff --check` passed, DG-25 recorded zero writes, and task process/residue checks are clean.

---
*Phase: 3B*
*Completed: 2026-09-04*
