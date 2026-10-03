# 服务端性能基线

所属：Business / E2E（真实服务压测）；框架合同为 Unit 及 Integration（loopback TCP）。
GitHub CI 手动选择 `mode=performance`，`performance_profile=smoke|baseline|stress`，默认 baseline。
固定负载以 [profiles.json](profiles.json) 为唯一来源。仅手动运行，不加入 required checks。

正式 Gate、Status、双 Chat、Resource、Varify 在 Ubuntu 同机运行，复用临时 MySQL、Redis、Mailpit。
每轮独立账号通过公开验证码、注册、登录、好友与群接口准备。固定种子 chat-performance-v1。
测量登录、同服/跨服 256 字节文本、二十人群两秒补拉、离线一百条恢复、PNG 和 1/10 MiB 附件及混合负载。
准备和预热不进入正式样本；离线积压准备在恢复动作计时外。吞吐按实际完成及排空时间计算。
开环定速负载具有在途上限，记录调度滞后、未发送数量、各服务 CPU ticks/RSS 和压测端事件循环延迟。

输出 metrics.json、performance.xml、测试.md，工件 performance-results 保留三十天。
缺场景、缺样本、错误 SHA、业务错误、超时、丢失、重复落库、内容不符、清理失败均拒绝通过。
速度指标暂不设回归阈值；共享运行器同机基线不代表公网体验、Windows 性能或生产容量。

框架验证由 GitHub 性能构建作业执行：

```sh
node --test tests/services/performance/*.test.js tests/services/connectionCount.test.js tests/build/ciPolicy.test.js
```

实际压测只在 GitHub 临时环境执行。run.js 要求同次源码工件、临时容器身份及 SHA。
日常使用 `$chat-performance`，无需本地恢复依赖或运行服务。

每次运行在所有负载轮次之前执行一次正确性回归：Gate 空闲超过 60 秒后的首个请求、255 字中文名称与描述修改后
重新登录及目录完整性、20 条 UUID 批量确认和幂等重试、真实 Redis 的 Token/在线租约 TTL 与验证码消费。
这部分属于准备阶段，不计入性能样本；任何断言失败仍使整次运行失败。Resource 就绪使用 `/ready`。

消息提交失败保留服务器白名单 `commit_error` 分类（权限、UUID、冲突、期限或存储），
未知字段不进入报告；预热失败和未发送请求仍会使整次运行失败，不自动重试消息或放宽负载。
诊断工件包含顶层和一层服务子目录中的日志，最多 20 个文件、每个尾部 64 KiB；跳过符号链接、
配置及更深目录，截断的不完整首行丢弃，敏感协议/SQL 行和本次所有新旧会话凭据脱敏。
