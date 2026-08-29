# 验证码 handler 测试

## 被测代码与当前契约

- 生产代码：`VarifyServer/server.js` 的 `createGetVarifyCodeHandler` 及直接执行入口。
- `V06-MOD-01`：CommonJS 模块被 `require` 时不加载 Redis/SMTP adapter，也不启动 gRPC 监听。
- `V06-HDL-01`：Redis 已有值时复用，不生成 UUID、不重复写入，并发送一封邮件。
- `V06-HDL-02`：Redis 无值时取 UUID 前 4 个字符，以 `code_` key 和 600 秒 TTL 写入后发送邮件。
- `V06-HDL-03`：Redis 写入返回 false 时响应 `RedisErr`，不发送邮件。
- `V06-HDL-04..06`：mailer 返回 false、Redis read rejection 或 SMTP rejection 均响应 `Exception`。
- `V06-HDL-07`：邮件发件人使用运行时环境配置注入的地址，不再依赖源码字面量。
- `V06-LOG-01`：默认事件日志不包含完整邮箱、验证码或 provider response。
- 所有 handler 路径均断言 gRPC callback 恰好调用一次并回显请求中的虚构邮箱地址。

`require.main === module` 保护和 handler factory 是最小 test seam：直接执行 `node server.js` 时仍创建相同的
默认 Redis、SMTP、UUID 和 gRPC 对象，并监听原地址；作为模块加载时不创建外部连接。

## 类型、依赖、隔离与超时

本模块包含 9 个 `node:test` Unit 测试。Redis、SMTP、UUID 和 logger 均使用进程内 fake；不连接真实 Redis、
不发送邮件、不打开端口，不读取 `config.json`，fixture 只使用 `.test` 保留域地址和明显虚构的短码。异步 fake
立即确定性完成，没有轮询或固定 sleep；统一 Node test runner 由 CI job 的 20 分钟上限约束，单模块通常小于
1 秒。测试不创建临时文件、key 或子进程，因此无需额外 teardown。

## 运行与 CI

```powershell
Set-Location .\VarifyServer
node.exe --test test/handler/handler.test.js
npm.cmd test
```

Domain is Business and Level is Unit. All collaborators are injected in-process
fakes; the suite is reported in `build/test-results/varify_unit.xml`.

统一入口为 `scripts/windows-local.ps1 -Task RunVarifyTests`。CI 复用 `varify-release` job 的 `npm ci` 依赖树，
JUnit 报告写入 `build/test-results/varify_unit.xml`，失败时由现有 `if: always()` artifact step 上传。

## 已知缺口

- 默认 handler、Redis 和 SMTP adapter 日志已改为通用事件，不输出邮箱、验证码、provider response 或异常对象。
- `bindAsync` error/实际端口与 start 转换由 `test/startup` 的生产 `startServer` interface 覆盖。
- `redis.js` 会把读取异常转换为 `null`，因此真实 adapter 无法区分 miss 与依赖错误；本模块只验证 handler
  收到 rejection 时的现有映射。真实 Redis adapter、TTL 原子性与重连属于后续 Integration 测试。
- 不验证真实 SMTP、邮件到达、gRPC transport/deadline、输入邮箱格式或空 UUID；当前源码尚未定义这些契约。
