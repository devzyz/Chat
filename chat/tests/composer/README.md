# 输入与提交流程回归

生产接口为 MessageTextEdit、MessageSubmissionController 和 UserSearchController。

- Q05-COMPOSER-01 / composer.interaction：Business / Component。真实 Qt 编辑器及临时 SQLite；覆盖附件剪切粘贴、后台准备时 UI 可响应、队列满载拒绝与无引用任务取消、撤销分支释放临时文件、存储失败重试/取消后的唯一所有权、真实退出提示选择 Cancel 后继续提交，以及控件交互。
- Q05-COMPOSER-02 / composer.byte_budget：Business / Unit。最终 JSON 字节预算和 Unicode 无损拆分。
- Q05-COMPOSER-03 / composer.upload_failure：Business / Integration。真实 HTTP 连接失败、部分成功后重试/取消、账号停止及重新登录后旧上传通知/已排队 SQLite 完成回调隔离；不依赖外部服务。
- Q05-SEARCH-01 / user_search.lifecycle：Business / Component。请求身份、期限、重复、取消、断线及旧服务器无编号结果。
- Q05-SEARCH-02 / user_search.loopback：Business / Integration。真实 ChatTcpTransport 与隔离 TCP peer，验证搜索 JSON 身份在生产解帧后保持。
- Q05-SEARCH-03 / user_search.widgets：Business / Component。位于 resource-transfer 套件，验证真实 ChatDialog 等待窗 Esc 取消、立即重试、页面销毁及换账号后的旧结果隔离，不需要 HTTP 服务。

使用 minimal 平台，不需要个人账号或公网。编辑器组件超时 45 秒，搜索控件 30 秒，其他入口 20 秒。
全部进入 RunClientTests，分别写 client_unit.xml、client_component.xml、client_integration.xml。
真实 ChatServer 的搜索字段兼容在 tests/server/message-sync/integration.py 内验证；上述 peer 不冒充生产服务端。

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -R '^(composer|user_search)\.' --output-on-failure
```

`composer.interaction` 增加 `conversationDrafts`：真实编辑器跨会话保留文本、附件、原生文档、撤销与重做，
后台附件准备回到原文档，清空一个会话不影响另一个。旧实现 RED 通过元对象缺少切换入口观察；正式用例直接调用生产接口。
