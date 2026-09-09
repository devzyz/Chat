<!-- generated-by: gsd-doc-writer -->
# 配置、错误与日志规范

## 配置来源

项目配置采用显式路径优先模型：

1. 命令行 `--config <path>`。
2. `CHAT_CONFIG` 环境变量。
3. 当前工作目录的默认配置文件。

新增或统一配置加载器 MUST 遵循该优先级。缺少 `--config` 的路径、未知参数或无法读取的文件 MUST 返回非零状态，不能静默回退到默认配置。

## 配置校验

- 配置 MUST 在启动监听、创建后台线程和登记 Redis 状态前完成校验。
- 必填 section、key 和非空值 MUST 逐项校验，错误包含配置路径和字段名。
- 端口 MUST 是完整的十进制整数且位于 1..65535；同一实例的 TCP/RPC 端口不得冲突。
- endpoint MUST 同时校验 Host 和 Port，不得用 `atoi` 的静默 0 表示错误。
- 枚举、布尔、超时、容量和长度 MUST 拒绝尾随垃圾字符与越界值。
- 新配置项 MUST 同步默认值策略、示例配置、发布复制规则和自动测试。
- 配置结构变化涉及多个服务时 MUST 搜索所有消费者，不允许只更新一份复制的 INI。
- Gate/Chat 的可选 `[Grpc]` 毫秒配置包括 `PoolAcquireTimeoutMs`、`StatusDeadlineMs`、`ChatDeadlineMs` 与 `VarifyDeadlineMs`；缺省时采用协议规范默认值，显式值必须在 100..60000 范围内并在监听前验证。

## Varify SMTP 运行配置

`VarifyServer/config.json` 的 `email` 支持 `host`、`port`、`secure`、`auth` 和
`deadlineMs`。省略字段时保持 `smtp.qq.com:465`、`secure=true`、`auth=login`，
总发送期限默认 10000 毫秒。显式字段严格校验；端口必须是 JSON 整数 1..65535，
`secure` 必须是布尔，`auth` 只能为 `login`/`none`，期限必须为 100..60000 毫秒整数。
配置在默认服务组合创建 Redis/SMTP 对象及监听之前校验。

`CHAT_VARIFY_EMAIL_USER` 仍指定发件地址；`login` 模式的 SMTP 身份及密码来自
`CHAT_VARIFY_EMAIL_USER` / `CHAT_VARIFY_EMAIL_PASS`，禁止在 JSON 写入凭据。
`none` 模式不提供 SMTP auth 对象，也不要求 SMTP 密码，适用于隔离 Mailpit：

```json
{
    "email": {
        "host": "127.0.0.1",
        "port": 1025,
        "secure": false,
        "auth": "none",
        "deadlineMs": 2000
    }
}
```

以上只是 email section 示例；完整 JSON 仍需现有 mysql/redis section 与所需环境变量。
CI 将示例端口替换为本次服务动态映射，不连接默认公网 SMTP。
`secure=false` 保留 Nodemailer 的可用时 STARTTLS 行为，不关闭证书验证；
Mailpit 未配置 TLS 时使用本次隔离 loopback 明文连接。

生产 `createSmtpAdapter` 惰性创建连接，`SendMail` 仅返回 Delivered/Rejected/Unavailable/
DeadlineExceeded/InvalidConfig，不返回 provider response、密码、验证码或正文；
正式 gRPC handler 将所有非 Delivered 结果映射到既有 Exception，公开协议编号不变。
总 deadline 或 adapter `close()` 终止本次 socket 并保证结果只完成一次，不自动重试。
Delivered 仅表示 SMTP 接受；超时临界点是否已被远端接受存在歧义，不能声明最终邮箱投递或网络 exactly-once。

所锁 Nodemailer 8.0.6 的普通 transport.close 不取消活跃发送，因此适配器通过受支持的
getSocket 持有连接，在 deadline 时 destroy，同时设置有限阶段超时。
真实 Mailpit/故障测试、仅按本 run 删除邮件及报告边界见
[SMTP 测试](../VarifyServer/test/smtp/README.md)。依赖锁与现有发布文件收集方式不变；
新增 `smtpConfig.js` 与其他生产 JavaScript 一并发布。

## ChatServer 多实例

- 每个受管理实例 MUST 使用唯一的 `SelfServer.Name`、TCP Port、RPC Port、`Log.Name` 和配置文件 basename。
- 实例 MUST 有独立工作目录、stdout、stderr 和状态文件。
- 状态文件识别进程时 MUST 同时校验 PID、可执行文件路径和启动时间，禁止仅凭 PID 终止进程。
- 启动批次失败时只允许回滚本批次创建的进程和状态，不得删除先前独立运行实例的状态。
- 管理脚本打印错误时 SHOULD 包含实例、配置路径、退出码和输出位置。

## 凭据和敏感数据

