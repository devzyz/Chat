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

## 本地目录（schema 3）

同一个 `message_storage.persistence` 入口新增：

| Test ID | Method | Contract |
| --- | --- | --- |
| Q05-STORE-15 | directoryPersistenceAndPagination | 目录重启、40 条联系人完整分页、主键更新、审批状态及非法记录整批回滚 |
| Q05-STORE-16 | directoryServiceIsolation | 异步本地页、失败不发布、旧账号回调丢弃及已接受写入排空 |
| Q05-STORE-17 | schemaTwoDirectoryUpgrade | schema 2 升级快照、原消息与游标保留及目录可写 |

网络回包到目录存储的组合由 `session_reset.account_state` 补充验证。

Basic groups additionally cover directory-gated zero recipients, sender-scoped UUIDs,
restart/cursor preservation, conflict rollback, two-second incremental polling and
exclusion from private receipt reporting. The account-state regression also exercises
network group directory conversion and restores the group type after account reset.

## 动态成员与本地查找（schema 4）

既有 `message_storage.persistence` 入口的 `groupEpochAndLocalSearch` 覆盖移除、
重新加入、旧代次/旧版本拒绝、历史保留和超过一页的本地搜索。
`directoryPersistenceAndPagination` 同时验证备注部分合并、全目录筛选及稳定分页。
升级用例目标为 schema 5；既有故障注入、账号隔离和发送/回执恢复用例继续回归。

## 本地新消息提醒

`conversationAttentionPersistence` 覆盖私聊和群消息提醒、本人消息排除、重复页拒绝后计数不变、
旧查看快照不清除新消息、查看边界单调、重启及账号隔离。
`schemaFourAttentionUpgrade` 覆盖 schema 4 快照、原正文和游标保留、历史不重新提醒及新消息正常计数。
二者复用 `message_storage.persistence`，不增加存储测试聚合入口。

`groupOperationSurvivesRefreshAndRestart` 验证目录刷新与管理命令落盘排队时不丢失原身份，重启后可恢复并清除。`schemaTwoDirectoryUpgrade` 同时覆盖 3→4 快照及数据保留。
