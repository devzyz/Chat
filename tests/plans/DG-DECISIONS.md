# Phase 2.5 实施前合同决策

本文件记录 `PHASE-2.5-PLAN.md` 的 DG-01..DG-04。只有状态为
`Confirmed` 的决策才能进入对应实施 plan。变更已确认决策必须遵守
`CI-GOVERNANCE.md` 的 D-04 合同评审流程。

## DG-01 — gRPC deadline 与连接池等待

状态：**Confirmed**
确认日期：2026-08-25
阻断计划：Plan 2.5-05

### 合同

| 项目 | 已确认规则 |
| --- | --- |
| Pool acquire timeout | 默认 1 秒 |
| Status RPC deadline | 默认 3 秒 |
| Chat-to-Chat RPC deadline | 默认 3 秒 |
| Varify RPC deadline | 默认 15 秒 |
| 配置 | 各发布单元可配置；启动时校验为 100ms–60s 的有限正整数 |
| 测试值 | 测试可注入 50–200ms 的短值，不等待生产默认时间 |
| Pool close | 唤醒全部等待者；新 borrow 立即返回 `Closed`；重复 close 幂等 |
| Pool exhausted | acquire timeout 后返回 `PoolExhausted`，不得无限等待 |
| Module 内部错误 | 区分 `PoolExhausted`、`Closed`、`DeadlineExceeded`、`Unavailable`、`Cancelled` |
| 公开错误 | Phase 2.5 统一映射到稳定的 `RPCFailed`；扩大公开错误码需独立兼容评审 |
| 自动重试 | Phase 2.5 禁止；只有具备幂等键和正式重试合同后才能增加 |
| 关闭上限 | 拒绝新 borrow、唤醒等待者；已开始 RPC 最迟在自身 deadline 后结束 |

### 验证义务

- Gate、Status、Chat 的实际 production pool 都必须覆盖 borrow/return、耗尽、close 唤醒、重复 close、关闭后 borrow/return。
- 动态 loopback 必须覆盖 success、unavailable、deadline exceeded 和对端 shutdown。
- 删除 deadline、恢复无限等待或错误 stop 状态时，回归测试必须 RED。
- 日志只记录方法、非敏感 endpoint、deadline 和错误类别，不记录 Token、验证码或密码。

## DG-02 — Qt session reset

状态：**Confirmed**
确认日期：2026-08-25
阻断计划：Plan 2.5-06

### 合同

采用严格会话隔离：只要客户端返回登录界面，当前认证会话即结束。主动退出、切换账号、服务器踢下线，以及异常断线后返回登录页，都执行完整会话重置。

| 类别 | 已确认规则 |
| --- | --- |
| connection 状态 | 停止新发送，关闭 socket 和连接计时器，清空 `TcpFrameDecoder` 半包缓存及 pending batch；新连接不得继承旧连接字节或确认状态 |
| authenticated session | 清空用户信息、Token、好友/申请/聊天映射、分页与加载游标、当前会话选择和其他账号级 transient state |
| Qt 页面与模型 | 销毁或重置旧聊天页面及其 `MessageModelStore`；仅隐藏旧页面不视为完成重置 |
| 允许保留 | 主题、窗口参数、服务器配置等非账号敏感的应用级配置 |
| 未来本地缓存 | 必须属于独立 Module，并按账号和 schema version 隔离；session reset 不能依靠 `UserMgr` 偶然保留，也不能让另一账号继承其内存内容 |
| 幂等性 | reset 可重复调用；不得重复弹窗、重复通知、二次释放或改变已经完成的结果 |
| 关闭分类 | 区分预期关闭与异常掉线；主动退出或切换账号不得显示“连接超时”等异常提示 |
| 当前重连策略 | 当前没有自动重连状态机；异常断线既然返回登录页，就执行 connection reset 和 authenticated-session reset |

### Module 与 Interface 边界

- connection lifecycle 由 `TcpMgr`/`TcpFrameDecoder` 所属 Module 清理。
- authenticated-session lifecycle 由单一 owning session Module 编排，并调用 `UserMgr`、聊天页面/模型等各自的生产 Interface。
- reset 是生产生命周期 Interface，不增加 `clearForTest`、测试专用全局开关或测试侧状态复制。
- 若未来加入自动重连，必须先另行确认可恢复状态、重放/幂等和隐私合同，不能弱化本决策中的跨账号隔离。

### 验证义务

- 半帧断线后建立新连接，新帧不得与旧字节拼接。
- 旧 session 的 pending ack/batch 不得修改新 session。
- 账号 A 退出后登录账号 B，B 不得看到 A 的 Token、好友、申请、会话、模型、游标或临时 UI 状态。
- 对同一断线重复触发 reset，只产生一次用户可见状态迁移和通知。
- 预期关闭与异常掉线走可区分、可断言的路径。
- 临时移除任一必要 reset 调用时，对应回归测试必须 RED。

## DG-03 — Gate 响应 allowlist

