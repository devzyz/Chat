<!-- generated-by: gsd-doc-writer -->
# 协议与数据契约规范

## 基础资料与好友管理

登录协商 `basic_social_v1`；未协商时 1042/1044/1046 返回 `UpgradeRequired`，
客户端禁用新管理入口。身份始终取当前认证会话，不信任请求中的操作者 UID。

| 请求/响应 | 参数与结果 |
| --- | --- |
| 1042/1043 | `name`、`description`、`expected_revision`，更新本人资料并返回 `profile.profile_revision` |
| 1044/1045 | `operation=apply/accept/reject/delete`、`target_uid`、`expected_revision`；申请/接受附 `description` 和 `backname` |
| 1046/1047 | `kind=profile/contacts/applications`；profile 可指定 `target_uid`，目录使用 `after`，返回 `items/next/load_more` |

请求携带 1～64 字节 `request_id`，响应原样回传。版本及游标用规范十进制字符串，零仅代表尚不存在；
资料比较 `profile_revision`，申请比较 `application_revision`（发起者通过 profile 的 `outgoing_revision` 取得），
删除比较 `relationship_revision`。过期写命令返回 `VersionConflict`，不会自动采用最新版本重放。
客户端请求上限 16，十秒超时；超时保留输入并提示重新读取结果。每十秒完整分页刷新资料、关系及申请，
写操作完成后立即触发读取，超时由周期目录恢复，账号切换丢弃旧请求和回调。

用户名、描述和备注最多 255 个字符，用户名去除首尾 SQL 空格且不能为空；重名返回 `NameExists`。
请求仍受 2048 字节 TCP 帧限制。登录响应 1006、批量消息确认 1017 和已协商能力的目录响应 1047 允许最多 8192 字节，
每页最多 50 条并按完整响应 8000 字节预算分页，足以容纳组合的 255 字符姓名、描述和备注。
无法容纳异常旧记录时返回 `ResponseTooLarge`。联系人关系目录完整落盘后即允许发送，申请目录失败不阻断聊天。所有公开资料查询直接投影数据库中的公开字段，不返回密码、邮箱或令牌。

删除双向好友边，同时将原私聊标记为失效并递增关系版本，取消双方申请。历史、回执和已有资源引用保留；
重新申请并接受后复用原 chat_id，递增关系版本。1016 文本/资源新提交携带 `relationship_revision`，
失效或不匹配时拒绝；未携带版本仅兼容初始版本 1。已提交且身份完全相同的 UUID 仍返回原确认，
不重新推送；旧版审批仅能处理初始申请版本 1，不能批准后续重新申请。
实时通知统一使用紧凑 JSON，超过 2048 字节时发送空正文的同步提示，接收端复用增量同步取正文。

## 头像与资源传输扩展

ResourceServer HTTP、头像权限、用户目录和资源消息合同见 [Resources](Resources.md)
及 [资源服务](../ResourceServer/README.md)。资源消息复用 1016/1017/1018，
保留已认证发送者检查；`client_msg_uuid` 写入当前消息表以支持历史身份和重试去重。

## 范围

本规范覆盖 `proto/chat.proto`、`proto/status.proto`、`proto/varify.proto`、gRPC、GateServer/ResourceServer HTTP、ChatServer TCP 包、Redis key/value 和 MySQL 持久化边界。它约束项目定义的契约，不测试或重写第三方库内部实现。

## protobuf

- proto package、service、RPC 方法、message 和字段编号一旦被消费者使用即视为公开契约。
- 已发布字段编号 MUST NOT 复用；删除字段时 MUST 使用 `reserved` 保留编号和旧名称。
- 字段类型、语义和单位 MUST NOT 在原编号上不兼容修改。
- 新字段 SHOULD 为可选兼容扩展，并定义旧客户端未发送时的默认行为。
- 命名使用清晰的 PascalCase message/service 和 lower_snake_case 字段。
- 不得用数据库表的内部列顺序决定 proto 字段设计。
- proto 修改 MUST 核对全部消费者并同步生成结果；GateServer、StatusServer、ChatServer、ResourceServer 和 VarifyServer 按 [proto README](../proto/README.md) 消费共享权威源，禁止创建服务内协议副本或只更新单个消费者。
- 生成的 `.pb.cc/.pb.h`、`.grpc.pb.cc/.grpc.pb.h` MUST 由固定工具链生成，不得手工修改。
- 生成结果和 proto 变更 SHOULD 在同一提交中完成，便于审核来源一致性。

