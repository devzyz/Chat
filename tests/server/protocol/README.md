# 协议测试

## 被测代码与契约

- 生产契约：仓库级 `proto/varify.proto`、`proto/status.proto`、`proto/chat.proto`，以及只由固定命令生成的 `generated/proto/cpp/*`。
- 契约：共享 package/message/service/RPC/field wire 语义与初始 release descriptor 兼容；当前 C++ 能消费旧 Varify payload 和未知字段；C++ client 能调用真实 Node Varify service。

## 用例、依赖与隔离

`protobuf_contract_tests.cpp` 包含 3 个 Foundation / Unit 用例：F02-PROTO-01..03。前两个保留现有 round-trip；F02-PROTO-03 从独立旧 wire fixture 解析普通与 unknown-field payload。

`cpp_node_varify_loopback_tests.cpp` 包含 T05-GRPC-02（Architecture / Integration）：Node 生产 `createServer` 绑定 `127.0.0.1:0`，C++ 生成 Stub 使用 2 秒 deadline 调用；ready 最多 3 秒，Node helper 最多 10 秒，teardown 最多 2 秒后只终止本测试拥有的进程。所有断言路径由 RAII 关闭 pipe/process/server，不访问 Redis、SMTP、公网或个人配置。

## 运行与 CI

```powershell
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\scripts\windows-local.ps1 -Task CheckProtocols
.\build\windows-tests\Release\server_unit_tests.exe --gtest_filter=ProtobufContractTests.*
.\build\windows-tests\Release\server_integration_tests.exe --gtest_filter=CrossLanguageProtocolTests.*
```

CI job 为 `servers-release`；Unit 写入 `server_unit.xml`，跨语言 Integration 写入 `server_integration.xml`。

## 已知缺口

- 初始 baseline 来自迁移前 Git wire source，而非可审计的外部正式 release artifact；后续正式发布必须晋升 baseline。
- 当前只覆盖 C++ client → Node Varify；完整双向、新旧 release 组合仍由后续 release compatibility matrix 扩展。
