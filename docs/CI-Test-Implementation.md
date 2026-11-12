# CI 自动测试如何实现

2026-11-12 按现有工作流、测试注册和代表性源码核对。下表描述配置和实现。Windows 的 377 条是 13 份报告中要求的 testcase 数，其中 C++ 服务端 228、Qt 79、Varify 57、实例脚本 13。一个 testcase 可以执行多个断言；某个源文件也可能同时包含常规和专项用例。[注册与报告校验](../scripts/windows-local.ps1) · [执行范围清单](CI-Test-Inventory.md)

## 从 CI 到断言

| 范围 | 实际调用链 | 具体怎么判定 |
| --- | --- | --- |
| Gate | Windows → RunServerTests → MSBuild → GoogleTest 组件程序 → XML | 测试调用生产请求处理，注入可记录调用的验证码、账户等适配器。两次密码不一致时，既检查错误结果，也检查依赖调用记录为空。[Gate 用例](../tests/server/gate-request/gate_request_component_tests.cpp) |
| Gate 选服与签发 | RunServerTests → 单元及组件测试 | 验证租约计数选择、并列排序、存储错误和凭据发布。[路由单测](../tests/server/gate-request/gate_routing_unit_tests.cpp) · [组件用例](../tests/server/gate-request/gate_routing_component_tests.cpp) |
| Chat | 同一运行器 → 单元、组件、集成以及独立 gRPC 客户端测试程序 | C++ 直接测会话状态与协议；集成用例走运行中的连接。Linux 专项另用 C++ 驱动调用生产消息提交适配器，在真实 MySQL 下核对结果，部分断言用自定义 Require。[会话测试](../tests/server/chat-session-state/chat_session_state_tests.cpp) · [TCP 测试](../tests/server/integration-host/chat_tcp_transport_tests.cpp) · [消息提交驱动](../tests/server/message-commit/message_commit_integration.cpp) |
| Resource | Windows → ResourceTests → 过滤 StoreTest.* | GoogleTest fixture 建独立临时目录，直接调用生产 ResourceStore，验证续传、越权、摘要和重启。常规报告收 9 条。独立资源专项还用 Python 发真实 HTTP、断开连接、比对下载字节和解码视频。[C++ 存储测试](../tests/server/resource/resource_tests.cpp) · [专项及边界](../tests/server/resource/README.md) |
| Qt | RunClientTests → CMake/CTest 按层级选用例 → C++ 测试程序 → CTest JUnit | 头像用 Qt Test 的 QCOMPARE 比较像素、用 QSignalSpy 观察模型通知；CMake 可把同一程序的不同方法注册成独立用例。帧解码仍采用自定义 main/expect，一次进程运行内检查多个分片条件。[头像测试](../chat/tests/local-avatar/local_avatar_tests.cpp) · [帧解码](../chat/tests/network-state/tcp_frame_decoder_tests.cpp) · [注册](../chat/CMakeLists.txt) |
| Varify | RunVarifyTests → node --test → JUnit reporter | JS 导入生产 handler，注入内存缓存和邮件收集器，检查注册与重置的验证码、发送次数和错误结果；另有独立子进程和真实回环 gRPC 用例。[handler 用例](../VarifyServer/test/handler/handler.test.js) · [gRPC 用例](../VarifyServer/test/rpc/rpc-routing.test.js) |
| 真实依赖与双服 E2E | Linux workflow → Bash 入口 → Node 协调器/场景 → C++ 服务、协议驱动和 Qt 客户端 → JUnit 与清理报告 | C++ 驱动复用生产帧编解码器；JS 场景负责一部分业务断言。例如丢确认后，JS 观察断线、待确认消息和已提交记录，重登重发后再次查询 MySQL，要求同 UUID 只有一行。[原生驱动](../tests/services/FourProcessDriver.cpp) · [恢复断言](../tests/services/faultRecoveryCases.js) · [四账号断言](../tests/services/firstReleaseCases.js) |
| 性能 | 性能工作流 → Node 负载客户端、场景和指标收集器 → 真实服务 | 按消息身份匹配提交和接收，区分确认、投递和重复通知，分别计算延迟与吞吐；业务正确性与采样完整性有独立断言。[负载场景](../tests/services/performance/scenarios.js) · [指标计算](../tests/services/performance/metrics.js) |
| 脚本与 CI 自检 | PowerShell 测试调用实例管理脚本；Node 测策略；Windows workflow 也有内联配置检查 | 用临时目录与子进程检查启动失败、状态和清理，自行写 JUnit。策略单测检查触发选择。YAML 中仍有项目 XML 和依赖清单等构建检查。[实例脚本用例](../tests/scripts/lifecycle/chatserver-instances.tests.ps1) · [策略测试](../tests/build/ciPolicy.test.js) · [Windows 工作流](../.github/workflows/windows-ci.yml) |
| 发布与部署 | Python unittest 验证打包工具；发布工作流另在新 runner 调用成品 smoke | 打包用例检查 ZIP 内容、配置脱敏等；成品 smoke 启动包中程序并验证实际表现。当前发布工作流结束于 GitHub Release，目标环境部署仍是规划项。[打包用例](../tests/release/contracts/test_release.py) · [冒烟脚本](../scripts/release/smoke.py) · [发布工作流](../.github/workflows/release.yml) |

