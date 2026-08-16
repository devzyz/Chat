# 协议测试

## 被测代码与契约

- 生产契约：`ChatServer/ChatServer/message.proto` 及生成的 `message.pb.h/.cc`。
- 契约：文本聊天请求保留路由字段、重复消息顺序、嵌入 NUL 和 UTF-8；验证码响应保留错误码、邮箱和验证码。

## 用例、依赖与隔离

`protobuf_contract_tests.cpp` 包含 2 个 GoogleTest round-trip 用例，只在内存中序列化和解析，不启动
gRPC server、不开端口。依赖为 GoogleTest 与 vcpkg 提供的 protobuf；生成文件不由测试手工修改。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=ProtobufContractTests.*
```

CI job 为 `servers-release`，合并报告为 `build/test-results/server_unit.xml`。

## 已知缺口

- 不验证真实 gRPC transport、deadline、连接失败或跨服务版本组合。
- proto 字段兼容性目前没有历史 descriptor/buf breaking-change 自动门禁。
