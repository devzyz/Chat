# 配置测试

## 被测代码与契约

- 生产代码：`ChatServer/ChatServer/ConfigMgr.h`、`ConfigMgr.cpp`。
- 契约：显式配置路径优先；单实例和双实例示例可加载；必填字段、端口范围、TCP/RPC 冲突及
  peer 自引用、重复、空项必须被拒绝并提供诊断。

## 用例、依赖与隔离

`config_mgr_tests.cpp` 包含 17 个 GoogleTest 用例。测试动态创建唯一临时 INI，并在析构时清理；只有双实例
契约读取随测试目标复制的 `chat-01.ini`、`chat-02.ini`。测试不连接 Redis、MySQL 或网络。依赖为
GoogleTest、Boost.PropertyTree 和 Windows 临时文件/PID API。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=*ConfigMgr*:*InvalidPort*
```

CI job 为 `servers-release`，合并报告为 `build/test-results/server_unit.xml`。

## 已知缺口

- GateServer、StatusServer 的配置入口尚无等价 seam，未在此复制其私有解析逻辑。
- 尚未以独立子进程验证 ChatServer `main` 的所有命令行错误路径。
