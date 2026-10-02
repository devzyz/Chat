# Message synchronization Integration tests

Production implementation: `common/message/MessagePersistence.h`, used by
ChatServer's `MysqlDao`; TCP handler is `LogicSystem.cpp`.

| Test ID | Test | Contract |
| --- | --- | --- |
| S06-SYNC-01 | RetryConflictAndBatchRollback | UUID retry returns original ID; conflicting content rolls back the whole batch; participant check |
| S06-SYNC-02 | IncrementalByteBoundedPagesAndResourceIdentity | Incremental cursor, byte-bounded pages, empty catch-up, resource UUID, text/resource identity conflicts and membership |
| S06-SYNC-03 | CursorCannotPassAnUncommittedWriter | Observe real InnoDB lock wait; sync sees the committed earlier message |
| S06-SYNC-04 | integration.py / tcp_flow | Offline send ACK, retry, two production ChatServer instances, push, incremental fetch and relogin |
| S06-SYNC-05 | integration.py / message_sync_probe | Real Qt coordinator and SQLite; process restart requests only IDs after the persisted cursor |

Use existing dependencies only. From a VS developer shell with `VCPKG_ROOT` set:

```powershell
msbuild ChatServer/ChatServer/ChatServer.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
msbuild tests/server/message-sync/MessageSyncTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
msbuild tests/server/resource/ResourceTests.vcxproj /p:Configuration=Release /p:Platform=x64 /p:VcpkgManifestInstall=false
cmake --build build/windows-client/Release --target chat message_sync_probe
python -B tests/server/message-sync/integration.py
```

The Qt Release build must already be configured; the probe uses its SQL/Network runtime
and QSQLITE plugin (ensure the selected Qt/MinGW `bin` directories are on PATH).
The Python entry creates its own MySQL data directory under `build/message-sync`,
applies migrations twice, runs production persistence tests, starts two ChatServer
processes, and cleans up only its own processes/files. `mysql` and `mysqld` must
already be on PATH. Binaries need their app-local runtime dependencies.
The existing ResourceTests executable provides the Status fixture; Redis also
uses the existing deterministic fixture. This is real MySQL/TCP/gRPC evidence,
not production Status/Redis or a full desktop E2E claim.

MySQL results: `build/message-sync/mysql.xml`; TCP failures propagate as nonzero
exit. These opt-in Integration cases are outside the quick CI aggregate defined by
`scripts/windows-local.ps1`; current report counts are maintained there.

## Receipt extensions

| Test ID | Test | Contract |
| --- | --- | --- |
| S07-RECEIPT-03 | ReceiptsAreAuthorizedMonotonicAndDurable | Real MySQL membership/reader checks, atomic rejection, idempotence, first timestamps, reconnect durability |
| S07-RECEIPT-04 | ReceiptPageCannotSkipAnUpgrade | Byte-bounded revision pages and old-message upgrade after earlier page |
| S07-RECEIPT-05 | integration.py / tcp_flow | Capability negotiation, two production ChatServers, cross-instance hint, exact authoritative receipts, forged reader rejection, sender offline/relogin |
| S07-RECEIPT-06 | message_sync_probe receipts mode | Production Qt service, SQLite and TCP report/Read confirmation, independent cursor and process restart |

The shared Redis fixture supports current atomic two-key presence scripts. It remains a fixture,
not evidence for a production Redis adapter. Non-object requests are dropped by the current
session dispatcher; malformed object history requests retain their error response.
The Read probe supplies an observation explicitly; real foreground/geometry behavior is covered
separately by the Qt message-model component test, not claimed as human GUI end-to-end evidence.

## Basic group extension

`integration.py / tcp_flow` also exercises production TCP group creation, creation UUID
retry/conflict, rejection of non-friend invitations, group directory discovery, sender-scoped
message UUID deduplication, non-member send/sync denial, cross-instance replies and offline
incremental recovery. It uses migration 005 in its owned database and the same bounded entry above.
Group delivery is polling-based; this test does not claim desktop interaction or group receipts.

`MessageSync.GroupMembershipAndCommitOrdering` verifies three members sharing one
message, UUID retry identity, non-member denial and an observed InnoDB lock wait
while group synchronization waits for an uncommitted writer.

## Dynamic group extension

The same runner now applies migration 006. `DynamicGroupLifecycleAndResources`
covers batch rollback, role checks, version/UUID conflicts, join boundaries, rejoin
epochs, transfer/leave/dissolve and resource authorization.
`ConcurrentGroupVersionsSerialize` executes competing management transactions and
requires exactly one success. The existing lock-wait test protects group/message serialization.
`integration.py` adds four-account TCP management, late-generation rejection, creation
retry after rename, tombstone discovery, and direction-specific friend remarks.
All tests retain the real-MySQL/fixture-Status/fixture-Redis boundary described above.

`MembershipChangesWaitForCommittedMessages` observes real InnoDB lock waits for
add/remove behind a message transaction, then verifies the join boundary and revoked
send/sync permission. `TransferAndLeaveCannotRemoveTheOwner` races transfer against
the target's leave and requires exactly one success and one active owner.

用户搜索协议扩展：`tcp_flow` 验证生产 ChatServer 1007/1008 的成功、未知 UID、参数类型错误均保留请求编号，同时兼容不携带编号的旧客户端。

基础社交扩展使用迁移 007。`tcp_flow` 覆盖能力协商、资料更新/重名/版本冲突、公开投影、双向删除、
删除后的历史与旧 UUID 确认、拒绝/重新申请/接受、复用原 chat_id、旧版本发送拒绝、在线大正文通知与同步、
申请目录按字节连续分页以及重新登录恢复。`IncrementalByteBoundedPagesAndResourceIdentity` 进一步验证删除后
资源引用仍可读、旧 UUID 仍可确认、旧版本不能创建引用而当前版本可提交。仍只使用自建临时 MySQL 与 Redis/Status 替身。

社交长字段回归通过真实 TCP 组合 255 字符中文姓名、个人描述、申请说明和备注，验证申请、联系人及资料响应完整可读，目录仍按 8000 字节分页。
申请分页还验证接收者 UID 与备注字段完整，避免目录刷新后旧审批请求丢失内嵌身份。
