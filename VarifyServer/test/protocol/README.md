# 协议测试

## 被测代码与契约

- 生产代码：`VarifyServer/const.js`、`proto.js`、仓库级 `proto/varify.proto`。
- 契约：公开错误常量保持既有 wire-visible 数值；加载后的描述符公开 `VarifyService/GetVarifyCode` 路径及
  请求、响应类型。

## 用例、依赖与隔离

`protocol.test.js` 包含 6 个 Foundation / Unit 用例（V02-PROTO-01..06）：保留两个既有合同，并保护 canonical authority、生成 consumer 注册、descriptor 语义兼容、隔离字段号/RPC 变异 RED，以及 Node 对旧 wire/unknown-field fixture 的消费。`check-contract` 与 `check-compatibility` 使用锁定的 `protobufjs`，不依赖 C++/vcpkg codegen 工具；固定 `protoc` 的生成物字节漂移仍由 Server job 的 `CheckProtocols` 独立拥有。依赖由 `npm ci` 安装，不使用 Redis、SMTP 或凭据。

真实跨语言 T05-GRPC-02 由 `tests/server/protocol` 的 Integration GTest 拥有；`node-varify-loopback-server.js` 只通过生产 `createServer` 提供动态 loopback server，不是独立 testcase。

## 运行与 CI

```powershell
Set-Location .\VarifyServer
node.exe --test test/protocol/protocol.test.js
```

Domain is Foundation and Level is Unit. The suite runs in-process without a
socket, child process, or production adapter and is reported in
`build/test-results/varify_unit.xml`.

统一入口是 `scripts/windows-local.ps1 -Task RunVarifyTests`。CI job 为 `varify-release`，合并报告为
`build/test-results/varify_unit.xml`。

## 已知缺口

- 初始 descriptor 来自迁移前 wire source；下一正式发布需晋升为可审计 release baseline。
- 当前未声明 Node client → C++ Varify server，因为仓库没有 C++ Varify server release unit。
