<!-- generated-by: gsd-doc-writer -->
# 并发与异步规范

## 基本原则

- 并发设计 MUST 先明确状态所有者、执行线程、同步方式和关闭顺序。
- 线程安全不能只依赖注释；必须由 strand、mutex、atomic、队列或不可变数据结构实际保证。
- 不允许用固定 `sleep` 证明正确性。等待必须基于条件、事件、future、端口状态或有硬超时的轮询。
- 所有可能阻塞的调用 MUST 有退出条件；生产代码中的无限等待必须有明确的进程生命周期理由。

## Boost.Asio

- 每个 socket、acceptor、timer 和 `io_context` MUST 有清晰的拥有者。
- 同一连接上的 callback 若依赖顺序，MUST 使用单一 `io_context` 执行上下文、strand 或显式串行队列。
- 异步 callback MUST 首先处理 `error_code`，并区分正常取消与真实错误。
- callback 捕获 `this` 前 MUST 证明对象存活；共享异步对象优先捕获 `shared_from_this()` 的结果。
- 新异步操作 MUST 定义取消路径，关闭 socket 或 timer 后不得继续访问已释放状态。
- `io_context::run()` 的线程数量和并发假设 MUST 在拥有线程池的类中集中管理。
- `hardware_concurrency()` 返回 0 或 1 时仍 MUST 创建至少一个可用 worker。

## 线程与任务队列

- `std::thread` 创建后必须在所有路径上 `join` 或安全转移所有权；新代码 SHOULD NOT 使用 `detach()`。
- 不得在持有该线程完成所需的 mutex 时 `join()`。
- 不得对当前线程调用 `join()`，不得对同一线程二次 `join()`。
- 条件变量等待 MUST 使用谓词，并在关闭状态变化时 `notify_all()`。
- 队列容量、满载策略、关闭时剩余消息的处理 MUST 明确：拒绝、丢弃、排空或持久化只能选择并记录一种。
- 多生产者输入必须保证每个逻辑消息最多处理一次；需要顺序时必须定义是全局 FIFO、会话 FIFO 还是 chat FIFO。
- `std::atomic` 只用于独立原子状态；多个字段的不变量不能靠分别 atomic 自动成立。

## 锁

- 锁的作用域 SHOULD 尽可能小，不得在持锁状态下执行网络、磁盘、Redis、MySQL、gRPC 或用户 callback。
- 多把锁同时存在时 MUST 有固定加锁顺序，或使用 `std::scoped_lock`。
- 手工 `lock()`/`unlock()` SHOULD 替换为 `std::lock_guard`、`std::unique_lock` 或 `std::scoped_lock`。
- 递归锁不是修复设计循环的默认手段。
- 分布式锁 MUST 携带所有者标识、有限租约和有限获取超时；只有所有者可以释放。

## Session 与网络写入

- Session 的关闭 MUST 幂等；多条错误路径同时触发时只能执行一次资源释放和状态注销。
- 写队列 MUST 保证前一次 `async_write` 完成后才启动下一次完整写入。
- 发送队列达到上限时 MUST 返回明确结果，不得越界或无限增长。
- 读 header 后 MUST 校验消息 ID、body 长度和最大允许长度，再创建或填充 body buffer。
- 认证前允许的消息集合 MUST 明确；未认证连接关闭时必须清理已创建的临时状态。
- 心跳、timer 和 socket callback 之间的关闭竞争必须有测试覆盖。

## 连接池

- Redis、MySQL 和 gRPC 连接池 MUST 明确容量、借用、归还、耗尽、关闭和依赖失效行为。
- 借用连接的等待 MUST 可被关闭唤醒，并 SHOULD 有有限超时。
- 归还无效连接时 MUST 丢弃或重建，不能重新放回健康队列。
- 池关闭 MUST 幂等；关闭后新的借用应立即失败。
- 健康检查线程 MUST 可停止、可 join，禁止成为阻止进程退出的孤儿线程。

## 服务启动与关闭

推荐启动顺序：

1. 加载并校验配置。
2. 初始化日志。
3. 创建本地资源并完成 TCP/gRPC 端口绑定。
4. 初始化依赖客户端或连接池。
5. 在 Redis 等共享系统登记实例状态。
6. 启动后台等待线程和业务处理。
7. 进入事件循环。

推荐关闭顺序与依赖方向相反：停止接收新请求、取消 timer、关闭会话和队列、停止 RPC、join 线程、注销共享状态、关闭连接池和日志。

- 任一步启动失败 MUST 回滚本次已创建的资源。
- 在创建 `std::thread` 后抛异常时，catch 路径 MUST 先判断 `joinable()` 再 join。
- `Stop`、`Close`、`Shutdown` 和析构 MUST 可重复调用且有确定结果。
- 信号 callback 捕获的引用在事件循环结束前 MUST 始终有效。

## 测试要求

- 并发测试 MUST 有硬超时和失败诊断，包含必要的线程、队列、PID、端口或实例信息。
- 至少覆盖正常执行、重复关闭、关闭时等待、依赖失败和资源回收。
- 故障测试不得依赖概率竞争；使用 barrier、condition variable、promise/future 或可控 fake 建立确定时序。
- 压力和长时间竞态测试放入手动或 nightly 集合，不得用它们替代快速确定性的 PR 测试。
