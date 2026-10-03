# Qt resource transfer

Test IDs: Q04-RESOURCE-01..05, mapped by method in `tests/TEST-CONTRACT-MATRIX.md`.

`resource_transfer_tests` is a CMake target. Set `RESOURCE_TEST_HOST` to the freshly built
`ResourceTests.exe`, then run it with the selected Qt kit DLLs available. The test launches a real
loopback HTTP/filesystem server with explicit authentication fixtures.
For headless execution use `-platform minimal -style Fusion`.

`conversation_attention.widgets` 同时运行 `chatWindowPlacement`：将真实 ChatDialog
放入具有登录页固定尺寸的主窗口，调用生产 `placeChatWindow`，检查 125%/150% 缩放下的
逻辑可用区域、负坐标副屏及有偏移的屏幕中，窗口边框、编辑器和发送按钮均在区域内，
且聊天页解除固定尺寸并保留最大化能力。该测试不依赖实际显示器。
`conversationAttentionWidgets` 另检查默认首项选择（UID 0）不产生错误警告。

`conversation_attention.widgets` 是 Business / Component CTest 入口，复用该目标中
`conversationAttentionWidgets`，只使用真实 Qt 控件和临时 SQLite，不需要 ResourceServer。
它验证未加载会话的消息提醒、分页、窗口重建、账号存储重开、点击清除及清除状态持久化。
同一用例还通过真实异步补拉验证隐藏、最小化及模态窗口遮挡期间的新消息保留提醒，恢复前台后再清除。
已接入默认客户端回归；全部资源传输用例仍需下述独立 HTTP 测试服务。

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

`resumeUploadAndDownload` 同时验证编辑器文本/附件/文本经生产提交控制器、真实 HTTP 和临时 SQLite 依次落盘；`incomingAttachmentAndPageLifetime` 验证发送按钮及 Enter 的实际页面接线。用例数量不变，保留原上传续传/摘要和销毁断言。

提交控制器组合回归在首段文本落盘、附件上传至少一个块后关闭真实 HTTP 服务，再使用同一目录和端口重启；重试必须从已确认偏移继续，最终只产生三条有序消息。测试 host 的可选端口参数仅用于这一隔离恢复场景。

`searchCancellationWidgets` 注册为默认 Component 入口 `user_search.widgets`，保护真实等待窗 Esc 取消、立即重试和账号切换后的旧结果隔离。它只使用临时 SQLite，不启动资源服务。

基础易用性补充：`conversationAttentionWidgets` 使用多页且时间不等于 ID 的目录，验证最近活动排序仍保留
未加载会话的提醒边界；`localHistorySearchWidgets` 通过真实 Ctrl+C 检查单条正文复制。提醒夹具中的更近本人消息
用于保持被测未读会话在第一页之外，避免默认打开该会话后正确清除提醒而改变原测试前提。

`resumeUploadAndDownload` also provides an expired upload checkpoint: the server returns 404 and the client creates a fresh upload while preserving the source file and completing the transfer.