状态：**Confirmed**
确认日期：2026-08-25
阻断计划：Plan 2.5-03

### 合同

采用端点级最小响应 allowlist。响应必须从明确的生产 response-shaping Interface 构造，禁止复制请求 JSON 后再删除已知敏感字段。

| Gate endpoint | 成功响应允许字段 | 失败响应允许字段 |
| --- | --- | --- |
| `/get_varifycode` | `error` | `error` |
| `/user_register` | `error` | `error` |
| `/reset_pwd` | `error` | `error` |
| `/user_login` | `error`、`uid`、`token`、`host`、`port` | `error` |

### 安全与兼容规则

- `email`、`user`、`passwd`、`password`、`confirm`、`varifycode`、`varify` 不得出现在上述公开响应中。
- `token` 只允许出现在登录成功响应中，不得进入失败响应、日志或测试报告。
- JSON 解析失败、业务失败、RPC 失败及内部异常只返回稳定公开的 `error`，不得返回内部异常文本、堆栈或依赖详情。
- 日志不得记录请求正文；邮箱只允许记录长度或经过正式规则生成的脱敏值。
- 当前客户端对注册/重置响应中邮箱以及注册 UID 的读取只用于日志，不构成保留这些字段的业务理由；实现时同步移除无效读取。
- allowlist 是精确字段集合；任何新增公开字段必须按 `CI-GOVERNANCE.md` D-04 完成合同评审。

### 验证义务

- success 与每一类 failure 路径均断言响应 key set 与上表完全相等。
- 使用合成密码、验证码、Token 和邮箱标记，断言禁止值及禁止字段名均未出现在响应或捕获日志中。
- 临时恢复任一敏感字段回显或加入未评审字段时，对应回归测试必须 RED。
- 测试与 handler 必须调用同一个生产 response-shaping Interface，不复制 JSON 序列化逻辑。

## DG-04 — proto 权威来源

状态：**Confirmed**
确认日期：2026-08-25
阻断计划：Plan 2.5-02

### 合同

采用按服务拆分的仓库级单一权威 proto Module。目标结构为：

```text
proto/
├── varify.proto
├── status.proto
└── chat.proto
```

| 权威源 | 所有者与消费者 |
| --- | --- |
| `proto/varify.proto` | Varify 服务拥有；VarifyServer 和 GateServer 消费 |
| `proto/status.proto` | Status 服务拥有；StatusServer、GateServer 和 ChatServer 消费 |
| `proto/chat.proto` | Chat 服务拥有；只包含 Chat-to-Chat 专有 RPC/消息，由 ChatServer 消费 |

当前四份可独立编辑的 `message.proto` 不再作为并列权威源。Gate 按需生成 Varify/Status，Status 只生成 Status，Chat 生成 Status/Chat，Node Varify 直接加载权威文件。Phase 2.5 保持现有 `package message`，不借迁移改变 wire service 全名。

### 生成物与工具链

- `.proto` 是权威源；所有生成代码禁止手工修改。
- 固定 `protoc`、C++ gRPC plugin 及 Node loader/tooling 版本，并记录可复查的生成命令。
- 若迁移期间 Visual Studio 项目仍提交生成文件，CI 必须从权威源重新生成并验证工作树无差异；生成副本不能接受独立修改。
- 最终由各消费项目引用对应生成输出，不通过复制完整协议超集来获得少量共享定义。

### 兼容合同

- CI 生成 descriptor set，并与上一正式发布版本的 descriptor 基线比较。
- 如果当前尚无可审计的正式发布基线，则以 Phase 2.5 完成时的 descriptor 和无秘密 wire fixture 作为初始基线；之后只能随正式发布晋升。
- 禁止复用字段号、改变既有字段类型或 cardinality、改变 package/service/RPC 全名，以及改变既有 RPC 的请求或响应类型。
- 删除字段时必须同时 `reserved` 原字段号和字段名。
- 新增兼容字段或 RPC 允许，但必须按 D-04 完成合同评审，并增加当前版本与上一正式发布版本的互操作证据。
- 旧 wire fixture 必须能被当前 C++/Node 消费者解析；未知字段不得破坏旧消费者。

### 扩展规则

- 后续群聊、文件传输、语音、音视频和局域网通信按 Module 增加独立 proto 文件。
- 跨 Module 类型只有在确实形成稳定共享语义时才抽取公共 proto；禁止重新形成无边界的巨型 `message.proto`。
- fixture、descriptor 和测试报告不得包含真实邮箱、Token、验证码或其他凭据。

### 验证义务

- 任一消费者私自修改共享 service、RPC、字段号或类型时，兼容检查必须 RED。
- 从干净环境使用固定工具链可重复生成 C++/Node 所需输出。
- Gate↔Varify、Gate↔Status、Chat↔Status 以及 Chat↔Chat 的实际消费者均从对应权威文件构建。
- 当前/上一发布版的静态 descriptor、旧 wire fixture和必要的动态 loopback 共同作为兼容证据；同版本 round-trip 不能单独宣称兼容。
