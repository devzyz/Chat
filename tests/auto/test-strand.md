# Chat 项目自动测试规范

## 1. 文档用途

本文件是本项目生成、审核和执行自动化测试时必须遵守的统一规范。`tests/auto` 保存的是测试约束和测试范围，不是测试实现目录。

测试保护的是外部可观察行为、边界条件和已经确认的架构契约，不锁死私有函数、容器类型或线程实现。重构后只要契约不变，原测试应继续通过。

设计测试前必须以当前仓库为准，至少检查：

- `Chat.sln`、`Directory.Build.props`、`Directory.Build.targets`
- `scripts/windows-local.ps1`、`scripts/chatserver-instances.ps1`
- `.github/workflows/windows-ci.yml`
- GateServer、StatusServer、ChatServer、VarifyServer 和 `chat` 客户端的实际源码
- `ChatServer/ChatServer/configs/chat-01.ini` 与 `chat-02.ini`

不得根据旧目录、旧文章或历史提交假定当前仍有 ChatServer1/ChatServer2 两套实现。当前契约是：**一个 ChatServer 可执行文件，通过不同配置启动任意 N 个独立进程实例**。

## 2. 当前构建和测试基线

| 单元 | 当前构建方式 | 当前自动检查 | 阶段三测试入口 |
| --- | --- | --- | --- |
| GateServer / StatusServer / ChatServer | Visual Studio MSBuild + vcpkg | Release 构建与 ZIP；ChatServer 模块 + Gate/Status 独立 Asio GoogleTest | `RunServerTests`，JUnit XML 保存到 `build/test-results/server_unit.xml`、`server_gate_asio.xml`、`server_status_asio.xml` |
| Qt Windows 客户端 | CMake + Ninja + Qt 6.5.3 MinGW | `message_model_tests` + CTest | `RunClientTests`，JUnit XML 保存到 `build/test-results/client_unit.xml` |
| VarifyServer | Node.js 22 + `npm ci` | 配置/proto、handler、动态 loopback route、bind/start lifecycle、语法与依赖树 | `RunVarifyTests`，JUnit XML 保存到 `build/test-results/varify_unit.xml` |
| ChatServer 实例管理脚本 | PowerShell | 配置输入、冲突、启动失败和陈旧 PID 防护 | `RunScriptTests`，JUnit XML 保存到 `build/test-results/script_unit.xml`；不联网安装 Pester |

`scripts/windows-local.ps1 -Task TestPhase1` 是上述四类测试的统一入口；CI 为复用各自工具链和依赖缓存，在现有 `static-check`、`servers-release`、`client-release`、`varify-release` job 中调用对应的 `Run*Tests` 子任务。脚本测试没有采用 Pester，是因为 GitHub 干净 runner 不应为纯 CLI 契约用例联网安装模块；轻量运行器仍保证隔离临时目录、逐项诊断和非零失败退出码。

### 测试目录组织

测试源码 MUST 按被测生产模块放入明确的子目录，不能按语言或 runner 把所有源码堆在同一级。当前结构为：

```text
tests/server/{config,startup,messaging,transport,concurrency,lifecycle,protocol,rpc,data}
tests/scripts/{validation,lifecycle}
chat/tests/message-model
VarifyServer/test/{config,protocol,handler,rpc,startup}
```

中央 runner、MSBuild project 或父目录索引可以保留在集合根目录。每个测试模块子目录 MUST 包含
`README.md`，至少说明被测生产代码/契约、用例范围、依赖与隔离、运行命令、CI job/报告和已知缺口。
移动或拆分测试时 MUST 同步 MSBuild/CMake/npm/PowerShell/CI 与本文件引用，不得复制测试维持旧路径，
也不得减少用例或弱化断言。新增测试应进入已有职责匹配的模块；没有匹配模块时，先定义新模块边界及 README。

## 3. 测试分层

- **Unit**：纯逻辑、小范围、快速、无真实网络和外部服务。
- **Component**：同一进程内组合多个真实模块，但通过边界替身隔离 Redis、MySQL、SMTP、gRPC 远端。
- **Integration**：真实进程、协议或真实临时依赖的组合测试。
- **E2E**：从客户端或公开协议入口验证完整用户流程。
- **Stress / Sanitizer**：长时间并发、资源泄漏、竞态和未定义行为检查。

