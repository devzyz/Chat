# 消息模型测试

## 被测代码与契约

- 生产代码：`messagelistmodel.*`、`messagemodelstore.*`、`messageitemdelegate.*` 和 `messagerecord.h`。
- 契约：插入、确认、状态更新和删除同步稳定 ID 索引；多页历史去重且保持时间顺序；Unicode、空文本和换行
  无损；每个 chat 保留独立模型与分页状态；窄视口增加长文本布局高度。
- Q01-MODEL-01 also covers history arriving before acknowledgement, including a legacy
  row without UUID: confirmation merges by server ID and preserves the pending UUID;
  duplicate peer/history/ack observations cannot create a second row.

## 用例、依赖与隔离

Domain 为 Business，目录继续由 message-model Module 所有。每个 CTest case
按照实际边界独立标记 Level：

| Test ID | CTest name | Level | Report |
| --- | --- | --- | --- |
| Q01-MODEL-01 | `message_model.append_ack_status_removal` | Unit | `client_unit.xml` |
| Q01-MODEL-02 | `message_model.unknown_ids` | Unit | `client_unit.xml` |
| Q01-MODEL-03 | `message_model.history_order` | Unit | `client_unit.xml` |
| Q01-MODEL-04 | `message_model.multiple_history_pages` | Unit | `client_unit.xml` |
| Q01-MODEL-05 | `message_model.shifted_indexes` | Unit | `client_unit.xml` |
| Q01-MODEL-06 | `message_model.text_and_chat_identity` | Unit | `client_unit.xml` |
| Q01-MODEL-07 | `message_model.store_pagination_state` | Component | `client_component.xml` |
| Q01-MODEL-08 | `message_model.delegate_reflow` | Component | `client_component.xml` |

`message_model_tests.cpp` 是一个 Qt 测试可执行入口，以多组断言覆盖现有模型契约。它使用
`QT_QPA_PLATFORM=minimal`，不需要真实显示器、网络、字体或 Server。依赖 Qt Widgets 和客户端模型源码；
失败通过进程非零退出传播。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
ctest.exe --test-dir .\build\windows-client\Release -L unit --output-on-failure
ctest.exe --test-dir .\build\windows-client\Release -L component --output-on-failure
```

CTest resolves `qminimal.dll` from the selected Qt kit through an explicit
`QT_QPA_PLATFORM_PLUGIN_PATH`; it does not rely on plugins left beside a local
deployment. The eight Interface contracts are registered as independent CTest
cases so failures identify the affected model/store/delegate behavior. Both
the application and the tests link the same `chat_message_model` production
module instead of compiling separate source lists.

CI job 为 `client-release`。Unit case 写入 `build/test-results/client_unit.xml`，
Component case 写入 `build/test-results/client_component.xml`；两组任一失败或缺报告都会使 runner 失败。

## 已知缺口

- 不覆盖真实窗口事件、网络管理器、登录/好友/聊天跨页面流程或平台字体的精确像素。
