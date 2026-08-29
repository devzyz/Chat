---
phase: "2.5"
plan: "06"
subsystem: qt-session-lifecycle
tags: [qt, session, tcp, decoder, lifecycle, regression]
requires:
  - phase: "2.5-01"
    provides: exact Qt level/report registration and public client runner
  - decision: "DG-02=A"
    provides: strict connection and authenticated-session isolation contract
provides:
  - Production TcpFrameDecoder and TcpMgr connection reset lifecycle
  - Single ClientSession owner for account, pending-batch, and owned UI/model teardown
  - Expected-close versus abnormal-disconnect routing without duplicate transitions
  - Three deterministic Component cases and one additional decoder reset contract
affects: [phase-2.5, qt-client, develop-ci, future-local-cache]
tech-stack:
  added: []
  patterns: [owned-session-root teardown, idempotent lifecycle reset, shared production static library, explicit singleton shutdown]
key-files:
  created:
    - chat/clientsession.h
    - chat/clientsession.cpp
    - chat/tests/session-reset/session_reset_tests.cpp
    - chat/tests/session-reset/README.md
    - tests/plans/PHASE-2.5-06-SUMMARY.md
  modified:
    - chat/tcpframedecoder.h
    - chat/tcpframedecoder.cpp
    - chat/tcpmgr.h
    - chat/tcpmgr.cpp
    - chat/usermgr.h
    - chat/usermgr.cpp
    - chat/mainwindow.h
    - chat/mainwindow.cpp
    - chat/main.cpp
    - chat/singleton.h
    - chat/CMakeLists.txt
    - scripts/windows-local.ps1
    - tests/TEST-CONTRACT-MATRIX.md
key-decisions:
  - "ClientSession is the sole authenticated-session owner; MainWindow registers the real ChatDialog ownership root and routes kicked or abnormal disconnect transitions through resetSession."
  - "TcpMgr owns only connection state and delegates partial-frame state to TcpFrameDecoder; reset clears both decoder and pending text batches before reconnect."
  - "Session reset destroys the old UI/model ownership tree; application-level theme/window/server configuration remains outside account state."
patterns-established:
  - "The Qt executable and Component tests link the same chat_session_core production library."
  - "Singleton QObjects are explicitly released while QApplication still exists instead of relying on static destruction order."
requirements-completed: [G-005]
duration: 45m
completed: 2026-08-29
---

# Phase 2.5 Plan 06: Qt Connection and Session Reset Summary

Qt connection bytes, pending acknowledgements, account maps/cursors, and the complete chat page/model ownership tree now end at a deliberate production lifecycle Interface before another account can begin.

## Accomplishments

- Added `TcpFrameDecoder::reset`; a new connection no longer combines a retained partial frame with fresh bytes.
- Added `TcpMgr::resetConnection`, which stops new sends, aborts an outstanding socket/connect attempt, clears endpoint/decoder/pending-batch state, and runs before reconnect.
- Added `ClientSession::beginSession/resetSession` as the single authenticated-session owner. It invokes the real `TcpMgr` and `UserMgr` Interfaces and destroys the registered `ChatDialog` root, which also destroys `ChatPage`, `MessageModelStore`, heartbeat timer, selection, pagination/loading, scroll-anchor, avatar, and legacy-seed state.
- Added `UserMgr::resetSession` for user/token, friend/apply/chat maps, UID-to-chat mapping, contact cursor, chat cursor, and load flags. No-user `GetUid` now returns the existing sentinel `0` instead of dereferencing null.
- Made reset idempotent and preserved exact `Logout`, `SwitchAccount`, `Kicked`, and `UnexpectedDisconnect` reasons. `TcpMgr::sig_connection_close(bool)` keeps expected closes out of the abnormal timeout-dialog route.
- Moved session/network/user implementations into `chat_session_core`, linked by both the production executable and the Component tests.
- Added explicit application shutdown for QObject singletons while `QApplication` is alive, avoiding platform-dependent static destruction order.

## DG-02 State Contract

Cleared on reset:

- socket/connect attempt, host/port, send acceptance, decoder half-frame bytes, pending text batches;
- authenticated user, token, friend/apply/chat maps and UID-to-chat mapping;
- contact position, chat cursor, load-finished/loading state;
- current selection and every widget/model/timer/cache owned by the old `ChatDialog` tree.

Retained:

- theme/style, window policy, and Gate/server configuration;
- no account-local message cache is retained accidentally. A future local cache must be a separate Module keyed by account and schema version.

## TDD RED/GREEN Evidence

