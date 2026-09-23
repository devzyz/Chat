# 私聊消息状态控制

覆盖文本、图片、视频和文件消息，延续单账号单在线会话与 UUID 幂等合同。
群聊、撤回、编辑、多设备收件不在范围内。消息存储与增量正文同步见 [MessageStorage](MessageStorage.md)。

## 状态与证据

| 展示 | 证据 |
| --- | --- |
| 待发送 Queued | 原始请求与消息已在账号 SQLite 同事务提交 |
| 发送中 Sending | 当前 attempt 已进入已认证连接的发送调度 |
| 待核实 Uncertain | 断线、ACK 超时或无法确定是否提交；不能推断失败 |
| 发送失败 Failed | 本地保存失败，或首次 attempt 得到明确永久拒绝 |
| 已发送 Sent | 成功 ACK 或权威消息同步确认服务端提交 |
| 已送达 Delivered | 接收者 SQLite 已保存消息，并由服务端提交送达回执 |
| 已读 Read | 接收者满足可见性规则，阅读意图落盘且服务端确认 |

Read > Delivered > Sent，迟到 ACK、失败、超时和较弱回执不能降级这些事实。
没有回执表示尚未确认，不能标为“对方未读”。服务器 ID 和旧 `STATUS_READ_ALREADY` 均不构成阅读证据。
资源送达表示描述符落盘，已读表示卡片呈现，不表示下载、打开或播放成功。
图标分别为 ○、…、✓、灰色 ✓✓、蓝色 ✓✓、?、!；tooltip 与可访问文本给出状态含义。

## 所有权与重试

`UserMgr` 持有账号级 `MessageService`，其单工作线程拥有 `LocalMessageStore`。
发送队列、attempt、回执、游标在 SQLite 保存；TcpMgr 仅负责传输及兼容 ACK 通知，已移除业务重试队列。
模型消费持久化快照，兼容 ChatInfo 中的 ACK 也不再写成已读。
`MessageStateReducer` 定义发送尝试与确认事实转换，SQLite 事务负责身份、批次和持久化约束，模型合并保持最高已确认事实。

- 请求先落盘再发送；保存原始批次、UUID、收件人、内容和资源 ID，重试不从气泡文字重建。
- 业务请求保持不变；每次发送附加递增的十进制字符串 `attempt_id`，1017 原样回传。
  旧 attempt 拒绝不影响当前尝试，迟到成功仍可确认同一 UUID/服务器 ID。成功 ACK 必须完整匹配批次。
- ACK 等待 15 秒，最多初次加 3 次自动重试，间隔 1/3/10 秒。预算耗尽保持待核实，双击可手动恢复。
  永久拒绝停止自动重试；已有歧义的后续拒绝保持待核实。
- 断线/崩溃遗留 InFlight 恢复为 Uncertain。认证后先同步，完成或 15 秒核实窗口结束后按原 UUID 重试。
  周期同步不重置发送预算；显式登出或被踢暂停意图，再登录不自动重发，用户可手动恢复。
- ACK 与清理已提交 outbox 同事务。正文同步也可清理已提交批次，不推进未收到正文的游标。
- 首次本地写入失败不发网络请求；服务内保留原始草稿，双击重试会重新尝试打开/写入本地存储。
  尚未成功落盘的草稿只在当前会话内可恢复，退出或崩溃不能承诺保存。
- generation 隔离账号；切换时排空已接受写入并关闭旧连接，丢弃旧回调。
  工作线程待完成普通操作最多 256，发送每轮最多 8 个批次。

## 已读观测

`MessageReadTracker` 隶属消息列表，每 50ms 采样真实 delegate 气泡几何。
只有当前前台活动窗口、非最小化、无活动模态窗口、当前会话内已落盘的接收消息参与观测。
气泡可见高度至少为 `min(气泡高度, viewport 高度)` 的 50%，连续达到 500ms 才产生该消息 ID 的阅读意图。
隐藏、失焦、窗口状态或会话切换清空累计；跳过的历史 ID 不会因最大 ID 水位而误读。
后台同步、插入模型和收到通知只产生 Delivered。
观测结果先进入持久化 receipt outbox；服务端确认后才刷新已读事实。

