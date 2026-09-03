# Phase 3A 实施前合同决策

本文件记录 Phase 3A Windows PR 快速业务测试的 DG-05..DG-08。所有决策均由用户于
2026-08-30 确认为方案 A。修改这些合同必须遵守
`tests/CI-GOVERNANCE.md` D-04，不得通过修改测试预期静默改变既有行为。

## DG-05 — Chat Logic dispatcher 队列合同

状态：**Confirmed**

### 合同

- 队列按接受顺序全局 FIFO；当前仍是单 worker，不引入按 chat 分片或并行处理承诺。
- 队列最多同时接受 `MAX_DEALQUE` 条待处理消息。达到容量后拒绝最新消息，不删除或覆盖已接受消息。
- 投递 Interface 必须返回可观察的 `Accepted`、`Full` 或 `Closed` 结果；调用方和测试不得依赖日志判断是否接受。
- shutdown 停止接受新消息，唤醒等待 worker，并排空所有已接受消息后在有限时间内结束。
- shutdown 和析构幂等；关闭后的投递立即返回 `Closed`。
- 未注册消息 ID 被安全丢弃并记录不含正文、Token 或凭据的诊断；它不得阻断后续已接受消息。

### 验证义务

- 覆盖顺序投递、多 producer 的不丢失/不重复、容量边界、overflow、空闲唤醒、shutdown drain、关闭后投递和未知 ID。
- 所有并发测试使用 latch、condition variable、promise/future 或等价条件同步，不用固定 sleep 证明正确性。
- 临时恢复 `size() > MAX_DEALQUE`、关闭时直接丢队列或关闭后仍接受消息时，focused 测试必须 RED。

## DG-06 — Status ChatServer 选择合同

状态：**Confirmed**

### 合同

- 从启动时已验证的 ChatServer 列表中选择当前有效连接数最少的服务器。
- 非负整数连接数视为有效；缺失、空、负数或非数字计数视为未知，并排在所有有效计数之后。
- 有效连接数相同，或全部计数未知时，按服务器运行时 `Name` 的字典序稳定选择。
- 空服务器列表必须失败，不得解引用 `unordered_map::begin()`，不得返回空 host/port 的成功响应。
- 选择逻辑不得依赖 `unordered_map` 枚举顺序。

### 验证义务

- 覆盖空列表、单服务器、最小连接数、并列、部分未知、全部未知、负数和畸形计数。
- Phase 3A 使用 in-memory Adapter，不访问真实 Redis；真实计数读取、断线和 TTL 属于 Phase 3C。
- 临时恢复 `_servers.begin()->second` 直接选择时，focused 测试必须 RED。

## DG-07 — Status Token 持久化失败策略

状态：**Confirmed**

### 合同

- `GetChatServer` 只有在 Token 已成功写入持久化 Adapter 后才可返回成功 endpoint 和 Token。
- Token 写入失败时 fail closed：公开响应使用现有稳定的 `ErrorCodes::RPCFailed`，并清空 Token、host 和 port。
- Phase 3A 不新增公开错误码，不改变 proto 字段或 RPC 全名。
- Token 校验继续区分 UID 不存在、Token 不匹配和成功；依赖异常不得回显内部文本。
- 日志和测试报告不得包含 Token 值。

### 验证义务

- 覆盖写入成功、写入 false、Adapter 抛出异常、UID 不存在、Token 不匹配和成功校验。
- fake Token store 与生产 Redis Adapter 实现同一内部 port；测试不提供 test-only public helper。
- 临时忽略写入返回值并继续返回成功时，focused 测试必须 RED。

## DG-08 — Chat session 身份与替换合同

状态：**Confirmed**

### 合同

- 同一 UID 同时只有一个活动 session；新认证 session 原子替换旧映射。
- 旧连接随后关闭或超时时，只能删除与自身 session ID 匹配的映射，不得删除已经替换进去的新 session。
- session 注册、替换、条件删除和关闭均幂等；并发关闭最多执行一次对外清理。
- 每个 session 的发送队列保持 FIFO，最多接受 `MAX_SENDQUE` 条；满时拒绝最新发送，不覆盖已排队帧。
- session 关闭后拒绝新发送；写失败触发一次关闭和一次身份匹配清理。
- Phase 3A 只证明内存 Module 和 in-process Adapter；真实 TCP partial write、对端断线和进程级清理属于 Phase 3B。

### 验证义务

- 覆盖注册、替换、旧关闭不删新映射、当前关闭删除、重复关闭、并发关闭、发送 FIFO、容量边界和关闭后发送。
- 测试通过真实生产 Interface，禁止直接访问私有容器或增加 `clearForTest`。
- 临时将条件删除改为按 UID 无条件删除时，focused 测试必须 RED。
