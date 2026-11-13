# Chat

面向局域网的 Qt 桌面聊天项目，支持账号注册与恢复、好友与群管理、跨实例收发消息，以及头像和附件传输。聊天消息持久化后才确认；客户端使用 SQLite 保存历史和待发送消息，断线后可以补拉或重试。

## 服务与数据

| 模块 | 职责 |
| --- | --- |
| [Qt 客户端](chat/) | 登录、会话和消息展示、本地历史、草稿、资源上传与下载 |
| [GateServer](GateServer/) | HTTP 注册、登录、改密与退出；从租约有效的候选 Chat 实例中选服，在 Redis 签发凭据 |
| [ChatServer](ChatServer/) | 校验凭据并绑定 TCP 会话，处理好友、群与消息；写入 MySQL 后确认，通过 gRPC 通知其他实例 |
| [ResourceServer](ResourceServer/README.md) | 通过 HTTP 上传、续传、下载和发布头像；Redis 校验凭据，MySQL 保存引用，文件系统保存字节 |
| [VarifyServer](VarifyServer/) | 通过 gRPC 提供验证码服务，使用 Redis 和 SMTP；同一发布单元附带局域网 TLS 转发入口 |

每个 Chat 实例运行同一二进制。实例内的两个业务分片按连接分工，同一连接上的请求及回调保序；Redis、MySQL 和出站 gRPC 各有独立工作池。详见[服务架构](docs/Architecture.md)与[并发合同](docs/Concurrency.md)。

## 在 Windows 构建与运行

1. 按 [Windows 构建指南](WINDOWS_BUILD.md#environment) 配置 Visual Studio、Qt/MinGW、Node.js 和已有的 vcpkg 安装。日常构建使用只读依赖树。
2. 在仓库根目录分别构建原生服务和桌面客户端：

   ```powershell
   .\scripts\windows-local.ps1 -Task BuildServers -Configuration Release
   .\scripts\windows-local.ps1 -Task BuildClient -Configuration Release
   ```

3. 按 [MySQL 迁移说明](docs/Data.md#operational-entry) 准备数据库，按[运行配置](docs/Operations.md)填写 Redis、SMTP、监听地址及客户端连接配置。跨机器部署需配置 [TLS 入口](docs/Operations.md#tls-入口配置)。
4. 启动 Varify、Gate、Resource 和所需的 Chat 实例，再运行客户端。单个 Chat 实例可用显式配置启动：

   ```powershell
   .\build\windows-servers\Release\ChatServer\ChatServer.exe --config .\ChatServer\ChatServer\configs\chat-01.ini
   ```

同时管理[两个示例实例](ChatServer/ChatServer/configs/)时，使用以下入口：

```powershell
.\scripts\chatserver-instances.ps1 -Task Start `
  -Executable .\build\windows-servers\Release\ChatServer\ChatServer.exe `
  -ConfigDirectory .\ChatServer\ChatServer\configs
.\scripts\chatserver-instances.ps1 -Task Status
.\scripts\chatserver-instances.ps1 -Task Stop
```

每个实例的名称、日志名和监听端口必须唯一；脚本为各进程分配独立工作目录。增删实例时同步维护 Gate 候选列表和 Chat 对端配置。

## 版本与验证

[VERSION](VERSION) 是发布包版本号；[项目状态](docs/Status.md)记录已合并的开发基线、实际验证和待办。按[测试入口](tests/README.md)运行对应模块回归；完整验收、打包和发布条件见 [CI 治理](tests/CI-GOVERNANCE.md)。