业务目录仍可按以下领域分类，但类型和领域不能混用：

- `foundation`：Config、协议、线程/队列、网络、连接池、Redis、MySQL、gRPC。
- `architecture`：Session、LogicSystem、服务生命周期、ChatServer 多实例和跨实例路由。
- `business`：验证码、注册、登录、好友、聊天和历史消息。

## 4. PR、Integration 和 Nightly 测试集合

### PR 必跑

- 不访问 Redis、MySQL、SMTP 或公网。
- 不使用仓库中的个人开发配置和密码。
- Server Unit、Qt CTest、VarifyServer Unit、PowerShell Unit。
- 必须确定性完成，单个测试通常不超过 10 秒。

### Integration

- 可启动 loopback 监听和临时子进程。
- Redis/MySQL 必须由测试环境临时创建并使用专用数据空间。
- Windows CI 没有准备外部服务前，不把这类测试伪装成 PR Unit。
- 未来 Linux/Docker 环境完成后，再将真实 Redis/MySQL 集成测试纳入独立 job。

### Nightly / 手动

- 高强度并发、长时间重连、压力和 Sanitizer。
- 不得拖慢当前 Windows Release 构建和发布包 job。

## 5. 用例设计矩阵

每个被测契约必须逐项判断是否需要覆盖：

1. **Normal**：正常输入和正常生命周期。
2. **Boundary**：空值、最小/最大值、队列和长度边界。
3. **Invalid**：畸形参数、错误配置、非法协议和错误状态。
4. **Concurrency**：竞争、顺序、重复调用和 shutdown 期间的访问。
5. **Failure / Recovery**：依赖失败、端口占用、超时、断线和恢复。
6. **Lifecycle**：创建、启动、停止、析构、重复关闭和异常回滚。

不要为每一格机械生成测试；必须能说明它保护的风险或契约。

## 6. Test Plan 必填字段

实现代码前先提交 Test Plan，每个重要用例至少包含：

- Test ID 和名称
- 对应源码/公开入口
- 被测行为或契约
- 类型：Unit / Component / Integration / E2E
- 前置条件与输入
- Expected Result / Invariant
- 外部依赖及替身策略
- Timeout
- 本地执行命令
- CI job 和 Label/分组
- 失败时需要保留的日志或 XML 报告

Expected Result 必须来自已确认需求或当前公开契约。若实现违反契约，应保留失败测试并报告疑似 Bug，不能修改正确预期来迁就实现。

## 7. 测试隔离和配置

- 禁止依赖测试顺序、本机历史数据或个人绝对路径。
- 测试配置必须在临时目录动态生成；不要直接修改或复用仓库中的 `config.ini`。
- TCP/gRPC/HTTP 使用 loopback 和动态端口；必须测试固定端口冲突时的失败行为时除外。
- 每个 ChatServer 测试实例必须使用唯一的 `SelfServer.Name`、TCP 端口、RPC 端口、`Log.Name` 和工作目录。
- Redis key 使用测试运行 ID 前缀；MySQL 使用专用 schema/database；测试完成后清理。
- 邮箱、API Key、数据库密码不得写入测试源码、日志或 artifact。
- 随机测试必须固定或记录 seed，失败必须能重放。

配置优先级测试应覆盖当前约定：显式 `--config` / `SetConfigPath` 高于 `CHAT_CONFIG`，最后才是工作目录下的默认配置。不同可执行文件若尚未实现同一失败策略，应把差异记录为缺陷，而不是在测试中默许静默回退。

## 8. Mock、Fake 和真实依赖

- Unit 可以 Mock Redis、MySQL、SMTP、gRPC stub、时钟和 UUID 生成器。
- 优先使用小型 Fake 表达状态，不要为每个内部调用建立脆弱 Mock。
- 测试 Redis/MySQL/gRPC 适配器本身时必须保留真实 Integration Test。
- 禁止 Mock 掉被测对象的核心行为。
- 生产代码无法隔离外部依赖时，只允许增加最小 test seam，例如构造参数、接口边界或 `require.main === module` 启动保护。
- 增加 test seam 不得改变默认生产行为，也不得顺便重构无关业务逻辑。

## 9. 并发和异步测试

