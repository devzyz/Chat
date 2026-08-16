# 配置测试

## 被测代码与契约

- 生产代码：`VarifyServer/config.js`。
- 契约：命令行 `--config` 高于 `CHAT_CONFIG`，环境变量高于工作目录 `config.json`；默认配置正确展开；
  畸形 JSON 必须令加载进程非零退出。

## 用例、依赖与隔离

`config.test.js` 包含 4 个 `node:test` 用例。每个用例创建唯一临时目录，并通过独立 Node 子进程隔离
`process.argv`、环境变量和 CommonJS require cache；临时配置只含虚构凭据并在 teardown 清理。测试不连接
Redis、MySQL、SMTP 或网络。

## 运行与 CI

```powershell
Set-Location .\VarifyServer
node.exe --test test/config/config.test.js
```

统一入口是 `scripts/windows-local.ps1 -Task RunVarifyTests`。CI job 为 `varify-release`，合并报告为
`build/test-results/varify_unit.xml`。

## 已知缺口

- 当前生产模块在加载时读取配置，尚未导出无副作用的纯加载函数。
- 未覆盖字段 schema、端口范围、凭据为空或配置权限错误；这些契约需先明确。
