# Phase 3B Integration Summary

Status: **local implementation and regression closeout complete; remote PR checks and post-merge `develop` evidence pending**.

Phase 3B replaced planned transport coverage with real, owned loopback and formal-executable contracts while preserving production ownership. Across Plans 3B-00..05 it delivered:

- `IntegrationHost` composition and bounded lifecycle contracts;
- run-owned process, pipe, deadline, port, and temp-resource harnesses;
- shared production Gate Beast HTTP, Status gRPC, and Chat TCP targets exercised through real protocol bytes/stubs;
- real Qt `QNetworkAccessManager` and `QTcpSocket` transport contracts with generation-safe terminal outcomes;
- black-box GateServer, StatusServer, and ChatServer composition evidence without fake dependencies;
- an emitted local baseline of 13 JUnit reports / 313 cases, all green, while retaining the original 12-report/232-case floor.

The Phase closeout ran `RunAllTests -Configuration Release` exactly once and exited 0. All 313 emitted cases had zero failure, error, skipped/disabled/unavailable/timeout, or credential-shaped marker results. Required cleanup cases were present, the phase diff secret scan was clean, no formal/test process remained, and no new run-owned residue was introduced.

DG-25 remained intact. The two protected dependency trees match reproducible 3B-04 metadata anchors exactly:

- `D:\vcpkg\test-vcpkg`: 15,714 files, `D98B4CB1B0DF4EC400E3861BCD1A1D4868711688C8A2AC61CA9240CB9874827F`.
- `D:\git\Chat\vcpkg_installed`: 25,731 files, `3A06E6B6E266438516C99B5222BB274036B962904AFD575F7D7606181DC7612F`.

The canonical per-plan execution record is [`PHASE-3B-05-SUMMARY.md`](PHASE-3B-05-SUMMARY.md). Plans 3B-00..04 retain their own summaries in this directory.

## Remote CI Checkpoint

Local success does not establish remote success. The next authorized step is to push a PR branch and obtain these four unchanged required checks:

1. `Static configuration checks`
2. `Server Release build`
3. `Qt client Release`
4. `VarifyServer dependency and package check`

Artifact upload must remain `if: always()` / `if-no-files-found: error`, with no `continue-on-error`. After merge, `develop` must provide post-merge evidence. G-008, G-009, G-010, G-011, and G-015 remain open at their explicitly documented real-dependency/business boundaries.

## Self-Check: PASSED

The canonical Plan 3B-05 Summary exists, all local report and dependency evidence above was re-read from emitted artifacts, and remote work is explicitly left pending.

## PR #3 Qt CI repair (2026-09-07)

Run `34074327768` at `c19a766` passed Server, static, and Varify checks but crashed in
`http_transport.finiteDeadlineAbortsAnUnresponsivePeer` (45 other Qt cases passed).
The local focused CTest loop reproduced the same SIGSEGV on repetition 3. GDB located
it in `LoopbackHttpPeer`'s socket-destroyed callback during peer destruction:
the server-owned sockets outlived the `_requests` and `_sockets` members used by that callback.
Declaring `_server` last makes it destruct first, while those containers remain alive.
No production transport, dependency, deadline, or test registration changes are needed.

The existing Q04-HTTP-03 regression passed 100 consecutive Release executions after
the repair; all 46 Qt cases and `CheckTestStructure` also passed. This is local evidence;
the new remote check remains pending. The repair used only the existing Qt toolchain.

PR #4 head `c26f6d6` descends from `c19a766` and changes only Linux/preflight files.
Its Qt fixture is identical, so this is an inherited 3B fixture defect, not a conflict
introduced by Phase 3C. The 3C branch/worktree was not modified by this repair.
