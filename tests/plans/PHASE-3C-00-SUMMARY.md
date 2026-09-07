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

### First authoritative attempt and owning fix

Draft PR #4 run `34119107345`, job `101732755306`, reached the hosted
`ubuntu-24.04` preflight at candidate HEAD `dd8eb0d`. Checkout, Node, Qt,
the pinned vcpkg checkout, and vcpkg bootstrap succeeded. All four existing
contract mutations returned the expected RED. The first stable failure then
occurred before configure/build:

```text
LINUX_PREFLIGHT_BLOCKED: CMake identity mismatch; expected 3.28.3, got 3.31.6
```

The `always()` upload succeeded. Artifact `phase3c-linux-preflight` had ID
`10017496428` and archive SHA-256
`9838e1c552c9cbaa051321ffc1197ff304dc68baf5678e89a6f4aafc483001d5`.
Its JSON correctly reported `LINUX_PREFLIGHT_BLOCKED` at `tool-identities`.
Its 10/0 JUnit only proved the static contract; the absence of bounded startup
logs prevented any PASS claim.

The owning 3C-00 fix keeps CMake 3.28.3 unchanged and acquires it before the
preflight through mature action `lukka/get-cmake` v4.4.2 at immutable commit
`fffaaafeea488556c2c12dad60690008bc1caacb`. The exact input is
`cmakeVersion: "3.28.3"`; the action's otherwise floating Ninja acquisition is
also fixed to 1.12.1. Cloud and local action caches are disabled. Structure
contract mutations now prove that a missing action, floating `v4.4.2` ref, or
3.31.6 identity drift each returns `LINUX_PREFLIGHT_BLOCKED`.

Fix commits:

- `ea703b8` — `test(3c-00): require locked CMake acquisition`
- `49d09c3` — `fix(3c-00): acquire locked CMake on hosted Ubuntu`

No CI rerun was initiated from this executor session.

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

### Second authoritative attempt and owning fix

Draft PR #4 run `34120164172`, job `101736152670`, exercised the first
owning fix at candidate HEAD `780d477`. The locked top-level CMake 3.28.3
acquisition succeeded, as did checkout, Node, Qt, the pinned vcpkg checkout,
vcpkg bootstrap, and all structure mutations. The first stable configure
failure was instead:

```text
fatal: failed to unpack tree object 8c705e8acf87afb971678e50206c65dca9fccedc
.ci/vcpkg/.git: note: vcpkg was cloned as a shallow repository. Try again with a full vcpkg clone.
```

The failure occurred while resolving `boost-context@1.90.0`. The later Unix
Makefiles/compiler message was cascading output, not the owning root cause.
The `always()` artifact `phase3c-linux-preflight` had ID `10017884831`,
archive SHA-256
`f5dd2067200cc65f71071f5a5b29d8dbb4d32db1f53bb8c34f8a14d395be647d`,
and correctly reported `LINUX_PREFLIGHT_BLOCKED` at `cmake-configure`. Its
JUnit remained the 10/0 static contract only; no bounded startup logs existed,
so the attempt supplied no PASS evidence.

The owning fix preserves the exact vcpkg commit
`fc3be1ebea7eaeb3071fe716ac65713af1f3a146`, repository baseline, triplet,
run-owned install root, and every tool identity. It adds `fetch-depth: 0` to
that exact checkout. The structure contract now rejects a missing full-depth
setting, and `scripts/linux-ci.sh` proves both deletion and mutation to
`fetch-depth: 1` return non-zero. Direct GREEN remains 10/10 with
`READY_FOR_HOSTED_PREFLIGHT`; it does not claim hosted PASS.

Fix commits:

- `5e378e6` - `test(3c-00): require full vcpkg history`
- `1816702` - `fix(3c-00): fetch complete pinned vcpkg history`

No push or CI rerun was initiated from this executor session. After these
commits are fast-forwarded to `phase-3c-integration-20260907` and pushed,
dispatch the same `linux-ci.yml` workflow on that ref. The remaining hard gate
is a fresh `ubuntu-24.04` result whose artifact JSON is `PASS`, whose JUnit is
10/0, and whose Gate, Status, Chat, and Varify bounded startup logs are all
present.

### Third authoritative attempt and owning fix

Draft PR #4 run `34121240236`, job `101739505788`, exercised the full-history
checkout fix at candidate HEAD `b5b9444`. Locked CMake 3.28.3, Node, Qt, the
full-history pinned vcpkg checkout, and vcpkg bootstrap all succeeded. The
first stable configure failure was:

```text
mysql-connector-cpp[jdbc] is only supported on 'static', which does not match x64-linux-chat-release.
```

The later Unix Makefiles/compiler messages were cascading output. Using
`--allow-unsupported` would bypass the compatibility gate and is not an
acceptable fix. Artifact `phase3c-linux-preflight` had ID `10018277729` and
digest
`sha256:3e679654e18f9d4f921a3b42a73c2d79a747434cdf919bf2b7f5ba426da068e2`.
Its JSON correctly reported `LINUX_PREFLIGHT_BLOCKED` at `cmake-configure`;
the JUnit was 10/0 static-contract evidence only, and no bounded startup logs
were produced.

The owning fix mirrors the existing Windows release triplet rule: the Linux
triplet stays dynamically linked by default, while only
`mysql-connector-cpp` and its `libmysql` dependency switch to static linkage.
The vcpkg baseline/ref, manifest features, run-owned install root, and tool
identities remain unchanged. T10-LNX-06 first failed alone under RED, then the
direct contract returned 10/10 and `READY_FOR_HOSTED_PREFLIGHT`. Independent
temporary mutations that deleted the exception or broadened it to `hiredis`
both returned non-zero with the named vcpkg contract diagnostic.

Fix commits:

- `8a13f0c` - `test(3c-00): require narrow MySQL static linkage`
- `c3b1b6d` - `fix(3c-00): narrow MySQL linkage on Linux`

No push or CI rerun was initiated from this executor session. A fresh hosted
Ubuntu run remains required for a PASS JSON, 10/0 JUnit, and all four bounded
startup logs.

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
