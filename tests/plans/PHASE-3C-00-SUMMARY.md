# Phase 3C Plan 00: Hosted Linux Preflight Checkpoint

Status: **CHECKPOINT — authoritative GitHub-hosted Ubuntu preflight pending**

Plan 3C-00 now has a fail-closed, same-source Linux build contract and a
remotely runnable hosted workflow. Local/static evidence is intentionally
limited to `READY_FOR_HOSTED_PREFLIGHT`; this document does not claim Phase
3C-00 complete or Linux release support.

## Delivered implementation

- `linux-x64-release` CMake configure/build/test presets and the
  `x64-linux-chat-release` triplet use the repository baseline with the
  run-owned `.ci/vcpkg_installed` root.
- Canonical `proto/varify.proto`, `proto/status.proto`, and `proto/chat.proto`
  generate one `chat_protocol_cpp` target.
- Gate, Status, and Chat formal executables link their same-source production
  module targets; the existing Qt `chat_network_core`, `chat_message_model`,
  and `chat_session_core` targets are reused through `chat::client_modules`.
- `scripts/linux-ci.sh` owns focused selectors, exact identity checks,
  configure/compile/link, Varify `npm ci`, bounded loader/startup probes, and
  non-PASS evidence on every failure path.
- `.github/workflows/linux-ci.yml` pins `ubuntu-24.04`, action commits, Qt
  6.5.3, Node 22.18.0, the repository vcpkg commit, a hard timeout, an
  `always()` evidence upload, and an explicit downstream `PASS` dependency.

## RED, GREEN, mutation, and runner evidence

| Gate | Result | Evidence |
| --- | --- | --- |
| T1 RED | Expected non-zero | `T10-LNX-01..10` emitted before failure; four failures named missing production ownership, canonical proto generation, Varify production-main startup, and bounded loader/startup evidence. |
| T2 static GREEN | 10/10, zero failure | Direct `cmake -P tests/build/linux_preflight_contract.cmake` returned 0 and wrote `READY_FOR_HOSTED_PREFLIGHT`, never `PASS`. |
| Missing target mutation | Expected non-zero | Same contract returned `LINUX_PREFLIGHT_BLOCKED`. |
| Duplicate production source mutation | Expected non-zero | Same contract returned `LINUX_PREFLIGHT_BLOCKED`. |
| vcpkg baseline drift mutation | Expected non-zero | Same contract returned `LINUX_PREFLIGHT_BLOCKED`. |
| Broken Varify startup mutation | Expected non-zero | Same contract returned `LINUX_PREFLIGHT_BLOCKED`. |
| Owning public selector | Pending | Must run on GitHub-hosted `ubuntu-24.04`; the local Windows `bash.exe` is an unavailable WSL launcher and is not authority under DG-23. |

All mutation copies were created below the task-specific temporary directory
and removed after the checks. No mutation residue remains in the worktree.

## Atomic commits

- `79e2357` — `test(3c-00): freeze Linux preflight contract`
- `310ac9b` — `feat(3c-00): add hosted Linux build preflight`
- `0856bda` — `style(3c-00): normalize preflight file endings`

## Hosted checkpoint

After fast-forwarding these commits to `phase-3c-integration-20260907` and
pushing that branch, dispatch exactly:

```text
gh workflow run linux-ci.yml --ref phase-3c-integration-20260907
```

The authoritative run must make workflow
`Linux real dependencies and compatibility` and job
`Linux configure compile link and startup preflight` succeed without retry.
Artifact `phase3c-linux-preflight` must contain at least:

- `linux-preflight.json` with `status: PASS` and exact observed identities;
- `junit/linux_build_proof.xml` with `T10-LNX-01..10` and zero failures;
- bounded Gate, Status, Chat, and Varify startup logs.

Any configure/install/compile/link/loader/startup, identity, timeout, missing
report, or upload failure is `LINUX_PREFLIGHT_BLOCKED` and returns work to
Plan 3C-00. Plans 3C-01..3C-09 must not execute before this hosted result is
accepted.

## DG-25 and stop-condition audit

No local configure, vcpkg restore/install/remove/update/upgrade/clean, npm
install, dependency-root switch, baseline/triplet change, or tool checkout was
executed. `D:\vcpkg\test-vcpkg` and
`D:\git\Chat\vcpkg_installed` were neither read for build use nor modified.
Only the planned GitHub run-owned dependency paths are configured.

The current stop condition is exactly the missing authoritative hosted
Ubuntu result. Local/static evidence is insufficient by DG-23, so Phase 3C-00
remains open and no downstream Phase 3C completion claim is made.

## Self-Check: PASSED

The implementation commits exist, all eight listed implementation files and
this checkpoint record exist, the branch diff passes `git diff --check`, the
worktree is clean after committing this file, and no tracked deletion was
introduced.