## 十类测试的内容、方式与范围

以下是用例归属建议。Gate、Chat、Resource 的进程内业务规则仍由各模块的 C++ GoogleTest 直接验证。例如，Gate 注册时两次密码不同，应在调用账户存储之前返回错误。下面十类补充控件、协议、外部依赖、跨进程和交付等观察点。每条用例按决定它通过或失败的主要断言归类；实际接入 CI 的项目见[测试清单](CI-Test-Inventory.md)。

| 类别 | 具体测试项 | 测试方式 | 范围限定 |
| --- | --- | --- | --- |
| 1. Qt 控件与交互 | 输入与取消、按钮或信号触发次数、模型行变更、列表分页、草稿保留、头像刷新 | 用 C++ Qt Test 创建真实控件或模型，用 QTest 产生输入，用 QSignalSpy 记录信号并比较参数；异步更新等到指定条件或超时 | 观察一个客户端进程内的控件、信号和模型。接收方实际收到消息归第 5 类 |
| 2. Node 验证码服务 | 注册和重置的用途隔离、同邮箱并发去重、冷却与消费后重发、非法输入、下游超时的响应 | 用 node:test 调用生产 handler，注入可控制的缓存、邮件和时钟等边界（依用例需要），断言响应、缓存键和发送次数；检验 gRPC 路由时启动本机服务发请求 | 关注 Varify 对请求作出的决定。真实 Redis 的原子操作与邮件是否到达 Mailpit 归第 4 类 |
| 3. 协议与兼容性 | 帧头、字节序、半帧与粘包、长度上限与非法输入、protobuf 描述及生成文件漂移、跨语言字段含义 | 用独立保存的字节样本或描述符作期望，让生产编解码器处理，逐字段核对；跨语言合同用 C++ 客户端调用本机 Node gRPC 服务 | 结论限于列出的格式、样本或版本。当前版本互通不能证明上一发行版与当前版本可完整互操作 |
| 4. 真实依赖与存储集成 | MySQL 迁移、消息幂等和事务，Redis 原子写与 TTL，SMTP 投递，临时文件上的续传、归属和摘要 | 在用例所属的临时库、Redis、Mailpit 或目录中调用生产适配器，以独立查询、实际收件、重新打开文件或重建对象核对结果 | 验证应用与一种依赖之间的承诺。账号从取码到登录再跨服聊天的整段旅程归第 5 类 |
| 5. 多进程业务与故障恢复 | 取码注册、登录选服、跨服好友与消息、离线历史、丢确认重试、重启恢复；手动首版验收另测群与资源 | Node 场景启动正式进程并控制故障，C++ 或 Qt 驱动走正式协议；按账号、UUID 和服务端 ID 关联发送方状态、接收方结果及必要的数据记录 | 验证一个具名用户场景在多个组件间完成。每条场景标明真实服务和替身。运行时长本身不作为性能基线 |
| 6. 性能与压力 | 登录、同服及跨服私聊、群补拉、离线恢复、资源传输和混合负载下的吞吐、延迟、错误、丢失与资源占用 | Node 负载端运行 smoke、baseline 或 stress，分开准备、预热、采样和排空，记录目标速率、实际完成量以及压测端调度延迟 | 数值仅对应该档位、数据量和 runner。当前速度指标用于记录；超时、结果错误或采样缺失可使运行失败 |
| 7. 静态检查与 CI 工具自测 | 工作流语法、源码规范、项目/依赖配置、测试注册；事件分流、缓存选择、报告缺失与失败传播 | 静态规则直接运行 actionlint 等检查器；CI 策略用表驱动输入事件、路径和预期作业，报告检查器用正常和损坏样本验证结果 | 证明配置及检查机制能识别问题。例如 release 报告缺失归本类，发行包本身缺 DLL 归第 10 类 |
| 8. Windows/Linux 构建 | 正式目标的配置、编译、链接，Windows 独立运行目录，Linux 禁用 Qt 后的纯服务端构建 | 在对应 runner 用正式工具链构建；需要验证运行依赖时，将构建产物移到隔离目录，探测启动、监听或就绪并回收进程 | 证据对应指定平台、工具链、构建选项和构建产物。下载最终 ZIP 后能否运行归第 10 类 |
| 9. 运维脚本与进程管理 | 实例名与端口冲突、配置缺项、启动失败、PID 身份识别、状态查询和停止后的清理 | 用 PowerShell 从公开命令入口运行实例脚本，准备临时配置和受控子进程，核对退出码、实际进程状态、端口和残留 | 被测对象是管理命令。例如脚本遇到冲突能否干净退出归本类；Chat 重启后用户消息能否恢复归第 5 类 |
| 10. 打包与发布 | ZIP 清单、运行库、proto 和迁移文件、配置脱敏、版本和摘要，成品启动与上传回验 | Python unittest 用临时 ZIP 制造缺文件或错摘要；发布流程从同次构建取包，在全新 Windows runner 下载、解压并启动，最后回验发布制品 | 打包规则、成品冒烟、上传回验分别给出结果。覆盖终点为已发布的制品；目标环境部署、配置生效及部署后冒烟保留为独立规划项 |

