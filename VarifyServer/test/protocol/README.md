# 协议测试

## 被测代码与契约

- 生产代码：`VarifyServer/const.js`、`proto.js`、`message.proto`。
- 契约：公开错误常量保持既有 wire-visible 数值；加载后的描述符公开 `VarifyService/GetVarifyCode` 路径及
  请求、响应类型。

## 用例、依赖与隔离

`protocol.test.js` 包含 2 个 `node:test` 用例，只加载常量和 proto descriptor，不启动 gRPC server、
不开网络端口。依赖由 `npm ci` 安装的 `@grpc/grpc-js` 与 `@grpc/proto-loader`，不使用 Redis、SMTP 或凭据。

## 运行与 CI

```powershell
Set-Location .\VarifyServer
node.exe --test test/protocol/protocol.test.js
```

统一入口是 `scripts/windows-local.ps1 -Task RunVarifyTests`。CI job 为 `varify-release`，合并报告为
`build/test-results/varify_unit.xml`。

## 已知缺口

- 不验证真实 gRPC server/client、deadline、transport 错误或 C++/Node 跨语言互操作。
- proto 尚无历史 descriptor 的 breaking-change 自动门禁。
