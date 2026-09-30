# Qt resource transfer

Test IDs: Q04-RESOURCE-01..05, mapped by method in `tests/TEST-CONTRACT-MATRIX.md`.

`resource_transfer_tests` is a local-only CMake target. Set `RESOURCE_TEST_HOST` to the freshly built
`ResourceTests.exe`, then run it with the selected Qt kit DLLs available. The test launches a real
loopback HTTP/filesystem server with explicit authentication fixtures.
For headless execution use `-platform minimal -style Fusion`.

The resume case creates a real PNG, cancels after 64 KiB is acknowledged, destroys the manager,
recreates it using the same account cache, resumes, downloads and compares the complete bytes.
The invalid-file case rejects an empty file without contacting a server; generic attachments are supported.
The avatar case publishes a cropped PNG through the real HTTP server, restores it in a second account's
isolated installation directory, recreates the cache and verifies failed publication preserves the old image.
Account cleanup uses ChatPage ownership and cancels replies before UI/model destruction.
AvatarCache belongs to the authenticated account session and is destroyed on account reset.
The page case checks incoming attachment recognition, one transfer manager across resizes,
and destruction with pending work. The model case verifies that attachment updates leave text intact.

`groupMembershipControlsWidgets` 使用真实 ChatPage/GroupPanel 和临时 SQLite，
验证有效成员可输入、离群后禁止输入及管理、缓存成员仍显示。
可选 `CHAT_UI_CAPTURE` 保存控件截图；这属于自动控件测试，不替代四账号人工桌面验收。
测试按生产顺序在 Qt 应用退出前释放网络与用户单例。

`groupPanelRejectionVisible`、`groupPanelExternalRevocation`、`groupPanelSnapshotRequired`、
`groupPanelPaginationFailure`、`groupPanelReopenRetryIdentity` 使用真实按钮、标签、分页请求信号与临时 SQLite 验证：
管理拒绝原因保持可见；已落盘的外部移除立即禁用操作；完整同版本成员快照到达前
禁止添加、移除、转让；分页失败保留缓存但不能操作；迟到页不覆盖失败；
重新打开保留原管理 UUID、参数并可重试，本地保存失败明确提示。
这三项缺陷先经 RED 复现，再运行修复后的全部五个独立用例；不替代人工桌面验收。

`groupPanelTcpRefreshRevision` 经过真实 `TcpMgr::handleMessage` → SQLite → 目录变化 →
业务回包链路，验证资料从旧版本一次刷新到新版本，且重新入群后的旧代次回包不能恢复操作。
