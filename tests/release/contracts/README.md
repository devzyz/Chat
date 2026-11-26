# 发布包验证

正式发布由新建的 `vX.Y.Z` 稳定标签触发。两平台的回归、四个真实程序包及解包冒烟均须通过。
在 **Actions → Release → Run workflow** 手动运行同一流程进行演练；发布和部署作业跳过。
演练使用本地发布存储，实际核对附件字节并按两个 tag 拉取同一 Linux 服务包。

| 正式版附件 | 独立滚动 Release `latest` |
| --- | --- |
| `Chat-client-windows-x64.zip` | 同步 |
| `Chat-server-windows-x64.zip` | — |
| `Chat-client-linux-x64.tar.gz` | — |
| `Chat-server-linux-x64.tar.gz` | 同步 |

固定入口为 `/releases/tag/latest` 和 `/releases/download/latest/<文件名>`。
滚动条目标记为预发布，指向最高已发布正式版本；其附件和 tag 需要允许更新。
每个正式版使用同一提交、同次通过验证的四个包，作为草稿上传，核对 GitHub 记录的字节摘要后公开。
同步滚动版直接下载正式版的两个原件。正式版保留历史；可重试未完草稿或单独重跑滚动作业。
滚动附件逐个更新；更新异常时恢复上一正式版的两个文件。需要锁定同一版本的消费者先解析正式 tag。
初次启用前确认仓库策略允许更新滚动 Release 和 `latest` tag。

## 本地回归

```powershell
python tests/release/contracts/run_contracts.py
node --test tests/build/ciBudget.test.js
```

使用临时 ZIP 验证缺文件、修改字节、错误 SHA、版本、配置清理、proto 布局、失败报告以及草稿重试与发布保护。
MySQL 工具测试使用非规范路径并比较解析后的路径，覆盖 Windows 临时目录短文件名展开的等价性。
测试自动发现，不手工维护精确数量或 Test ID 清单；JUnit 输出到 `build/test-results/release_candidate_contracts.xml`。
合成字节测试不是实际程序启动证明。

## 包与运行验证

```powershell
python scripts/release/package.py --artifacts <本次CI制品目录> --output <新目录> --sha <完整SHA>
python scripts/release/smoke.py --directory <ZIP所在目录> --sha <完整SHA> --report <JUnit路径>
```

输入包含 GateServer.zip、ChatServer.zip、ResourceServer.zip、chat-client.zip、VarifyServer.zip；每种恰好一份。
Server ZIP 必须携带 MSVC redistributable，Varify ZIP 携带 node.exe；组装时补入相邻 proto 和版本化 SQL。
真实运行配置替换为空白模板；使用说明随包提供，详见 [INSTALL](../../../scripts/release/INSTALL.md)。

Windows 冒烟验证摘要、文件、Gate HTTP 与 Chat 监听启动、独占控制台停止、Qt 窗口和包内 Node/gRPC 模块加载。
默认用独立临时 PG 运行包内迁移、Gate 和两个服务的启动验证。ResourceServer 另外验证 HTTP 认证拒绝和正常停止。
通过 `CHAT_PG_BIN` 指定 initdb 和 pg_ctl 的目录；psql 单独安装时设置 `CHAT_PG_CLIENT`。
数据库工具仅用于夹具，服务进程仍使用包内 DLL。兼容验证可显式选择 `CHAT_SMOKE_DATABASE=mysql`。
该模式的 MySQL 8 工具从 `CHAT_SMOKE_MYSQL_BIN`、PATH 或 Program Files 的唯一 MySQL 8 安装定位；
服务端及客户端均在创建数据前校验为 MySQL 8；缺失工具或版本不符即失败，不自动安装。
MySQL 初始化及运行错误通过 `--console` 写入报告旁的 `<report-stem>-logs/resource-mysql.log`，
ResourceServer 启动日志也保留于同目录；CI 连同冒烟 XML 上传，不随临时应用目录清理而丢失。
程序只使用包内运行库，PATH 不引入构建工具目录；
临时数据库仅验证启动，未执行资源业务或真实 Redis 凭据校验，不声称覆盖 Windows 业务 E2E。
同 SHA 的 Linux 完整业务 E2E 必须先成功。冒烟失败不发布，JUnit 即使失败也上传。

正式 tag 提供发布版本号，手动演练使用仓库 `VERSION` 作为示例版本。
