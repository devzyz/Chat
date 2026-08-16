# 消息帧测试

## 被测代码与契约

- 生产代码：`ChatServer/ChatServer/MsgNode.h`、`MsgNode.cpp`。
- 契约：缓冲区初始化和清理正确；消息 ID 与 body 长度使用网络字节序；空 body、嵌入 NUL 和
  `MAX_LENGTH` 内容按显式长度无损处理。

## 用例、依赖与隔离

`msg_node_tests.cpp` 包含 8 个 GoogleTest 用例，仅在内存中创建 `MsgNode`、`SendNode`、`RecvNode`，
不打开 socket、不启动线程，也不访问外部服务。依赖为 GoogleTest 和生产代码已有的 Boost.Asio 类型。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=MsgNodeTests.*:SendNodeTests.*:RecvNodeTests.*
```

CI job 为 `servers-release`，合并报告为 `build/test-results/server_unit.xml`。

## 已知缺口

- 尚未覆盖真实 TCP 半包、粘包、非法 header 或 session 关闭竞争；这些属于后续 Integration/Component 测试。
- 当前用例不为生产类增加测试专用接口。
