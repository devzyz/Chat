# 项目当前状态

**已合并基线**　2026-11-25 核对远端 `develop` 为 `6426065`。PR #49 已合入 PostgreSQL 默认后端，已有 MySQL 配置继续受支持。发行版本号见 [VERSION](../VERSION)。[合并提交](https://github.com/devzyz/Chat/commit/642606536b51fa12f5733de23da9919457377120) · [数据库接入](Data.md)

**当前工作**　优先完成 ChatServer 多线程框架改造。入口的启动、续租和停机整理仍在工作区进行。旧登录覆盖有效会话、慢踢线延迟其他账号登录和队列满载漏续租，均已在 `67ae82f` 复现，当前版本待复验。改造后按原验收条件逐项核对。[框架改造后待修清单](plans/ChatServerThreadingFollowups.md)

## 已合并的能力

| 范围 | 已实现与边界 |
| --- | --- |
| 账号与好友 | 注册、登录、重置密码、头像、本人资料、备注、好友申请/接受/拒绝/双向删除/重新添加、退出及切换账号 |
| 私聊与消息 | 文字、图片/视频/文件、UUID 幂等、持久化 outbox、送达/已读、离线补拉和重登恢复；同账号有限自动重连 |
| 群管理 | 建群、增删成员、退出、转让、改名/解散、代次与历史边界、群资源授权；20 人群、2 秒补拉，无群已读回执 |
| 桌面交互 | 本地历史/搜索、未读提醒、最新消息摘要排序、消息复制、会话草稿/附件/光标/撤销、统一退出确认 |
| 存储与部署 | 服务端新开发库默认 PostgreSQL，兼容 MySQL 007；客户端 SQLite 6。升级前备份并显式迁移，服务地址手动配置，单账号单在线会话 |

PR #35 补齐断线保存、心跳超时和资源续传恢复。PR #40 退役独立 StatusServer；Gate 选服并签发凭据，Chat 和 Resource 直接向 Redis 校验。PR #41～#45 改进写入前的凭据复核与并发建聊。每条 TCP 连接固定归属一个业务分片，Redis、数据库与出站 gRPC 分池执行。[服务职责](Architecture.md) · [并发与关闭合同](Concurrency.md)

## 验证与未完成项

- **PR #49 的 Windows quick** 在分支提交 `d2baf75` 上通过静态检查、Server、Qt、Varify 和 `Regression checks`。该次运行跳过 Linux、`Full regression checks`、真实环境验收及发布。Server 作业含临时 PostgreSQL 的迁移与原生存储合同。[Actions 运行记录](https://github.com/devzyz/Chat/actions/runs/43875280247) · [双库测试说明](../tests/server/schema-migration/README.md)
- **PR #49 本机验收**记录了 Windows 服务端六组共 234 项通过。MySQL 和 PostgreSQL 均验证了提交成功但确认丢失后按原 UUID 重试，独立连接只读到一行。Windows 符号链接权限限制了 Linux 性能证据导出测试。[PR 验证记录](https://github.com/devzyz/Chat/pull/49)
- **较早的本机测量**使用 `7ead057` 的正式 Release 程序。三个原生服务完成 Debug 和 Release 构建，80 项服务单元测试通过。两库各按每秒 20 条发送 200 条 256 字节私聊消息，接收端逐条核对，本机 PG 提交 ACK 的 P95 为 2.44 ms。解包后使用包内 PG 迁移器的 6 项冒烟通过。这组测量未运行百万条历史或高并发压测。[当时的状态记录](https://github.com/devzyz/Chat/blob/d2baf75fbc1cc363190e82534c36f6ad0b0d8b4c/docs/Status.md)
- PR #45 时期的 Windows 回归、隔离 MySQL 与 Redis 替身的双实例验证、Qt 写超时首次失败及复跑结果，保留在[阶段验证记录](history/DevelopmentHistory.md#至-pr-45-的阶段验证记录)和[当时的状态页](https://github.com/devzyz/Chat/blob/d2baf75fbc1cc363190e82534c36f6ad0b0d8b4c/docs/Status.md)。
- 局域网上线前还需真实多机、证书和防火墙部署、桌面与休眠恢复验收。已完成资源的引用感知清理及一致备份恢复演练尚未完成。
