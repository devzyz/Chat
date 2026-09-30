# 列表交互测试

所属：Foundation / Component。生产模块为 `ListViewBehavior`，会话、联系人、搜索、好友申请和消息列表共同使用。

`Q01-LIST-01` / `list_view.interaction` 使用真实 QListWidget / QListView 验证：
悬停滚动条不改变模型或选择策略；滚轮与原生 Qt 行为一致；键盘和滚动条到底通知；
短列表向下滚动可继续请求、向上不误触发；视图销毁取消排队回调。

底部通知不代表加载成功，业务调用方仍负责进行中、游标和无更多数据的判断。
不覆盖搜索网络、好友审批、数据库分页或平台触摸板硬件。

测试链接与应用相同的 `chat_list_view`，使用 minimal 平台，无网络、账号或外部服务依赖。
CTest 超时 15 秒，非零退出传播失败，CI 归入 `client_component.xml`，数量由中央 runner 定义。

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest --test-dir build/windows-client/Release -R '^list_view\.' --output-on-failure
```
