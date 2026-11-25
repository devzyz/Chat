# ChatServer 多线程改造后待修问题

2026-11-12 在 `perf/chat/isolate-dependency-pools` 的 `67ae82f` 上复核。以下三项均已复现，留待多线程框架改造完成后逐项修复和回归。当前工作重点见[项目状态](../Status.md)。

三项的证据范围均为旧基线已复现、当前版本待复验。下列日志名指当时的本机产物，各项验收条件用于对照下一次运行。

### P1 · 已断开的登录仍会覆盖同账号的有效会话

两个实例使用同一份仍有效的 Token。让第一实例的登录等待发布在线状态，再让第二实例完成同账号登录并通过心跳。断开第一条连接，等服务端确认断开后再放行其绑定任务。旧任务仍执行 `Publish`，覆盖第二实例的在线记录。本地绑定随后失败，清理逻辑删除在线记录并向第二实例踢线。实测第二条连接收到 EOF。该竞态在分池前已存在。[绑定发布、回滚和踢线逻辑](../../ChatServer/ChatServer/SessionLifecycleCoordinator.cpp)

- [ ] **验收**：重放上述顺序。放行旧任务后，第二实例的在线记录仍指向原会话，该连接仍能完成心跳。复现使用双 ChatServer 和 Redis 协议替身，本机日志为 `build/pool-spec-stale-bind-probe.log`。

### P2 · 慢速跨服踢线阻塞其他账号登录

让账号 A 跨实例重登，并阻塞发往旧实例的踢线 RPC。出站调用已交给 gRPC 池，但唯一的生命周期线程仍同步等待其结果。实测账号 B 的登录查询已解除 MySQL 表锁，既有连接也能回复心跳，B 却在 10.09 秒后仍未发布在线状态或收到登录回包。释放踢线 RPC 后，B 随即登录成功。探针将 RPC 期限设为 30 秒，客户端重连认证期限为 10 秒。[生命周期协调器](../../ChatServer/ChatServer/SessionLifecycleCoordinator.cpp) · [认证期限合同](../Protocol.md)

- [ ] **验收**：保持 A 的踢线 RPC 阻塞，验证 B 能在客户端认证期限内完成绑定并收到成功的 1006 回包。放行 RPC 后，核对踢线针对 A 的旧会话。本机日志为 `build/pool-spec-kick-probe.log`。

### P2 · Redis 队列瞬时满载会漏掉一次实例续租

占用两个 Redis 工作者并填满 64 个等待位置，触发一次实例续租，然后立即释放工作者。探针观察到队列排空后续租执行次数仍为 0，直到模拟下一次定时触发才变为 1。代码每 60 秒续租一次，租约 TTL 为 90 秒。据此推算，漏掉一轮可使健康实例约 30 秒无法被 Gate 选中。探针实测了漏投，空窗时长由定时配置推算。[续租准入](../../ChatServer/ChatServer/InstanceLease.h) · [续租定时器](../../ChatServer/ChatServer/ChatServer.cpp) · [Gate 选服](../../GateServer/GateServer/GateRequestProduction.cpp)

- [ ] **验收**：在一次续租触发时短暂填满队列，随后恢复容量。验证实例在原租约到期前成功续租，Gate 在此期间仍能选中它。本机日志为 `build/dependency-pool-review-lease.log`。