## gRPC

- 每个 RPC MUST 定义成功、业务失败、依赖失败、超时和远端不可用的行为。
- 客户端调用 MUST 设置有限 deadline。当前生产默认值为：连接池借用 1000 ms、Status/Chat RPC 3000 ms、Varify RPC 15000 ms；配置值必须为 100..60000 ms 的正整数。
- callback 或 completion MUST 只完成一次。
- 服务启动 MUST 检查 `BuildAndStart()` 或 `bindAsync` 的结果，端口绑定失败不得报告 ready。
- 连接池 MUST 遵循 [Concurrency.md](Concurrency.md) 的借用、归还和关闭规则。
- 错误码优先使用项目稳定枚举或 gRPC status，不应依赖自然语言文本供程序判断。
- RPC 日志可以包含方法名、对端、deadline 和非敏感标识，不得记录 Token、验证码或密码。
- 连接池耗尽、连接池关闭、deadline exceeded、unavailable 与 cancelled 在内部保留稳定分类；当前公开兼容行为统一映射为 `RPCFailed`，且不自动重试。

## ChatServer TCP

ChatServer 的 Linux TCP 监听器在 bind 前启用 SO_REUSEADDR，使旧会话主动关闭后可以立即重启或接管
已释放的监听端口；不启用 SO_REUSEPORT，已有存活监听器仍必须使新绑定失败。Windows 保留原有
独占绑定设置。此行为不修改消息 wire 字段、服务发现端点或持久化数据。

- 包头中的消息 ID 和 body 长度 MUST 使用明确的网络字节序。
- 读取 MUST 支持半包和连续多包，不得假设一次 read 得到完整消息。
- body 长度 MUST 在分配、复制和解析前限制在协议最大值内。
- 未知消息 ID、非法长度和损坏 payload MUST 安全关闭或返回明确错误，不能越界访问。
- 认证前后允许的消息集合 MUST 明确，未认证连接不得执行需要用户身份的操作。
- 写入 MUST 通过单连接顺序队列串行化，避免多个 `async_write` 交叉数据。
- protobuf 或 JSON payload 解析失败不得继续进入业务 handler。