- 密码、邮件授权码、Token、验证码、私钥和完整连接字符串 MUST NOT 写入源码、测试、文档、日志或 artifact。
- 示例配置 MUST 使用明显的占位值，不得复制个人开发凭据。
- 日志 MAY 记录字段存在与否、目标主机和非敏感标识，但 MUST 对秘密值做完全隐藏。
- JSON/INI 解析错误如果可能回显原始行，调用层 SHOULD 净化后再输出到用户或 CI。
- CI 密钥只能通过 GitHub Secrets 或等价安全机制注入，不得写入 workflow 常量。

## 错误模型

- 启动阶段的配置错误、端口绑定失败、日志初始化失败和关键资源创建失败 MUST 令进程非零退出。
- 可恢复的远端依赖短暂不可达 MAY 重试，但必须定义最大尝试次数、退避、超时和最终状态。
- 库层错误 SHOULD 返回结构化结果或抛出带上下文的异常；不得只打印日志后返回一个看似成功的默认对象。
- 捕获异常时必须保留原始错误语义，同时补充操作、实例或 endpoint 上下文。
- 清理失败不得覆盖原始异常；可作为附加日志记录。
- 对客户端公开的错误码必须稳定，内部异常文本不得直接作为协议字段泄漏。

## 日志级别

| 级别 | 使用场景 |
| --- | --- |
| Trace | 高频协议或状态细节，仅用于明确启用的诊断 |
| Debug | 配置来源、状态转换、连接池统计等开发信息 |
| Info | 服务启动、监听地址、实例启动停止和重要业务里程碑 |
| Warning | 可恢复异常、重试、陈旧状态和降级行为 |
| Error | 当前操作失败，需要排查但进程可能继续 |
| Critical | 无法继续保证进程正确性的故障 |

- 日志 MUST 包含服务或实例身份；请求级问题 SHOULD 包含可公开的 request/session/message 标识。
- 同一个错误 SHOULD 在拥有最多上下文的一层记录一次。
- 高频循环和重试 MUST 限频，禁止日志风暴。
- 日志消息 SHOULD 使用参数化格式，不要先拼接大量临时字符串。
- 日志初始化前的致命错误写入 stderr；初始化后使用统一日志设施。

## 可观察启动

- “进程存在”不等于服务 ready。未来 Integration 测试和部署脚本 SHOULD 使用端口、健康接口或协议探测判断 ready。
- 启动成功日志 MUST 在本地端口绑定成功后输出。
- 服务停止 SHOULD 记录原因和完成状态。
- CI 失败时 SHOULD 保存对应 stdout/stderr 和 JUnit/XML；不得只保留退出码。

## 当前迁移要求

### Redis adapter 生命周期与期限（3C-04）

- Gate、Status、Chat 使用同一个纯技术 hiredis pool，业务 key/field 和锁 owner 规则不变。
  默认借用、连接与命令 socket timeout 为 2000 ms；每次借用最多创建一个替代连接，
  不重放业务命令。空闲连接借出前以 PING 重新验证，错误 context 归还时丢弃。
- C++ Redis Host 必须是数字 IPv4/IPv6 或 `localhost`（映射为 `127.0.0.1`）；
  其他 DNS 名称在池构造时明确报配置错误。hiredis 的同步 DNS 不受 socket timeout 约束，
  因而不能把未限定的名称解析标成有限连接。原有数字地址配置不变。
- 池按需连接，没有后台心跳线程；`close()` 幂等并唤醒等待者。调用方在销毁池前必须
  停止业务请求并归还借出的连接；已借出的命令通过自身有限 timeout 收敛。
- Varify 的 `redis.js` 在导入时不读取配置、不创建客户端；默认 handler 组合注入配置，
  首次操作才创建 ioredis 连接。JSON `redis.connectTimeoutMs` / `commandTimeoutMs`
  可选，默认各 1000，合法范围为整数 1..60000；密码仍仅由
  `CHAT_VARIFY_REDIS_PASSWORD` 提供。Node 连接总期限为两者之和，包含异步解析和 AUTH。
- ioredis 关闭离线队列、自动重发与自动重连；失败命令保留 null/false 旧映射，
  后续独立操作才创建新连接。验证码写入为单条 `SET key value EX seconds`，不再分步 EXPIRE。
  `Quit()` 直接断开并取消等待，不发送可能阻塞的 QUIT；入口绑定失败与 SIGINT/SIGTERM
  都关闭 Redis/SMTP adapter，gRPC 排空最多 10 秒后强制关闭。
- 测试只使用本次 RunContext 的动态地址、合成密码和 key prefix；真实服务验收状态见
  主工作区 `docs/Status.md`，本地 loopback fault 测试不代表真实 Redis 数据/重启测试已通过。

ChatServer 已具有较完整的 fail-fast 配置和异常清理。GateServer、StatusServer 和 VarifyServer 的历史启动逻辑仍可能存在校验或错误传播差异；新改动 MUST 朝统一契约收敛，不能把现有差异复制为新的正确行为。
