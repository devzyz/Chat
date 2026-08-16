# Server 测试

本目录是 Windows C++ Server 快速测试的统一入口。`ServerUnitTests.vcxproj` 负责构建一个 GoogleTest
可执行文件，测试源码按被测模块拆分，生产源码仍来自 `ChatServer/ChatServer`。

| 模块 | 目录 | 主要契约 |
| --- | --- | --- |
| 配置 | [config](config/README.md) | ChatServer 配置路径、必填项、端口与 peer 校验 |
| 消息帧 | [messaging](messaging/README.md) | MsgNode 缓冲区、网络字节序和长度边界 |
| 并发 | [concurrency](concurrency/README.md) | Asio 工作分派与幂等停止 |
| 协议 | [protocol](protocol/README.md) | protobuf 消息序列化契约 |

从仓库根目录运行：

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
```

该入口由 CI 的 `servers-release` job 调用，并生成 `build/test-results/server_unit.xml`。新增测试 MUST
放入对应模块目录；若没有合适模块，应先创建边界明确的子目录及其 `README.md`，不得把源码重新堆到本目录。