## 给新增测试选择写法

1. **能直接构造 C++ 被测对象时**，在所属模块已有测试目标中写 GoogleTest。以 Gate 注册失败为例，输入两次不同密码，调用生产入口，断言返回的错误码和后续账户创建调用次数。外部依赖可替换，业务判断使用生产实现。
2. **预期结果属于 Qt 对象时**，使用 Qt Test。以头像刷新为例，给模型放入两个账号的数据，触发一个账号刷新，用 QSignalSpy 捕捉变化并核对受影响的行。异步条件应设置截止时间，失败时能区分“未收到信号”和“信号内容错误”。
3. **预期结果只有多个进程配合才能观察时**，在一个具名场景里用公开协议发起操作，观察接收方、持久化或恢复结果。沿用现有 Node/Python 驱动；可单独表达的 C++ 状态转换补在对应组件，共享生产协议驱动。仅把脚本里的 assert 换成 C++ 调用不会增加覆盖面。
4. **被测对象本身是工具或 Node 服务时**，用对应生态直接测。Python 打包失败应给出错误文件和原因；Node 验证码失败应指出用途、错误码或调用次数。两个例子均可从测试入口单独复现。
5. **接入 CI 时**，沿用所属模块的显式注册、层级、硬超时和清理规则。核对失败能传播到进程退出码和报告；报告中保留场景名与失败观察值。报告计数以注册粒度为准，不按断言个数统计。[仓库质量规范](Quality.md) · [测试入口](../tests/README.md)

## 调整现有实现的优先级