## 回执与同步

1005/1006 协商 `capabilities: ["message_receipts_v1"]`。
新客户端连旧服务器不启用回执；未声明能力的旧客户端不会收到新通知。
未升级的接收客户端最多让发送者看到 Sent，不能根据其在线情况推断阅读。

| TCP ID | 内容 |
| --- | --- |
| 1029 / 1030 | 回执批量上报 / 响应 |
| 1031 | 会话回执变化提示，只触发查询 |
| 1032 / 1033 | 按独立 revision 补拉 / 响应 |

上报字段为 `version=1`、`chat_id`、非空 `request_id`（最长 64 字节）和 1～8 个
`items: [{message_id, level:"delivered"或"read"}]`。重复 ID、非法等级或身份整批拒绝。
成功响应返回 `error=0`、相同版本/会话/请求 ID、`latest_revision` 和对应权威 items。
item 含 `message_id`、`recipient_uid`、`level`、`revision`、毫秒 `delivered_at` 与可空 `read_at`。

查询请求使用相同信封及 `after_revision`；响应回显游标并附 `items`、`next_revision`、`load_more`。
所有 revision 为规范十进制字符串，范围 0～INT64_MAX，避免 JSON 数值精度损失。
每页最多 16 项且完整帧体不超过 2048 字节；游标只能推进到实际返回项的 revision，空页保持原值。
这些消息保留 2048 字节上限，不借用正文历史 1028 的扩展长度。

当前认证会话是阅读者身份来源，客户端不能代别人上报。
会话成员可查询；上报者必须是该会话每条消息的 recv_id。
错误使用原 numeric `error` 与 `receipt_error`：InvalidRequest、Unauthorized、InvalidMessage、
StorageUnavailable、DeadlineExceeded。错误不推进客户端游标。
上报应答丢失可重放；相同等级不分配新 revision，Read 隐含 Delivered 并保持首次时间。

MySQL `private_chat_receipt_clock` 分配会话 revision，`chat_message_receipt` 只保存当前最高等级。
写入和分页均锁同一 `private_chat` 行，避免跳过未提交的小 revision；旧消息升级到 Read 时获得新 revision。
回执与正文使用独立游标，回执可早于正文/发送 ACK 保存，关联身份后投影。
本地回执页和游标同事务提交；Delivered ACK 不能清除已排队的 Read。

`NotifyMessageReceiptChanged` gRPC 及本地 1031 仅作提示，提示丢失由每 30 秒补拉恢复。
客户端每类最多 8 个并发回执会话请求，溢出公平排队；请求超时 15 秒。
临时上报失败按 1/3/10 秒退避，随后等待周期调度；永久错误分解为单条并隔离，避免堵塞其他消息。

## 升级与验证边界

MySQL migration 004 新增两张回执表；Gate、Chat、Resource 使用相同更新后的 schema 校验合同。
停止写入、备份数据库、执行 [迁移入口](Data.md#operational-entry)、部署全部相关服务后再更新客户端。
旧二进制会拒绝新 schema，不能混跑；DDL 失败按迁移恢复合同处理，不自动回滚/删除业务数据。

SQLite schema 1→2 先 `VACUUM INTO messages.schema1.sqlite` 留下快照，再事务创建 outbox/回执/回执游标表。
若已有同名备份，另建带 UUID 后缀的全新快照，保留原文件，避免误用上次失败留下的文件。
保留旧正文与 cursor，不从旧状态生成 Read。已有收件正文可以补报 Delivered。
旧未确认记录不猜测原批次自动重发；用户显式重试时复用本地原始内容和 UUID。

测试入口与证据范围见 [Qt 存储](../chat/tests/message-storage/README.md)、
[Qt 模型](../chat/tests/message-model/README.md)、[会话重置](../chat/tests/session-reset/README.md)、
[回执单元](../tests/server/message-receipt/README.md)、[真实 MySQL/TCP](../tests/server/message-sync/README.md)。
真实控件测试覆盖显示门槛；探针直接注入阅读观测以验证持久化/网络链路，不声称人工桌面全流程验收。
当前执行结果与后续工作只维护在 [Status](Status.md)。
