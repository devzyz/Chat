# Qt 客户端测试

Qt 测试由 `chat/CMakeLists.txt` 显式注册，测试源码按客户端模块组织：

- [message-model](message-model/README.md)：消息模型、模型仓库和 delegate 布局行为。

从仓库根目录运行：

```powershell
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
```

CI 的 `client-release` job 运行 CTest，并生成 `build/test-results/client_unit.xml`。新增模块必须创建独立
子目录和 `README.md`，并在 CMake 中显式注册，不能依赖目录通配符发现。