1. **Decoder lifecycle:** RED compilation failed because `TcpFrameDecoder::reset` did not exist. Adding the production Interface made the focused decoder executable GREEN.
2. **Account state:** RED compilation failed because `ClientSession` and the production account reset/getter Interface did not exist. The real `ClientSession` + `UserMgr` slice then passed.
3. **Owned UI and idempotence:** the new QSignalSpy case returned nonzero because the registered session root remained alive. Production root destruction made it GREEN and repeated reset remained a no-op.
4. **Pending batch:** before connection reset wiring, an old text failure exposed the old client message ID. After `ClientSession` invoked `TcpMgr::resetConnection`, the same public signal/handler path returned an empty ID set; post-reset sends are ignored.
5. **Empty account safety:** calling `GetUid` after reset produced a focused segmentation RED. Returning sentinel `0` after the user object is cleared made the case GREEN.
6. **Wiring mutation:** temporarily removing `_session.resetSession(reason)` from `MainWindow` made `CheckTestStructure` fail with the session-routing diagnostic. Restoring it returned the structure gate to GREEN.

No existing assertion was deleted, relabelled, or weakened.

## Testcase and Report Changes

| Report | Before | After | Delta |
| --- | ---: | ---: | ---: |
| Qt Unit | 7 | 7 | 0 |
| Qt Component | 2 | 5 | +3 |
| All non-Qt reports | 161 | 161 | 0 |
| **Total** | **170** | **173** | **+3** |

`network_state_tests` remains one Unit runner testcase but gains contract T07-FRM-04. The three new Component runner cases protect Q02-SESSION-01..06. No real `QTcpSocket` connection is created, so no Integration label or `client_integration.xml` is claimed.

## Verification

- Public `RunClientTests -Configuration Release`: GREEN; production `chat.exe` built, Qt Unit 7/7 and Component 5/5 passed, with exact report-count enforcement.
- All pre-existing nine Qt runner testcases remained GREEN; all three new cases passed.
- Two independent `ctest --schedule-random` runs: GREEN, 12/12 each, demonstrating no order dependency.
- `CheckTestStructure`: GREEN; 3 Qt sources registered, exact levels/counts/reports enforced, production-library sharing and required reset wiring guarded.
- Twelve-report XML audit: exactly 173 testcase elements, zero failure elements, and zero error elements. Only the Qt reports were regenerated by this plan; unchanged non-Qt reports were audited, not rerun.
- Synthetic sensitive-state marker scan: zero report matches.
- Stub, test-only reset switch, credential-literal-shape, owned-process-residue, trailing-whitespace, and scoped `git diff --check` scans: GREEN.

## Deviations from Plan

### Auto-fixed Issues

**1. [Rule 1 - Lifecycle bug] Released singleton QObjects before QApplication destruction**

- **Found during:** pending-batch Component RED/GREEN execution.
- **Issue:** a test that exercised `TcpMgr` signal handling completed its assertions but Windows heap validation failed during process teardown because the static singleton destroyed its `QTcpSocket` after `QApplication`.
- **Fix:** added a production `ReleaseInstance` lifecycle and used it in the real client shutdown and test cleanup while Qt still exists.
- **Verification:** the focused pending-batch case and public runner exit normally and GREEN.

**2. [Rule 3 - Generated cache recovery] Reconfigured a confirmed incomplete CMake cache**

- **Found during:** first public client runner invocation.
- **Issue:** CMake invalidated compiler cache variables and left `CMAKE_MAKE_PROGRAM-NOTFOUND`.
- **Fix:** after verifying the exact incomplete-cache marker, used the repository runner's existing bounded recovery path once; no directory or build artifact tree was deleted.
- **Verification:** the second bounded public invocation configured, built 81 steps, and passed 12/12.

## Known Stubs

None. Created/modified plan-owned files contain no TODO, FIXME, placeholder, coming-soon, or unavailable implementation stub.

## Threat Flags

None. The plan narrows existing client lifecycle behavior and adds no endpoint, credential path, persistent store, schema, public network dependency, or authorization surface.

## Unverified Range

- Remote CI was intentionally not triggered.
- A real `QTcpSocket` reconnect timing case was not added; the current tests are deterministic in-process Unit/Component contracts and do not claim Integration coverage.
- Active logout/switch-account has no existing UI control in this source tree; the production `MainWindow::resetSession` Interface and `SwitchAccount` behavior are present for callers, while currently implemented kicked and abnormal-disconnect routes are wired end to end.
- Server, VarifyServer, and PowerShell suites were not rerun because this plan changed only Qt client sources and runner registration. Their existing reports were included only in the exact 173-report audit.

## Task Commits

None. No staging, commit, push, stash, checkout, reset, clean, workflow dispatch, or remote mutation was performed in the shared dirty workspace.

## Next Phase Readiness

- G-005 is closed for the deterministic `develop` gate.
- Plan 2.5-07 can validate clean-runner parity and the final required checks without adding a Qt Integration report.
- Future local cache work must preserve this strict cross-account reset while moving persistence into an account/schema-keyed Module.

## Self-Check: PASSED

- All declared production, test, runner, documentation, and Summary artifacts exist.
- Final public Qt reports contain 7 Unit and 5 Component cases; all twelve existing reports total 173 with zero failures/errors.
- No commit hash is claimed because commits were explicitly prohibited.