好友申请的 `fromuid`、好友确认的 `authuid` 必须匹配连接的认证 UID。
申请拒绝自身及不存在的目标用户；确认的内嵌 `applyinfo` / `authinfo` 身份必须与外层一致，
且只能确认发给当前用户的待处理申请。身份不符、反向申请和重复确认返回既有 `UidInvalid`，
不新增好友关系或问候消息。消息 ID、JSON 字段及 schema 不变；重复申请仍保持已有单行。
对应真实依赖合同见 [3D 好友旅程](../tests/services/README.md#phase-3d-foundation)。

历史消息查询使用连接的认证 UID，DAO 在读取消息前检查私聊双方或群成员关系。
非成员访问返回既有 `UidInvalid`，不返回消息；请求和响应字段不变。
客户端按发送者与 UUID 组合去重，ACK/失败只更新当前发送者；历史记录可以确认
同一发送者的待发送 UUID 对应的持久化 ID。对应边界见服务场景 `E03-XMSG-01..08`。

历史按 `message_id > current_msg_id` 升序推进，每页最多十条，并受 2048 字节 TCP body 上限约束。
服务端按紧凑 JSON 的实际字节数返回可容纳的完整前缀，缩短页时保留 `load_more=true`，
游标只推进至实际返回的最后一条，剩余记录由下一页读取；客户端按服务端 ID 合并各页。
非空页游标必须等于页尾 ID，页内 ID 必须严格递增；空页不能声称还有下一页。
重放旧页不倒退已确认游标，也不能重新打开已结束的扫描。模型重新排序时保留持久索引。
历史请求拒绝缺失/非整数/负数/超出当前服务端 int 范围的游标，以及无效 chat_id，
返回既有 `Error_Json`，不返回消息。单条旧数据编码后仍超出帧上限时同样返回此错误，避免空页循环。
本次修复保持 wire 字段、帧长上限和数据库不变；十条是行数上限，不是每次响应的保证数量。
Qt 消费者已按 `load_more` 和页尾游标继续翻页；不需要迁移或新增配置。N-1 运行兼容性仍须实际发布基线验证。

## HTTP/JSON

- GateServer endpoint MUST 明确方法、路径、请求字段、响应字段和错误码。
- JSON 输入 MUST 校验类型、必填字段、长度和允许字符，不得只判断 key 是否存在。
- HTTP 层负责传输与输入校验，注册、登录和密码规则 SHOULD 由业务层处理。
- 响应 MUST 使用一致的错误 envelope；不得将 C++ 异常文本直接返回客户端。
- 密码和验证码 MUST NOT 出现在 URL、访问日志或错误响应中。

## Redis

Chat 在线位置继续使用 `uip_<uid>`（实例名）与 `usessionid_<uid>`（全局唯一 Session ID）。
仅 `RedisUserPresenceStore` 维护这两个 key：Lua 原子读取/发布两项，删除时同时比较实例名和
Session ID。查询区分 Found、NotFound、Unavailable；发布失败不报告 Chat 登录成功。
本次保留原有无 TTL 的在线记录，崩溃残留及租约续期作为后续设计，不声称已解决。

跨服 `KickUserReq.session_id = 2` 指定被替换连接；缺失字段返回 UidInvalid 并拒绝踢人。
这是 protobuf 的附加字段，但旧服务端仍会按 UID 踢人，因此本次要求所有 ChatServer 同步升级，
不支持新旧实例混合运行。客户端 TCP 帧和登录响应字段不变。替换以服务端关闭连接为准，
不保证客户端收到离线通知；Send 的 Accepted 只表示排队，不表示写入或送达。


- Redis key 前缀和 hash 字段属于跨实例契约，修改前 MUST 搜索全部读写方。
- 新 key SHOULD 包含清晰命名空间；测试 key MUST 带唯一 run id，避免污染开发数据。
- 临时数据 MUST 定义 TTL；永久数据必须说明为什么不应过期。
- 分布式锁 MUST 使用唯一 owner 标识和有限租约，仅 owner 可以释放。
- 多步状态修改需要原子性时 MUST 使用事务、Lua 或等价原子操作，不能依赖进程内 mutex。
- Redis 不可用时的 fail-fast、重试或降级策略 MUST 在调用接口中明确。

## MySQL

- SQL MUST 使用参数绑定，禁止拼接用户输入。
- 数据写入跨多表或多步骤时 MUST 明确事务边界和回滚行为。
- 连接借用和归还遵循连接池契约，异常路径不得泄漏连接。
- schema 变更 MUST 有版本化迁移方案；不能只修改某台开发数据库。
- 分页查询 MUST 使用稳定排序和明确游标语义，避免跨页重复或遗漏。
- 数据库错误日志不得包含密码或完整敏感数据。

## 兼容性检查

任何协议或数据契约变更提交前 MUST 回答：

1. 哪些生产者和消费者受影响？
2. 新旧版本能否滚动共存？
3. 默认值、未知字段和未知错误码如何处理？
4. 是否需要更新示例配置、生成文件、测试和发布包？
5. 是否增加 Redis/MySQL 清理或迁移步骤？

至少应有项目级 round-trip、handler 或 Integration 测试验证契约，而不是只确认生成代码能编译。

## Incremental private-message synchronization (`sync_v1`)

Request 1027 adds `mode: "sync_v1"`, authenticated `uid`, `chat_id`, nonnegative `after_id`, and `request_id` (at most 64 bytes). Response 1028 echoes the envelope and returns `error`, `msgs`, `next_cursor`, `load_more`. Each row has `message_id`, `send_id`, `recv_id`, raw `content`, epoch-seconds `created_at`, and `msg_uuid`.

Rows are ordered by increasing server ID; only IDs greater than `after_id` are returned. A page contains at most 50 rows and fits the complete encoded response. Response 1028 permits a body up to 65535 bytes; login 1006, batch ACK 1017 and social directory 1047 permit 8192 bytes. Other messages and client requests retain the 2048-byte bound. Legacy history keeps its existing serializer and fields. Old clients cannot consume large sync responses; update all ChatServer writers before deploying the new client.

The client atomically commits a whole page and its cursor; ACKs/pushes never advance it. Existing committed local history is trusted. Synchronization and deployment details: [MessageStorage](MessageStorage.md).

Explicit legacy history requests remain usable while MessageService is active. TcpMgr accepts
one matching legacy response per requested chat in the current connection; unsolicited legacy
responses are rejected. These pages update only the legacy model, never the persisted sync cursor.
Responses carrying `request_id` belong to MessageService and do not complete legacy history commands.

## Private message receipts v1

The negotiated `message_receipts_v1` capability adds TCP 1029/1030 report, 1031 change hint,
1032/1033 independent revision synchronization. Complete envelopes, authorization, limits and
error semantics are defined in [MessageStates](MessageStates.md#回执与同步). All five IDs retain
the 2048-byte body limit. Revisions are canonical signed-64-bit decimal strings.
1016 may carry an `attempt_id` string (maximum 20 bytes); 1017 echoes it without changing the
UUID idempotency identity or the original batch commit semantics. ACK proves Sent, never Read.
The client reserves framing metadata space by limiting stored business requests to 1950 bytes.
`NotifyMessageReceiptChanged` is an additive Chat-only unary RPC; its hint is recoverable through
periodic authoritative synchronization and is sent only to a session that negotiated receipts.

## 基础文字群聊

群最多 20 人（含群主），创建者从自己的好友中选择 1～19 人。群主可添加自己的好友、
移除普通成员、转让群主、改名及解散；普通成员可退出。群只剩群主时仍可保留。

| 请求/响应 | 合同 |
| --- | --- |
| 1034/1035 创建 | `name`（非空、最多 60 UTF-8 字节）、`members`（不重复正整数 UID，不含自己）、`request_id`（UUID）；事务校验好友关系 |
| 1036/1037 资料 | `chat_id`、`after_uid`、`request_id`；返回 `members`、`member_count`、`next_uid`、`load_more`；客户端按同一群版本拼接分页 |
| 1038/1039 管理 | `chat_id`、`request_id`、`expected_revision`、`operation`；`add` 携带 `members`，`remove/transfer` 携带 `target_uid`，`rename` 携带 `name`，`leave/dissolve` 无附加参数 |
| 1040/1041 好友备注 | `target_uid`、`name`、`request_id`；只更新认证用户自己的好友备注，空字符串清除备注 |

身份只取认证 Session。成功返回 `error:0`；管理结果包含群权威状态和请求身份。
`group_revision`、`membership_epoch`、`expected_revision` 使用规范正十进制字符串。
`group_state` 为 `active/left/removed/dissolved`，`joined_after_id` 为本代入群时已提交消息边界。
业务拒绝通过 `group_error` 区分 `InvalidRequest`、`Forbidden`、`NotFriend`、`NotMember`、
`MembershipChanged`、`OwnerMustTransfer`、`MemberLimit`、`GroupDissolved`、`VersionConflict`、
`RequestConflict` 与 `StorageUnavailable`。客户端对版本冲突刷新后重新选择操作；存储不可用及超时保留原请求重试。

- 管理以 `(actor_uid,request_id)` 幂等，原参数和结果与业务变更同事务提交；相同身份不同参数拒绝。
  重试先读取原成功结果，不因之后退群或解散而重新执行。客户端管理命令先保存到账号 SQLite，
  超时、关闭资料窗口和重启后仍可重试；已完成终态才清除。群名、群主及成员变化不改变原建群身份：
  创建幂等使用不可变 `creator_uid`、`original_name`、原成员集合。
- 登录协商 `group_membership_v1`。群消息复用 1016/1017，携带 `chat_type:"group"`、
  `to_uid:0`、当前 `membership_epoch`；旧客户端不能绕过成员代次发送。群图片、视频、文件复用
  既有资源描述和提交链路。私聊正收件人、UUID、ACK/attempt 和回执合同保持不变。
- 群历史复用 1027/1028 `sync_v1`、`recv_id:0`；同步关联成员代次，旧历史入口也校验当前资格和边界。
  加入、退出、移除、解散与消息提交锁定同一群行；新成员只读取边界之后消息，重新加入产生新代次。
  离群后禁止服务端读取和新发送，本地历史保留；群不产生私聊送达/已读事实。
- 登录及 1025/1026 目录保留用户曾加入的群，显式返回状态，不根据某页缺失推断删除。
  客户端每 10 秒从起点完整分页，每 2 秒同步有效群；旧版本不覆盖新状态，旧代次响应和 outbox 不重发。
  同服、跨服及离线恢复以 MySQL 为权威，不新增广播设施。

普通响应遵守 2048 字节包体限制，群资料按实际序列化大小分页；1006、1017、1028 和已协商的社交目录 1047 按各自读响应上限处理。
协议需要 migration 006 及相应二进制合同；迁移入口与恢复边界见 [Data](Data.md)。

## 用户搜索请求关联

1007 请求可携带非空、最多 64 字节的字符串 `request_id`，1008 在成功、用户不存在及可解析的参数错误响应中原样回传。旧客户端省略该字段时仍收到不含该字段的旧格式响应；消息 ID、UID/名称查询行为不变。

新版客户端生成 UUID，等待十秒后结束当前搜索；只消费匹配当前搜索的响应。无编号响应结束等待并提示更新服务器，不猜测其对应请求。完整安全重试要求更新 ChatServer。断线、取消及账号结束使当前等待失效。

### 登录和批量确认的有界响应

协商 `basic_social_v1` 的登录不内嵌全量好友、申请及会话目录，返回空 `chat_list`、
`current_chat_id=0`、`load_more=true`；客户端从游标 0 拉取分页，并通过 1046 获取社交目录。
申请目录同时返回 `fromuid`、`touid` 和申请时的 `backname`，保留旧审批请求所需的完整身份及备注。
旧登录响应超过上限时返回 `login_error=UpgradeRequired`，不提交会话绑定。
1017 使用紧凑 JSON，消息入库前以最大消息 ID 预估完整 ACK，超限批次整体拒绝。
请求上限仍为 2048 字节；客户端与 ChatServer 必须同步升级。

### 验证码用途与认证生命周期

`POST /get_varifycode` 和 `GetVarifyReq.purpose` 接受 `register`（省略时默认）或
`reset_password`。注册与重置分别使用 `code_` / `code_reset_` 邮箱键，消费端由路由确定用途。
每用途十分钟最多八次校验；八位随机码十分钟有效，Lua 原子消费。
并发发送合并与一分钟复用也按邮箱及用途隔离；数据库写入失败后需重新申请。

`POST /logout` 只接受正整数 `uid` 和非空 `token`（最多 128 字节）。仅匹配当前 Token 才撤销，
缺失视为已退出，旧凭据不能删除新登录，返回 `1011`；依赖失败返回错误，客户端保留账号供重试。
改密先在账号级 MySQL 咨询锁内提升认证代次、撤销旧 Token，再写密码；撤销失败不改密码，
密码写入失败则保守地保持撤销。登录在同一账号锁内校验口令并完成选服。
`GetChatServerReq.auth_version` 与 Redis 中的认证代次不匹配时禁止发布 Token，阻止迟到 RPC 恢复旧认证。

Token 是 24 小时空闲租约：当前 Chat 会话的业务请求、心跳及推送接收校验原子核对绑定 Token 后续期。
改密、退出、替换登录后，旧连接下一次收发校验失败并关闭；已开始的事务和已写出的字节不承诺撤回。
资源请求继续经 Status 校验。强制终止客户端不能保证发送退出请求，凭据最终由撤销或空闲到期失效。
暂时断线使用原 Token 和原端点，按 1/2/4/8/16 秒退避最多五次，每次认证十秒超时；
恢复时保留当前页面、草稿和原消息 UUID。被踢、凭据失效和主动退出不触发自动重连。

升级需同时部署 Gate、Status、Chat、Varify 与客户端：新增 protobuf 字段保持线格式兼容，
但旧 Gate 不传认证代次、旧客户端不传重置用途，不能满足新认证行为合同。部署前断开旧会话。
