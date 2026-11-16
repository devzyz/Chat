# 服务端性能基线

所属：Business / E2E（真实服务压测）；框架合同为 Unit 及 Integration（loopback TCP）。
GitHub CI 手动选择 `mode=performance`，`performance_profile=smoke|baseline|stress|large-history|comprehensive`，默认 baseline。
固定负载以 [profiles.json](profiles.json) 为唯一来源。仅手动运行，不加入 required checks。

此手动工作流保留历史 MySQL 性能基线。正式 Gate、双 Chat、Resource、Varify 在 Ubuntu 同机运行，使用临时 MySQL、Redis 和 Mailpit。PG 与 MySQL 的同负载小样本见[双库测量入口](../../server/schema-migration/README.md#postgresql-与双库合同)。
每轮独立账号通过公开验证码、注册、登录、好友与群接口准备。固定种子 chat-performance-v1。
测量登录、同服/跨服 256 字节文本、二十人群两秒补拉、离线一百条恢复、PNG 和 1/10 MiB 附件及混合负载。
准备和预热不进入正式样本；离线积压准备在恢复动作计时外。吞吐按实际完成及排空时间计算。
登录测量结束后，按实际 TCP 端点核对连接分布。若实例人数不等，关闭较忙实例超出一半的连接，等待 Redis 中两个实例的连接数均已更新，再经 Gate 重新登录。两边人数相等才建立同服和跨服配对。
开环定速负载具有在途上限，记录调度滞后、未发送数量、各服务 CPU ticks/RSS 和压测端事件循环延迟。

输出 metrics.json、performance.xml、测试.md，工件 performance-results 保留三十天。
缺场景、缺样本、错误 SHA、业务错误、超时、丢失、重复落库、内容不符、清理失败均拒绝通过。
速度指标暂不设回归阈值；共享运行器同机基线不代表公网体验、Windows 性能或生产容量。

## 百万条历史消息

在 GitHub Actions 的 CI 页面选择 Run workflow，模式选 `performance`，档位选 `large-history`。
也可对已推送的分支执行 `node scripts/ci/performance.js start --ref <branch> --profile large-history`。
push、PR 和定时任务均不会运行此档位。

40 个账号经 Gate 登录两个 Chat 实例，组成 20 个跨服私聊。仅在本次运行的临时 MySQL 中批量预装
100 万条带唯一 UUID 的 256 字节文本。4 个长会话各 20 万条，其余 16 个各 12,500 条。
20% 的会话容纳 80% 的历史，用于观察索引较深时的读写表现。装载在会话间交错，逐会话清点实存条数。
造数耗时记在 `seed-history` 阶段，不计入客户端吞吐。工作负载在相同数据集上重复三轮，每个场景先预热
5 秒，再以每秒 20 次测量 30 秒。每轮各有 600 个正式样本。

| 场景 | 单次完成条件 |
| --- | --- |
| history-middle | 从会话中段轮换游标，经 TCP 补拉连续 100 条并核对内容、UUID、参与者和游标 |
| history-tail | 从预装尾部的游标补拉最后 100 条，逐条执行相同核对 |
| history-cross-write | 向这些私聊追加新消息，确认发送端 ACK、跨实例接收通知和数据库唯一记录 |

中段与尾部延迟的单位是完成一次 100 条补拉的毫秒数。数据分布、样本、送达或清理不完整都会使 CI 失败。
此档位衡量有历史积累的私聊。批量造数的速度不代表在线消息写入速度。

## 综合高负载

用 `node scripts/ci/performance.js start --ref <branch> --profile comprehensive` 手动触发。
240 个常驻客户端平均分配到两个实例，建立同服私聊、120 个跨服私聊和 12 个 20 人群。
预装 600 万条消息，24 个长会话各有 20 万条，96 个普通会话各有 12,500 条。
每个场景使用同一数据库，独立预热 6 秒、测量 30 秒，重复三轮。造数、准备离线积压、持久化批量核查与清理单独执行。
百分位按一次完整业务动作记录。CPU、RSS 和负载端调度滞后保存在同次运行的 JSON 工件中。

| 场景 | 负载及完成条件 |
| --- | --- |
| registration / login | 每秒 2 次验证码、邮件和注册全流程；每秒 6 次独立账号的 Gate 登录、选服和 TCP 认证 |
| same-server / cross-server | 各每秒 200 条，核对 ACK、接收者通知和持久化唯一性 |
| hot-private | 向 8 个长会话集中发送每秒 300 条，执行与分散私聊相同的核查 |
| history-middle / history-tail | 各每秒 75 次，每次连续读取并校验 100 条 |
| social-directory / receipt-retry | 每秒 60 次对端资料及完整好友目录查询；每秒 30 次同 UUID 重试、送达、已读与发送方权威回执补拉 |
| group / group-management | 每群每秒 1 条，等待其余 19 人按两秒周期实际读到；每秒 4 次建群、改名、成员读取和解散 |
| avatar / resources | 每秒 4 次头像发布并由对端校验；每秒 1.2 次传输，轮转 PNG、1/10 MiB 文件和真实 10 秒 MP4，多分块上传核对续传位置，下载中断后按 Range 恢复并核对散列 |
| mixed | 每秒 200 条跨服文本，同时每秒 1.2 次执行完整资源传输，两个负载各自计数 |
| offline | 每个窗口让 120 个接收者分别断开，给每人真实提交 100 条积压；随后经 Gate 重登并逐条核实恢复的消息 |

正式报告必须包含全部 45 个场景结果与对应预热。任何漏发、少样本、内容错误或缺少接收证据都会失败。
资源与混合窗口逐笔累计实际下载字节，报告另外按计划次数、轮转顺序、PNG、文件及当次 MP4 实际大小核对总字节数和吞吐。
此档位测量服务协议与数据链路，桌面 UI、物理多机、外网及进程故障切换由各自的验收环境覆盖。

每次进程采样须覆盖 Gate、两个独立 Chat、Resource 和 Varify，包含各自的 PID、CPU ticks 和 RSS。
GitHub 性能构建作业在恢复原生依赖之前执行框架验证：

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
