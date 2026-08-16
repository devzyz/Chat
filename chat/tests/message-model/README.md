# 消息模型测试

## 被测代码与契约

- 生产代码：`messagelistmodel.*`、`messagemodelstore.*`、`messageitemdelegate.*` 和 `messagerecord.h`。
- 契约：插入、确认、状态更新和删除同步稳定 ID 索引；多页历史去重且保持时间顺序；Unicode、空文本和换行
  无损；每个 chat 保留独立模型与分页状态；窄视口增加长文本布局高度。

## 用例、依赖与隔离

`message_model_tests.cpp` 是一个 Qt 测试可执行入口，以多组断言覆盖现有模型契约。它使用
`QT_QPA_PLATFORM=minimal`，不需要真实显示器、网络、字体或 Server。依赖 Qt Widgets 和客户端模型源码；
失败通过进程非零退出传播。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest.exe --test-dir .\build\windows-client\Release -R message_model_tests --output-on-failure
```

CI job 为 `client-release`，报告为 `build/test-results/client_unit.xml`。

## 已知缺口

- 尚未拆分成逐用例 QtTest 报告，当前 CTest 将该可执行文件记录为一个 test case。
- 不覆盖真实窗口事件、网络管理器、登录/好友/聊天跨页面流程或平台字体的精确像素。
