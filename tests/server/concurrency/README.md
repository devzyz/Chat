# 并发测试

## 被测代码与契约

- 生产代码：`ChatServer/ChatServer/AsioIOServicePool.h`、`AsioIOServicePool.cpp`。
- 契约：提交到池中的工作在硬超时内恰好完成，`stop()` 可重复调用且不抛异常。

## 用例、依赖与隔离

`asio_pool_tests.cpp` 包含 1 个 GoogleTest 用例。它使用 condition variable 和 5 秒硬超时，不以固定
sleep 判断正确性；只运行进程内 Asio worker，不监听网络端口。该单例会在本模块用例结束时停止，因此测试目标
不得添加依赖其后继续运行的用例。依赖为 GoogleTest 和 Boost.Asio。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=AsioIOServicePoolTests.*
```

CI job 为 `servers-release`，合并报告为 `build/test-results/server_unit.xml`。

## 已知缺口

- 尚未覆盖 GateServer、StatusServer 各自线程池的重复关闭行为。
- 长时间压力、取消竞争和异常启动回滚应放入 nightly 或独立并发测试，不应拖慢 PR 快速集合。
