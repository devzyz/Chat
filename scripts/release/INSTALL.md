# Chat Windows x64

本包包含 Qt 客户端、GateServer、StatusServer、ChatServer、ResourceServer 和带 Node.js 的 VarifyServer。
版本与源码提交见 release-manifest.json；下载后可使用 SHA256SUMS 校验 ZIP。

## 配置与启动

1. 准备 MySQL、Redis 和 SMTP。它们是外部服务，不包含在本包中。
2. 首次安装按 migrations/manifest.json 顺序执行 SQL；升级已有数据库前先备份，只应用尚未执行的迁移。
3. 将各应用的 config.ini.template / config.json.template 复制为 config.ini / config.json，
   填写本机地址、端口、数据库和日志设置。C++ 服务凭据填入对应 INI；Varify 凭据通过环境变量
   `CHAT_VARIFY_EMAIL_USER`、`CHAT_VARIFY_EMAIL_PASS`、`CHAT_VARIFY_MYSQL_PASSWORD`、
   `CHAT_VARIFY_REDIS_PASSWORD` 提供，不写入 JSON。模板不包含开发者连接信息。
4. 配置 StatusServer 中的 ChatServer 列表；ChatServer 的实例名、TCP/RPC 端口必须与之对应。
   多实例参考 ChatServer/configs/ 中的模板。
5. 填写 ResourceServer 的监听地址、StatusServer、MySQL 和可写 StorageRoot；相对存储路径以配置目录为基准。
   在各应用目录启动 VarifyServer（node.exe server.js）、StatusServer.exe、ChatServer.exe、ResourceServer.exe、GateServer.exe。
   C++ 服务支持 --config <文件>；Varify 支持 CHAT_CONFIG 和 CHAT_VARIFY_BIND_ADDRESS。
6. 填写 chat-client/config.ini 中的 GateServer 地址及 `[ResourceServer] Url`，启动 chat.exe。

每个 C++ 应用携带所需运行库；保留 VarifyServer 与 proto 的相邻目录关系。
客户端正常关闭窗口；控制台服务使用 Ctrl+C 停止。

## 验证范围

CI 对同一源码运行完整 Linux 真实依赖业务 E2E，并对下载解压后的 Windows 包检查
文件、运行库、配置处理与应用启动。Windows 包启动检查不等同于真实数据库业务 E2E。
