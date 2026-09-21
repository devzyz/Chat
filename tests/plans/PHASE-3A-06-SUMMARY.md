---
phase: 3A
plan: 06
subsystem: windows-ci-closeout
tags: [regression, junit, github-actions, branch-protection, develop]
requires:
  - Phase 3A-01..05 production Modules and focused evidence
  - 12-report/232-testcase declarative manifest
  - DG-25 fixed local vcpkg immutability
provides:
  - One complete local Release regression across all four public runners
  - Exact twelve-report integrity, secret, residue and registration audits
  - Clean topic-branch PR and protected develop CI evidence
affects: [phase-3b, develop-ci]
tech-stack:
  added: []
  patterns: [single phase closeout lane, exact report manifest, protected PR merge]
key-files:
  created:
    - tests/plans/PHASE-3A-06-SUMMARY.md
  modified:
    - tests/plans/PHASE-3A-PLAN.md
    - README.md
key-decisions:
  - The 232-case baseline remains exactly twelve reports and the four existing Required Check names remain unchanged.
  - Real transport, persistence, cross-instance E2E and release promotion gaps remain owned by Phase 3B/3C/3D/Release.
requirements-completed: []
metrics:
  completed: 2026-09-03
  tasks: 5
  runner_cases: 232
  reports: 12
---

# Phase 3A Plan 06: Regression, Reports and CI Closeout Summary

The complete Phase 3A baseline is green at 232/232 across twelve exact JUnit reports. The protected topic-branch PR and the exact merge-SHA post-merge develop run both passed all four Required Checks.

## Current Status

| Gate | Result |
| --- | --- |
| Local structure and isolated negative probe | PASS |
| One full local Release lane | PASS, 232/232 |
| Topic commit and PR | PASS, PR #2 at `eb6239b0f479f2cc0b76aa7e7b1cf4eeb9e617ad` |
| Current PR HEAD four Required Checks | PASS, run `33646763982` |
| Protected merge | PASS, `d04512e864890303f4c38ec8ca0a78e35e8c671c` |
| Merge-SHA develop push run | PASS, run `33750918123` |

## Protected Remote Closeout

