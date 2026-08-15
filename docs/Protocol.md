<!-- generated-by: gsd-doc-writer -->
# 协议与数据契约规范

## 范围

本规范覆盖 `message.proto`、gRPC、GateServer HTTP、ChatServer TCP 包、Redis key/value 和 MySQL 持久化边界。它约束项目定义的契约，不测试或重写第三方库内部实现。

## protobuf

- proto package、service、RPC 方法、message 和字段编号一旦被消费者使用即视为公开契约。
- 已发布字段编号 MUST NOT 复用；删除字段时 MUST 使用 `reserved` 保留编号和旧名称。
- 字段类型、语义和单位 MUST NOT 在原编号上不兼容修改。
- 新字段 SHOULD 为可选兼容扩展，并定义旧客户端未发送时的默认行为。
- 命名使用清晰的 PascalCase message/service 和 lower_snake_case 字段。
- 不得用数据库表的内部列顺序决定 proto 字段设计。
- proto 修改 MUST 同步所有持有副本的服务；当前 GateServer、StatusServer、ChatServer 和 VarifyServer 均包含相关协议文件，禁止只更新单个消费者。
- 生成的 `.pb.cc/.pb.h`、`.grpc.pb.cc/.grpc.pb.h` MUST 由固定工具链生成，不得手工修改。
- 生成结果和 proto 变更 SHOULD 在同一提交中完成，便于审核来源一致性。

## gRPC

- 每个 RPC MUST 定义成功、业务失败、依赖失败、超时和远端不可用的行为。
- 客户端调用 SHOULD 设置有限 deadline；无 deadline 的长期调用必须有明确说明。
- callback 或 completion MUST 只完成一次。
- 服务启动 MUST 检查 `BuildAndStart()` 或 `bindAsync` 的结果，端口绑定失败不得报告 ready。
- 连接池 MUST 遵循 [Concurrency.md](Concurrency.md) 的借用、归还和关闭规则。
- 错误码优先使用项目稳定枚举或 gRPC status，不应依赖自然语言文本供程序判断。
- RPC 日志可以包含方法名、对端、deadline 和非敏感标识，不得记录 Token、验证码或密码。

## ChatServer TCP

- 包头中的消息 ID 和 body 长度 MUST 使用明确的网络字节序。
- 读取 MUST 支持半包和连续多包，不得假设一次 read 得到完整消息。
- body 长度 MUST 在分配、复制和解析前限制在协议最大值内。
- 未知消息 ID、非法长度和损坏 payload MUST 安全关闭或返回明确错误，不能越界访问。
- 认证前后允许的消息集合 MUST 明确，未认证连接不得执行需要用户身份的操作。
- 写入 MUST 通过单连接顺序队列串行化，避免多个 `async_write` 交叉数据。
- protobuf 或 JSON payload 解析失败不得继续进入业务 handler。

## HTTP/JSON

- GateServer endpoint MUST 明确方法、路径、请求字段、响应字段和错误码。
- JSON 输入 MUST 校验类型、必填字段、长度和允许字符，不得只判断 key 是否存在。
- HTTP 层负责传输与输入校验，注册、登录和密码规则 SHOULD 由业务层处理。
- 响应 MUST 使用一致的错误 envelope；不得将 C++ 异常文本直接返回客户端。
- 密码和验证码 MUST NOT 出现在 URL、访问日志或错误响应中。

## Redis

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
