# Resource transfer tests

Test IDs: S05-RESOURCE-01..18; method mapping and CI boundary are in `tests/TEST-CONTRACT-MATRIX.md`.

Run `scripts/windows-local.ps1 -Task RunServerTests` for the nine `StoreTest.*` filesystem
integration cases (60-second timeout, no database). Windows develop/full CI runs the same
entry and retains `build/test-results/server_resource_integration.xml`. Debug and Release
are supported. ResourceServer is built and deployed by this entry and `BuildServers`.

Run `scripts/resource-local.ps1 -Task Test` for the additional HTTP/video/database/Qt
integration suite with existing dependency/tool paths. Its reports and logs remain in
`build/resource`; these additional cases are not claimed by the routine storage report.

For database-only regression, build `ResourceTests.vcxproj` with manifest restore
disabled and run `python -B tests/server/resource/catalog_integration.py --catalog-only`.
This includes killed-idle-connection recovery in the real ResourceCatalog pool and
the Chat DAO symmetry/rollback tests owned by `../data`. Results are written to
`build/resource/catalog.xml`; the native child is limited to 30 seconds. This
focused option does not claim production Redis/Status or TCP-flow coverage.

- `StoreTest.*`: restart/resume, offset conflict, owner isolation, incomplete upload, checksum mismatch,
  size/buffer bounds, path validation and media signature. Actual temporary files, no database.
- `stream_integration.py`: real HTTP Keep-Alive, partial request interruption, offset recovery, Range downloads,
  and a generated ten-second AVI (25 fps, 250 frames). Verify source/download SHA-256 and decode every frame.
  Authentication is a deterministic test-host fake, never available in the production executable.
  `ResourceTests --serve-test <directory> [port]` accepts an optional loopback port so the Qt
  submission test can restart the same isolated HTTP endpoint and verify resume after failure.
  Also covers generic attachment bytes, avatar owner/reader permissions, incomplete publication,
  malformed PNG, invalid dimensions and preservation of the previous avatar after rejection.
- `catalog_integration.py`: creates and initializes its own loopback MySQL directory, applies the complete
  versioned schema through `schema/migrate.js` twice, tests resource references, UUID identity and permissions.
  It never connects to a personal database and stops only its own MySQL process.
  Version 3 adds the resource/avatar tables; native schema verification stays enabled. Avatar references persist across catalog recreation and the
  production HTTP flow restarts ResourceServer to verify the published avatar survives.
- `chat_flow_integration.py`: two production ChatServer processes, production ResourceServer, real MySQL,
  real HTTP/TCP/gRPC, with explicitly scoped Redis/Status fixtures. Exercises cross-instance notification,
  retry, receiver download authorization, relogin/history and same-instance delivery. This is NOT evidence
  of real Redis or production Status adapter behavior.
- `schema_upgrade.js`: prepares the verified version-2 schema in its own temporary MySQL,
  applies migrations 3 through 6, verifies old user/message data and repeat application, then proves
  a missing avatar table is still rejected by schema verification. Uses `CHAT_MYSQL_BIN`.
- Qt `resource_transfer_tests`: cancel after an acknowledged prefix, destroy/recreate the transfer manager,
  resume from the persisted task, download and compare bytes; reject unsupported files.

Existing Asio lifecycle, Qt message model and account-reset regressions protect the reused structure.
Do not count fixture-backed tests as full production dependency E2E or manual GUI acceptance.

The production HTTP/TCP flow separately uploads valid PNG, ten-second MJPG AVI and ordinary file bytes.
It reuses the stream suite's OpenCV video fixture (250 frames), checks acknowledged chunk offsets,
commits each media type online and offline with UUID replay, syncs after relogin, verifies downloaded
SHA-256 and decodes all video frames. A later joiner sees no earlier messages and cannot download
these resources; a removed member cannot download them either. New private references to the same
resources restore independent access, which survives group dissolution. This uses the existing
catalog/transfer paths. These are protocol/media evidence, not Qt card-display or manual desktop evidence.

基础社交扩展后的 UUID 重试只确认原消息，不再次推送。`chat_flow_integration.py` 在重试 ACK 后使用接收方心跳响应作为有序边界，确认没有插入重复通知，并对所有收到的帧检查 2048 字节上限。

本轮存储回归补充账号/总量预算隔离与超限拒绝；ResourceCatalog 在建连时核验完整 schema，
部署 smoke 必须先运行发布包内版本化迁移。`/ready` 与真实 Status/MySQL 的组合由 hosted 测试核验。

`StoreTest.DeclaredLengthAndBufferAreBounded` now fills the 32-upload quota, ages abandoned uploads eight days, verifies an active upload survives, preserves completed bytes, and resumes interrupted discard cleanup. The owned fixture directory is removed at teardown.

`StoreTest.InterruptedCreationDoesNotBlockUploads`（S05-RESOURCE-18）注入新鲜缺字节 `.json` 和损坏的
未发布 `.creating`，重建存储后同账号及其他账号均可创建；中断残留被回收，完成资源的原字节不变。