- PR [#2](https://github.com/devzyz/Chat/pull/2) targeted `develop` from
  `phase-3a-ci-closeout-20260902`; its exact HEAD was
  `eb6239b0f479f2cc0b76aa7e7b1cf4eeb9e617ad`.
- Pull-request workflow run [33646763982](https://github.com/devzyz/Chat/actions/runs/33646763982)
  completed successfully with all four Required Checks green.
- Immediately before merge, GitHub reported the PR `OPEN`, `MERGEABLE` and `CLEAN`. Branch protection
  remained strict and admin-enforced, disallowed force pushes and deletions, and required the same four
  exact contexts. PR #2 was merged through the normal protected flow without an admin bypass at
  `2026-09-03T11:39:50Z`.
- The resulting merge SHA is `d04512e864890303f4c38ec8ca0a78e35e8c671c`.
- The exact-SHA `develop` push workflow run
  [33750918123](https://github.com/devzyz/Chat/actions/runs/33750918123) completed successfully at
  `2026-09-03T13:02:19Z`. Its four jobs were:
  - `Static configuration checks`: success;
  - `Server Release build`: success;
  - `Qt client Release`: success;
  - `VarifyServer dependency and package check`: success.
- Local `develop` was fetched and fast-forwarded from
  `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e` to the exact merge SHA. The local-only checkpoint
  `b25effb89782430bf7b3e0f7ef872ed181e28c95` is not an ancestor of `origin/develop`, so its topic
  branch remains preserved and was not pushed or deleted.

## Git and Protected-Path Preflight

- Read-only fetch confirmed `develop`, local HEAD and `origin/develop` at
  `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e` before branch creation.
- The staged index was empty and no merge/rebase was active.
- Topic branch: `phase-3a-ci-closeout-20260902`.
- Protected paths were inspected only as path/size/SHA-256 metadata:
  - `tests/auto/chat-test.md`: 21383 bytes, `4E91D9951016CE9739ED203ECBC0D1924B8CF58A521B49674EB78EB1D0F51179`.
  - `tests/auto/test-strand.md`: 12796 bytes, `BBB3872171442A5D3D2772A79FA82480E6D2B0AAB62E266CA691045E3DE740F4`.
  - `ChatGPT-Proxy.ps1`: 3537 bytes, `1F6E4B6D4C66E5DF7080C59952AA8F7C7B407969B163E7FA556D03132F4AA491`.
  - `.huorong-quarantine-check/`: three files with SHA-256
    `601711F7349AE908C28CBCFD7B394B3BC78DD20B098D3EFE556695ECD0A2E145`,
    `A242DEDD7D56319EE3CA93B5C2F595E3C1EF65083A60CCD876C93964DF690C41`, and
    `7CED4360DBD6967E76E215C3DCA12F74554153F19184168A128738C9DD53525E`.

## Structure and Negative-Probe Evidence

- Normal `CheckTestStructure`: PASS; 21 Server, 5 Qt, 6 VarifyServer and 2 PowerShell sources registered.
- Exactly one negative probe ran in an isolated temporary copy that excluded `.git`, `build`,
  `vcpkg_installed`, all `node_modules`, `.planning`, `tests/auto`, quarantine and proxy paths.
- Removing the copied `ChatServer/ChatServer/LogicDispatcher.vcxproj` production registration made
  `CheckTestStructure` fail nonzero on the missing required project. The temporary copy was then
  removed; the live registration remained present and no probe directory remained.
- The workflow file was unchanged in Phase 3A, so actionlint was correctly not rerun.

## One Full Local Release Lane

The sole effective full command was:

`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows-local.ps1 -Task RunAllTests -Configuration Release -VcpkgRoot D:\vcpkg\test-vcpkg -QtRoot D:\qt\Qt\6.5.3\mingw_64 -MinGwRoot D:\qt\Qt\Tools\mingw1120_64`

It returned exit 0 without retry. `RunAllTests` invoked the four public owning runners and ended with
`Regression report audit passed: 232 testcases across 12 reports.`

| Report | Cases | Failures | Errors |
| --- | ---: | ---: | ---: |
| `server_unit.xml` | 68 | 0 | 0 |
| `server_component.xml` | 56 | 0 | 0 |
| `server_integration.xml` | 34 | 0 | 0 |
| `server_chat_grpc_integration.xml` | 4 | 0 | 0 |
| `server_gate_unit.xml` | 2 | 0 | 0 |
| `server_status_unit.xml` | 2 | 0 | 0 |
| `client_unit.xml` | 18 | 0 | 0 |
| `client_component.xml` | 6 | 0 | 0 |
| `varify_unit.xml` | 18 | 0 | 0 |
| `varify_integration.xml` | 11 | 0 | 0 |
| `script_component.xml` | 9 | 0 | 0 |
| `script_integration.xml` | 4 | 0 | 0 |
| **Total** | **232** | **0** | **0** |

Totals are Server 166, Qt 24, VarifyServer 29 and PowerShell 13. Nine synthetic Phase 3A
password/code/token/email/body/exception markers had zero matches across the reports; no real secret value was printed.

## DG-25, Residue and Documentation Audit

- `RunServerTests` contains `VcpkgManifestInstall=false` and the fixed
  `VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\`; it does not call `RestoreServers`.
- No restore/install/remove/update/upgrade, implicit manifest install, tree cleanup/rebuild,
  root/triplet/baseline/tool identity change or substitute install root occurred.
- The only matching temporary residue was the pre-existing
  `chat-instance-validation-1b60afa800f84384a5a53da57e8ebdad` already recorded by Phase 2.5;
  it was not created, modified or deleted. No new known-prefix residue remained.
- Two MSBuild node-reuse workers created by the full lane were identified by creation time and exact
  command line, then stopped by PID. The unrelated pre-existing Adobe node process was left untouched.
- `git diff --check`: PASS. Relevant Markdown links: zero broken.
- Test sources contained 119 Test ID occurrences and 119 unique IDs. The five Phase 3A ranges appear
  once in the matrix; G-007..G-011 each have one owner row.
- G-007 and G-010 are complete. G-008/G-009/G-011 are complete only for their Phase 3A
  in-memory/outcome portions. Real TCP/HTTP, Redis/MySQL/SMTP, process composition, cross-instance E2E,
  compatibility and artifact/UAT gaps remain open and routed to Phase 3B/3C/3D/Release.

## Staging Boundary

The explicit candidate allowlist contains only Phase 3A production/test Modules, project/CMake/runner
registration, governance and README files, Phase 3A planning/Summaries, and the linked canonical
Phase 3B/3C/3D/Release planning documents. The following remain excluded and unstaged:

- `tests/auto/chat-test.md`, `tests/auto/test-strand.md`;
- `.huorong-quarantine-check/`, `ChatGPT-Proxy.ps1`;
- `build/`, `vcpkg_installed/`, every `node_modules`, `.planning/`, XML reports, temporary directories,
  personal configuration and credentials.

## Deviations from Plan

None affecting product behavior or evidence. The negative probe's outer PowerShell wrapper surfaced
the expected child error record as a nonzero command; its `finally` cleanup completed, the live project
was unchanged and the probe was not repeated.

## Known Stubs

None in Phase 3A production/test changes.

## Threat Flags

None. Phase 3A adds in-process business/state Modules and test registration; no new network endpoint,
authentication trust boundary, file-access path or schema change is claimed by this closeout.

## Self-Check: PHASE 3A COMPLETE

- All local Phase 3A Module, test, README, plan and Summary files exist.
- Structure, full regression, reports, marker, residue, process, link, Test ID and diff audits passed.
- The clean PR checks, protected merge and exact merge-SHA post-merge develop checks all passed.
- Phase 3A is complete. The next execution route is Phase 3B, starting with Plan 3B-00.