- 禁止用固定 `sleep` 证明正确性；等待进程就绪也必须有条件和超时。
- 优先使用 `condition_variable`、`barrier`、`latch`、`promise/future`、事件或端口探测。
- 所有可能阻塞的测试必须有硬超时，并在失败时输出线程、队列、PID 和端口信息。
- 当前 ChatServer `LogicSystem` 是**单 worker + 单队列**，应验证全局 FIFO、容量边界和 shutdown；不得编造尚不存在的 `chat_id -> worker` 分片或“不同 chat 并行”契约。
- 连接池至少覆盖获取/归还、耗尽、关闭唤醒、重复关闭和依赖失效。

## 10. 工具和报告

- Server C++：GoogleTest / GoogleMock；与当前 MSBuild、动态 CRT 和 vcpkg triplet 保持一致。
- Qt 客户端：CTest 作为统一运行器；需要 Qt 信号、事件循环或模型断言时可使用 Qt Test。
- PowerShell：行为/生命周期测试优先 Pester 并输出 NUnit/JUnit；纯 CLI 启动前校验可使用仓库内无网络轻量运行器，但必须逐项诊断并以非零退出码传播失败。
- VarifyServer：`node:test`，通过依赖注入隔离 Redis、SMTP 和 UUID。

建议分组：

- `server_unit`
- `client_unit`
- `script_unit`
- `varify_unit`
- `integration_redis`
- `integration_mysql`
- `integration_grpc`
- `e2e`
- `stress`

CI 必须使用与本地相同的测试入口，并保存失败日志及 JUnit/XML。禁止用自动重试把 flaky test 刷成通过。

## 11. CI 接入原则

- Server Unit 放在现有 `servers-release` job 中，复用已经恢复的 vcpkg 依赖，避免再次占用大量磁盘和一小时以上冷构建时间。
- Qt 测试继续放在 `client-release` job。
- VarifyServer Unit 放在 `varify-release` job，并始终从 `npm ci` 的干净依赖树运行。
- PowerShell Unit 可放在 `static-check` 或独立轻量 job；不得依赖已构建 Server。
- Integration/E2E 只有在依赖可重复初始化、清理且运行时间可接受时才设为 required。
- 发布包验证继续保留；它属于发布契约检查，不能替代单元测试。

## 12. 命名、断言和覆盖率

测试名使用“场景 + 预期”，例如：

- `MissingSelfServerNameIsRejected`
- `TcpAndRpcPortCollisionIsRejected`
- `FailedBatchStartPreservesExistingInstanceState`
- `PoolShutdownWakesWaitingBorrower`
- `MessageHistoryDeduplicatesByServerId`

失败信息应包含 config path、实例名、PID、端口、message/chat/user id、expected/actual 和随机 seed 等必要上下文。

每个测试必须至少包含一个有效断言。覆盖率用于发现遗漏，不以行覆盖率代替行为验证；禁止只有执行没有断言的“覆盖率测试”。

## 13. Sanitizer 边界

当前主线是 Windows MSVC/MinGW。ASan 可在工具链可用时加入独立构建；TSan/UBSan 更适合未来 Linux 阶段。不得为了追求 Sanitizer 覆盖而改变阶段二已经验证的 Windows 发布 triplet。

## 14. AI 生成测试规则

1. 完整阅读被测源码、调用方、配置和构建文件。
2. 先输出 Test Plan，再实现测试。
3. 明确当前行为、目标契约和已知缺陷，不能混为一谈。
4. 不得引用已删除的 ChatServer1/ChatServer2 源码目录。
5. 不得生成依赖开发者 Redis/MySQL 地址或真实邮箱的测试。
6. 不得修改 protobuf 生成文件、第三方源码或业务逻辑来“方便通过”。
7. 只有测试性确实受阻时才提出最小 seam，并单独说明理由。
8. 实现后必须运行相关最小测试集合，再运行统一入口；报告执行命令和结果。

## 15. 完成定义

一个测试任务只有同时满足以下条件才算完成：

- Test Plan 已审核，Expected Result 明确。
- 本地可用非 IDE 命令重复执行。
- 新测试在干净环境通过，并能在故意破坏契约时失败。
- 没有访问个人/生产服务，没有遗留进程、端口、Redis key 或数据库数据。
- CI 已接入正确 job，失败会使 job 失败。
- 失败日志足以定位问题。
- 没有为了通过测试修改正确预期或无关生产逻辑。
