<!-- generated-by: gsd-doc-writer -->
# 架构与模块边界规范

## 系统边界

Chat 是由 Qt 桌面客户端和多个独立服务组成的分布式聊天项目。当前 Windows 发布基线包含 GateServer、StatusServer、ChatServer、VarifyServer 和 Qt 客户端；Server 之间通过 HTTP/TCP、gRPC、Redis 和 MySQL 协作。

```text
Qt Client
  | HTTP: 注册、登录、服务发现
  v
GateServer ------gRPC------> VarifyServer
  |                           | Redis / SMTP
  | gRPC
  v
StatusServer ----Redis/MySQL----+
  |
  | 返回 ChatServer endpoint/token
  v
ChatServer instance A <---gRPC---> ChatServer instance B ... N
  | TCP                                |
  +-------------- Qt Client ----------+
```

该图表达允许的主要依赖方向，不表示所有调用都已完成生产级隔离或 Linux 验证。

## 发布单元

1. GateServer、StatusServer、ChatServer 和 VarifyServer MUST 视为独立发布单元。
2. 每个发布单元 MUST 携带自身运行所需的配置、DLL 或 `node_modules`，不得依赖另一个服务的输出目录。
3. Qt 客户端 MUST 作为独立目录发布，并由 `windeployqt` 补齐 Qt 和编译器运行时。
4. 服务 MUST NOT 依赖目标机器的全局第三方 DLL。
5. `ResourceServer` 当前存在于 `Chat.sln`，但不在 `scripts/windows-local.ps1` 和 Windows CI 的三 Server 发布链路中。将它纳入正式发布前 MUST 明确职责、依赖、测试和 artifact，而不是默认认为已被 CI 覆盖。

## 服务职责

### GateServer

- 负责面向客户端的 HTTP 入口、注册、登录、密码相关流程和服务发现协作。
- MAY 调用 StatusServer 和 VarifyServer 的公开 gRPC 接口。
- MUST NOT 承担长连接聊天会话和跨 ChatServer 消息路由。

### StatusServer

- 负责 ChatServer 选择、登录 Token/Session 校验所需的状态服务。
- MUST 通过明确配置识别可用 ChatServer；选择策略属于 StatusServer，不得散落到 GateServer。
- MUST NOT 直接拥有客户端 TCP 聊天会话。

### ChatServer

- 负责客户端 TCP 长连接、Session、聊天业务和 ChatServer 间 gRPC 通知。
- 项目 MUST 只维护一个 ChatServer 源码目标和一个可执行文件。
- 每个进程 MUST 代表一个实例，实例差异只来自配置。
- 扩展到任意 N 个实例时 MUST 通过配置和管理脚本完成，禁止创建 `ChatServer1`、`ChatServer2` 等复制目录。

### VarifyServer

- 负责验证码生成、Redis 缓存和邮件发送的 gRPC 服务。
- MUST 保持 Node.js 模块边界清晰，外部依赖调用不得渗透到其他服务源码。
- 不得把邮件账号、Redis 密码或验证码写入日志和发布文档。

### Qt 客户端

- 负责桌面交互、客户端状态和协议调用。
- UI 层 MUST NOT 直接操作 Server 的 Redis/MySQL 数据结构。
- 网络 DTO、领域状态、Qt Model 和 Widget 展示 SHOULD 分层，避免把业务规则固化在事件处理函数中。
- `ClientSession` 是 authenticated-session 生命周期的单一 owning Module。返回登录页前 MUST 通过其 `resetSession` 清理 `TcpMgr` connection 状态与 `UserMgr` account transient state，并销毁旧 `ChatDialog` 所有权树；仅隐藏旧页面不构成会话结束。
- `TcpMgr`/`TcpFrameDecoder` 只拥有 connection 生命周期；主题、窗口策略和服务器配置属于应用级状态，不随账号 reset。未来本地缓存 MUST 作为独立 Module 按账号和 schema version 隔离。

## 依赖规则

- 跨发布单元调用 MUST 使用公开协议或明确的数据契约，不得链接另一个服务的私有实现文件。
- 同一能力 SHOULD 只有一个权威实现；公共能力需要共享时，应抽取稳定接口，而不是复制后分别维护。
- 业务层 MUST NOT 依赖构建目录、开发者目录或另一个服务的当前工作目录。
- 数据库表、Redis key 和协议字段属于跨模块契约，修改前 MUST 搜索全部生产者与消费者。
- Singleton 是当前代码中的既有模式，但新模块 SHOULD 通过显式依赖传递降低全局状态；不得为了调用方便无条件新增 Singleton。
- 循环依赖 MUST 被拆分为协议、接口或单向事件；禁止通过头文件互相包含掩盖边界问题。

## 多实例规则

- 所有 ChatServer 实例共享同一二进制，但 MUST 使用唯一的 `SelfServer.Name`、TCP Port、RPC Port、`Log.Name` 和工作目录。
- Peer 和 StatusServer 实例列表当前手工维护；修改实例集合时 MUST 同步所有相关配置。
- 实例之间 MUST NOT 通过共享工作目录交换文件。
- 用户到实例的映射、登录计数等共享状态可通过 Redis 维护，但 key 语义 MUST 在 [Protocol.md](Protocol.md) 的规则下演进。
- 本地端口绑定失败和无效配置 MUST 在实例登记共享状态前失败。

## 新模块准入

新增服务、共享库或后台进程前，设计说明 MUST 回答：

1. 它属于哪个发布单元，谁负责启动和关闭？
2. 它替代还是扩展哪个既有职责？
3. 输入、输出、错误和超时契约是什么？
4. 是否引入新的网络端口、配置、Redis key、数据库结构或第三方依赖？
5. Windows 本地和 CI 如何构建、测试和打包？
6. 是否保持未来 Linux Server 的可实现性？

无法清楚回答时，不应通过复制现有目录先行实现。
