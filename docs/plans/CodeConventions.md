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

## N5～N8 核查范围

| 工作包 | 本轮边界 | 验收方式 |
| --- | --- | --- |
| N5 | `chat/` 顶层自有头文件与实现，包含消息模型/存储、资源/头像、认证/会话、UI 及辅助回调；测试子目录单列 N9 | 声明或无声明的定义具有中文 Doxygen；核对借用指针、账号代与线程退出顺序；纯注释 token 对比 |
| N6 JS | Varify 生产适配器/配置/启动及改名涉及的测试函数；GetRedis、QueryRedis、Quit、SendMail 的声明与调用同批迁移 | 外部 `GetVarifyCode` 及配置键保留；真实 JS AST 对比仅有四个显式改名；Varify 所属回归 |
| N6 PS | `scripts/` 所有 PowerShell 函数和脚本块 | 原有 Verb-Noun 保留，补注释帮助及回调说明；除统一入口新增任务外 token 不变；脚本与下载器回归 |
| N7 | 本地/CI 共用 `git_rules.py`；事件 JSON、固定历史祖先、发布与 merge/revert/squash | 独立临时 Git 历史、非法标题的 CLI/PowerShell 退出码、标题编辑事件及工作流语义校验 |
| N8 | `syntax.py`、原生 PowerShell AST 与增量对象选择；本地统一任务、Windows static-check 接入 | 真实解析器正反例、头文件归属及类外成员、具名/匿名回调、default/delete、Qt 宏、未知宏与损坏语法失败 |

工具准备及命令见 [规范测试入口](../../tests/build/conventions/README.md)，
机器支持范围、人工评审责任及固定 Git 基线见 [CI 治理](../../tests/CI-GOVERNANCE.md#12-规范检查合同)。
没有把语法检查扩张为变量命名、语义正确性或全仓无缺项的证明。

## N9 全目录审计与余项归属

审计读取 Git 跟踪文件及本轮未跟踪源码，不读取构建树；精确的路径、行号、对象、原因和解析失败
由 `AuditConventions` 生成到 `build/conventions/audit.json`。下表为本轮目录快照，
“发现数”按诊断计数，同一接口的声明/定义、不同规则可能分别计数；解析失败文件的对象不计入已解析总数。
这是一份历史缺口清单，**不是全仓门禁通过报告**。当前进度和执行证据只见 Status。

| 目录 | 自有文件 | 已解析文件 / 对象 | 发现数 / 解析失败文件 | 余项回挂与边界 |
| --- | ---: | ---: | ---: | --- |
| 仓库根 | 12 | 0 / 0 | 0 / 0 | README、构建/依赖清单等不属于三语言对象；按原构建/文档校验 |
| `.github` | 4 | 0 / 0 | 0 / 0 | N7：actionlint 和 CI 路由合同；YAML 不由 AST 检查器宣称覆盖 |
| `ChatServer` | 66 | 53 / 516 | 335 / 0 | N1：10 个空标签；N2：17 个命名诊断；N4：308 个注释诊断，主要其余 DAO、连接池、业务回调与公共辅助接口 |
| `GateServer` | 41 | 32 / 266 | 221 / 0 | N2：9 个命名诊断；N4：212 个注释诊断，含 HTTP、连接池、RPC 与其余基础接口 |
| `StatusServer` | 35 | 27 / 224 | 215 / 0 | N1：7 个空标签；N2：21 个命名诊断；N4：187 个注释诊断，含连接池及启动/配置接口 |
| `ResourceServer` | 10 | 6 / 86 | 64 / 0 | N4：HTTP 服务编排和局部回调等注释；不把已完成 ResourceStore 推断为整个服务完成 |
| `common` | 8 | 8 / 97 | 97 / 0 | N4：shared 资源目录、连接池、事务和请求编解码接口；需要独立模块合同核对后补说明 |
| `chat` | 224 | 135 / 1565 | 367 / 10 | N3：327 个旧 UI/测试命名诊断；N5：40 个测试对象注释诊断；顶层生产声明/定义/回调的注释诊断为 0；10 个 QTest 主入口宏回挂 N8 |
| `VarifyServer` | 33 | 21 / 279 | 166 / 0 | N6：均为未改变的测试函数、替身和内层回调；生产适配器及本批改名对象已核查 |
| `scripts` | 27 | 15 / 159 | 79 / 0 | N6：仅 `ci/toolchain.js`、`ci/vcpkgBinaryCache.js`、`generate-protocol-fixtures.js`、`protocol-compatibility.js` 的历史注释；PowerShell 无诊断；Python 手工评审和回归 |
| `tests` | 199 | 61 / 1011 | 1014 / 38 | 按语言回挂 N4/N5/N6：1011 个测试/夹具注释、3 个 JS 类命名诊断；38 个宏/调用约定文件回挂 N8，未当成通过 |
| `schema` | 9 | 4 / 36 | 41 / 0 | N6：迁移测试 JS 的 36 个注释、5 个命名诊断；SQL 由迁移合同人工核查，不在本轮执行个人数据库迁移 |
| `proto` | 4 | 0 / 0 | 0 / 0 | 协议源与说明无本轮改动；按 protobuf 生成/兼容合同，不改外部拼写 |
| `cmake` | 2 | 0 / 0 | 0 / 0 | 构建配置无本轮行为改动，由 CMake 校验及评审负责 |
| `triplets` | 3 | 0 / 0 | 0 / 0 | 依赖工具链配置无改动，遵守 DG-25，未恢复 vcpkg |

共记录 2599 条历史诊断、48 个解析失败文件，另有 267 个非支持语言/非代码文件显式列入清单。
48 个失败按原因全部归属 N8：Qt 测试的 `QTEST_*` 入口；Server 的 `TEST`/`TEST_F`/`TEST_P`；
`tests/server/chat-session-state/session_test_support.h` 的未覆盖语法；
`tests/server/process-harness/process_harness_child.cpp` 的 `WINAPI`。
这些文件日后发生变化会阻断增量检查，须先补相应语法支持和回归，不能新增静默排除。

N9 的收口是核清目录并把全部发现回挂原工作包；上述历史欠账尚未逐个整改。
旧 UI 名称包含自动槽/字符串连接约束，后续 N3 必须连同绑定、调用、元对象引用及 Qt 回归处理，
不能仅改字面量。N4/shared 和历史测试同样按所属模块拆批，避免用批量模板注释代替契约核对。
`GetVarifyCode`、现有配置键、schema/协议字段属外部合同保留，不作为内部批次擅自改名。
`docs/` 按文档链接和事实检查；`generated/`、`chat/packages/`、`node_modules/`、`tests/auto/`、
构建产物和用户资料明确排除且未修改，不以排除数量抵消欠账。
