# Local message persistence and synchronization

Production Modules: `LocalMessageStore`, `MessageService`, `UserStoragePaths`.
`message_storage.persistence` is a Business / Component CTest case using real
temporary SQLite databases and the production worker. It contributes one case
to `client_component.xml`; individual QtTest methods cover:

| Test ID | Method | Contract |
| --- | --- | --- |
| Q05-STORE-01 | restartAndIncrementalCursor | Reopen, Unicode content, incremental-only cursor; ACK cannot skip a gap |
| Q05-STORE-02 | pendingHistoryAndAckMerge | History before ACK merges by UUID; restart marks pending uncertain |
| Q05-STORE-03 | failedPageDoesNotAdvanceCursor | UUID identity conflicts, Invalid/stale page rolls back messages and cursor together |
| Q05-STORE-04 | accountsAndLocalPagination | Environment/account separation, latest/older local pages |
| Q05-STORE-05 | serviceSyncAndSessionIsolation | Async production coordinator, persisted next request, discard old-account response, full-queue account switch |
| Q05-STORE-06 | outgoingRequiresDurableStorage | Network submission follows successful disk commit; write failure sends nothing |
| Q05-STORE-07 | refreshKeepsTheDisplayedIntervalComplete | Refresh includes the whole displayed interval even after more than 50 new messages |

Additional state-control methods remain inside the same component CTest entry:

| Test ID | Method | Contract |
| --- | --- | --- |
| Q05-STORE-08 | receiptsAreDurableMonotonicAndIndependent | Early receipt, restart, independent cursor, no downgrade |
| Q05-STORE-09 | deliveredAckCannotEraseNewReadIntent | Durable Read outbox survives Delivered ACK and restart |
| Q05-STORE-10 | outgoingBatchRetriesUseStableIdentityAndAttempt | Immutable business payload, bounded retry timing, stale attempt rejection, committed identity |
| Q05-STORE-11 | invalidReceiptPageRollsBackAndDoesNotAdvance | Permission/identity/order conflicts roll back receipt and cursor |
| Q05-STORE-12 | serviceReceiptRoundTripAndAccountIsolation | Real coordinator report/sync flow and old-account callback isolation |
| Q05-STORE-14 | resourceIntentSurvivesRecoveryAndRetryBudget | Original resource descriptor/ID, exact 1/3/10 retry budget, recovery gate and canonical identity |
| Q05-STORE-13 | schemaOneUpgradePreservesHistoryAndBackup | Real schema 1 upgrade, backup contents/version, preserved history/cursor, no inferred Read |

Q05-STORE-06 also verifies retrying an unsaved original request after local storage recovers.

Run the owning entry:

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
```

For focused verification after configuring/building the client:

```powershell
ctest --test-dir build/windows-client/Release -R message_storage --output-on-failure
```

Qt SQL and the kit's `sqldrivers/qsqlite.dll` are required. CTest explicitly uses
the selected kit's plugin directory. There is no personal database or network.
`message_sync_probe` is invoked by the isolated server Integration runner and uses
the real MessageService, SQLite, Qt socket and frame decoder across process restarts.
Tests do not prove the complete GUI/TCP/MySQL chain; production server adapter
and TCP coverage belongs to `tests/server/message-sync`.

The optional probe `receipts` argument negotiates the production receipt protocol, reports durable
messages, submits explicit Read observations, waits for confirmed SQLite facts and verifies reopen.
It does not substitute for the GUI visibility tests or production Redis/Status verification.
