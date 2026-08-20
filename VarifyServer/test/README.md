# VarifyServer 测试

VarifyServer 使用 Node.js 内置 `node:test`，测试按被测模块组织：

- [config](config/README.md)：配置来源优先级和畸形配置。
- [protocol](protocol/README.md)：错误常量及 protobuf/gRPC 描述符。
- [handler](handler/README.md)：验证码复用/生成、TTL、依赖错误与单次 callback。

从仓库根目录运行：

```powershell
.\scripts\windows-local.ps1 -Task RunVarifyTests
```

- [rpc](rpc/README.md): real loopback gRPC registration, routing, response, and bounded deadline behavior.

也可在 `VarifyServer` 目录执行 `npm test`。CI 的 `varify-release` job 运行相同文件集合，并生成
`build/test-results/varify_unit.xml`。新增模块必须建立子目录与 `README.md`，且显式加入 npm 和统一本地入口。
