# 命名与注释规范化计划

## 范围与分批原则

此页仅在安排历史治理时读取；规则见 [Standards](../Standards.md#类与函数注释)、
[Native](../Native.md#接口注释)、[Languages](../Languages.md)、[Quality](../Quality.md#git-规范)。
当前批次与验证证据只维护在 [Status](../Status.md)，下表定义工作包及验收，不另建进度账本。

同类型、同验证边界的问题合为一批；重命名的声明、实现、调用、测试和引用必须同批完成。
每批一个逻辑目标/PR；规模过大时按模块拆分，不混入业务改造或全仓格式化。
注释以实现和已确认合同为依据，发现行为缺陷另立修复任务；协议、序列化、数据库字段保持兼容。
排除第三方、生成文件、构建产物及用户维护的 `tests/auto/`。每批先核实实际缺项，下表种子不是全量审计结论。

## 批次计划表

| 批次 | 同批任务与范围 | 依赖 | 验收与验证 |
| --- | --- | --- | --- |
| N0 规范文档 | 建立命名、注释、Git 规则及按需读取入口 | 无 | 规则无冲突、链接有效、diff 干净；仅文档验证 |
| N1 错误/失效注释 | 集中修正发送 ACK/已读混淆、旧参数名、空 Doxygen 标签、重构后孤立注释；优先 TcpMgr 与两端 LogicSystem | N0 | 每处说明与实现一致；本批触及函数及所属类补齐说明，检查 diff，不改行为 |
| N2 内部拼写与语义 | `GetUesr → GetUserByUid`、`backanme → backname`；复核 Receipts 等含糊名称，补全相关契约；调用与测试一并更新 | N1 | 旧名在自有活动代码中无残留，历史记录除外；受影响模块编译与回归通过 |
| N3 Qt 命名统一 | UserMgr、TcpMgr、AuthFlowCoordinator 等按依赖闭包迁移 lowerCamelCase、布尔查询和信号/槽；补类及函数注释 | N2 | 检查 connect、字符串连接、自动槽绑定和元对象引用；客户端编译及相关 Qt 回归通过 |
| N4 Server 契约注释 | 会话/生命周期、消息提交/存储、Gate/Status 路由、Resource 存储按子批治理；同一接口链的类职责、返回值、失败、线程和有效期一起补齐 | N2 | Register 旧会话返回、异步完成和关闭边界等明确；纯注释检查事实/diff，涉及改名则编译并运行所属回归 |
| N5 Qt 其余注释 | 消息模型/持久化/资源、认证/会话、其余 UI 按子批补齐；重点 recordAt 指针有效期、账号切换与回调失效 | N3 | 已声明批次范围内所有自有类/函数均有有效说明；纯注释不强制全量测试 |
| N6 Node 与脚本 | JS 适配器命名、JSDoc；PowerShell Verb-Noun 与注释帮助，按语言分两个子批；GetRedis/QueryRedis/Quit 连同调用处理 | N0 | 外部 gRPC 名称保留；运行 Varify 或脚本所属回归，确认异常与关闭说明准确 |
| N7 Git 自动门禁 | 分支名、PR 标题、commit 校验复用同一规则实现；覆盖 merge/revert/squash、标题编辑、发布 PR 和历史基线 | N0 | 合法/非法样例及事件范围测试通过；本地入口与 CI 汇总真实接入，命令同步测试入口 |
| N8 注释与命名增量检查 | 按 C++/Qt、JS、PowerShell 子批接入语法感知检查；配合评审验证语义，不用正则宣称全覆盖 | N7 | 覆盖重载、宏、Lambda、default/delete、信号/槽、未触及旧代码及解析失败；公布支持边界 |
| N9 余项收口 | 按目录清单审计 shared、其余 Server/客户端、测试与脚本；将漏项按类型并回相应工作包 | N3～N6、N8 | 全部自有适用目录有核查记录，无未说明缺项；按实际代码变更范围运行回归，证据写 Status |

建议顺序：N0 → N1 → N2 → N3/N4/N5；N6、N7 可独立安排，N8 在 N7 后，最后 N9。
“可独立”只表示依赖关系，不要求并行代理或增加重复报告。
`Varify` 等外部协议拼写迁移不属于内部规范化批次；需要独立兼容方案与用户任务后再实施。

## N4 接口链与核查边界

| 子批 | 权威声明与实现核查范围 | 重点契约 |
| --- | --- | --- |
| 会话与生命周期 | ChatServer 的 `CServer`、`CSession`、`SessionLifecycleCoordinator`、`UserSessionDirectory`、`SessionTypes`、`UserPresenceStore`、`RedisUserPresenceStore`、`LogicDispatcher` | 弱引用和快照有效期、旧会话返回、入队与送达区别、绑定回调线程、停止与存储排空的区别 |
| 文本提交与存储 | `MessageCommit`、`MySqlMessageCommitAdapter`；`MysqlDao`/`MysqlMgr` 的批次提交、历史分页及增量同步接口 | 认证身份、UUID 幂等冲突、批次事务、连接借用、期限与未知提交结果、输出参数的失败边界 |
| Gate/Status 路由 | `GateRequest`、`GateRequestInternal`、`GateRequestProduction`、`StatusRouting`、`StatusRoutingInternal`、`StatusRoutingProduction` 及对应实现 | 同步端口、依赖持有、错误映射、选服排序、Token 写入和校验 |
| Resource 存储 | `ResourceStore` 及 `Error`、`Metadata` | 单执行器调用、字节偏移、部分写失败、摘要与媒体校验、下载授权由上层负责 |

该边界不代表整个 Server 目录已无历史缺项。HTTP 传输、其他 DAO/连接池、共享资源目录、
其余业务处理及测试注释仍由 N9 按目录审计，并归回相应工作包；不把这些接口的说明推断成全仓覆盖。
纯注释子批对照实现与所属测试合同检查，并比较去除注释后的源码 token；涉及改名或行为变更时，
恢复表中对应的编译和模块回归要求。当前完成情况与实际证据只见 [Status](../Status.md)。