- **优先试点 Qt 帧解码。** 现有 main 在同一进程累积多个条件，而且包括“数量比较后继续按下标取值”。改为 Qt Test 时先断言数量，再比较消息 ID 和正文，使失败直接指向分片、粘包或长度场景。若拆为多个 CTest 项，同步更新显式注册、报告期望和 Test ID 映射。
- **保留清楚的已有实现。** Gate 的 GoogleTest、头像 Qt Test、Varify 的 node:test 可用作各自模块的新用例范本。原生驱动中只有通用错误文本的 Require，可在下次更改该场景时补充检查名称及脱敏的实际值。
- **迁移跨语言场景前先写下它的观测边界。** 例如“丢 ACK 后再次提交仅有一行记录”要保留真实断线、原 UUID、客户端状态和数据库行数。可在 C++ 增补本地状态机覆盖；删除原 E2E 前应有等价的跨进程失败证据。
- **先对照同一失败，再切换报告。** 在受控测试环境复现失败，比较旧、新用例指向的行为，再恢复运行并检查结果。按模块切换；保留同一提交的报告、清理和缺项检查。本文没有运行 CI 或迁移用例。

## 开源仓库怎样写一条测试

| 项目与样本 | 输入、运行边界和断言 | 对 Chat 的应用建议 |
| --- | --- | --- |
| Qt Test · 数据驱动单元测试 | 在同名 `_data()` 槽声明输入列和期望列，用带名字的行提供全小写、混合大小写、全大写三种数据。测试槽用 `QFETCH` 读取当前行，调用真实函数并用 `QCOMPARE` 比较结果。一处断言即可定位具体失败数据行。[Qt 官方数据驱动示例](https://doc.qt.io/qt-6/qttestlib-tutorial2-example.html) | 帧长度边界、输入校验、消息状态转换适合列为有名字的数据行。应先确认 Qt 行到 CTest/JUnit 条目的映射，再调整现有精确计数。 |
| Qt Test · 控件组件测试 | 新建实际的 `QLineEdit`，通过 `QTest::keyClicks` 发送输入事件，读取 `text()` 断言最终内容。测试程序用 `QTEST_MAIN` 运行测试槽，CMake 将它链接到 Widgets 与 Test。[Qt 官方 GUI 示例](https://doc.qt.io/qt-6/qttestlib-tutorial3-example.html) | 对聊天输入框，可以操作真实控件并检查草稿状态、提交信号或模型结果；需要验证跨账号送达时，再调用双客户端场景。 |
| Bitcoin Core · C++ 规则测试 | `MoneyRangeTest` 直接向生产函数提供负一、零、一、最大值和上界加一；每次用 Boost.Test 比较预期布尔值，整个用例无需启动节点。[金额边界用例源码](https://github.com/bitcoin/bitcoin/blob/master/src/test/amount_tests.cpp) | 文件大小、分片偏移、消息长度等已有纯规则，可直接覆盖临界值及其两侧。每个模块的网络用例保留少数代表值，验证调用链确实接入这些规则。 |
| Bitcoin Core · 多节点功能测试 | Python 测试声明启动三个节点。脚本通过 P2P 投递区块，并通过 RPC 等待同步；第三个节点的测试对端请求数据，限时等待收齐预期区块，再逐个断言每个区块仅收到一次。[功能测试示例源码](https://github.com/bitcoin/bitcoin/blob/master/test/functional/example_test.py) | 双 ChatServer 场景可从客户端 A 提交带 UUID 的消息，客户端 B 等待指定消息，读回历史后比较稳定服务端 ID，并核对重复提交后的记录数。将等待条件、超时与失败诊断放在调用相应动作的步骤旁。 |
| LLVM · 回归夹具 | LLVM 用 GoogleTest 测支持库，使用 lit 驱动行为回归。InstCombine 用例把 LLVM IR 输入、执行实际 `opt` 的命令和 FileCheck 期望放在同一文件，核对特定函数变换后的指令。官方指南建议将缺陷缩减为足以触发它的样本。[LLVM 测试指南](https://llvm.org/docs/TestingGuide.html#regression-tests) · [InstCombine 用例源码](https://github.com/llvm/llvm-project/blob/main/llvm/test/Transforms/InstCombine/add.ll) | 协议兼容和分页游标回归可保存一个最小输入与预期字段，由已有测试调用正式解析器，断言可观察结果。测试命名可直接说明哪一个旧版本输入或边界行为受到保护。 |
