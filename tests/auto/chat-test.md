# Chat 项目自动测试范围与实施路线

## 1. 当前项目基线

阶段一、阶段二已经建立以下可验证基线：

- Windows 本地构建和 GitHub Actions 使用同一套脚本入口。
- GateServer、StatusServer、ChatServer 使用 MSBuild、vcpkg manifest、动态 CRT 和 app-local DLL。
- Qt 客户端使用 Qt 6.5.3 MinGW、CMake/Ninja，并已通过 CTest。
- VarifyServer 使用锁定的 npm 依赖并完成语法、依赖树和发布包检查。
- ChatServer1/ChatServer2 已合并为一个 ChatServer 目标；`chat-01.ini`、`chat-02.ini` 用于启动两个进程实例。
- GitHub CI 已验证干净环境冷构建、vcpkg binary cache miss/save/exact-hit 和三类发布 artifact。

阶段三的目标不是再次验证“能否编译”，而是建立能发现行为回归的自动化测试体系。

## 2. 当前测试覆盖盘点

### 已存在

- `chat/tests/message-model/message_model_tests.cpp`
  - 消息插入、确认、状态更新、删除。
  - 历史消息去重和顺序。
  - 多语言文本与换行。
  - 每个 chat 的模型缓存与分页状态。
  - delegate 在不同宽度下重新布局。
- Qt CMake 已注册 `message_model_tests`，CI 使用 `ctest --output-on-failure`。
- `tests/server/config/config_mgr_tests.cpp`
  - ChatServer `ConfigMgr` 的显式路径、有效实例配置、必填字段、端口和 peer 校验。
- `tests/server/messaging/msg_node_tests.cpp`
  - `MsgNode`、`SendNode`、`RecvNode` 的网络字节序、复制、清理和长度边界。
- `tests/server/concurrency/asio_pool_tests.cpp`
  - ChatServer `AsioIOServicePool` 的任务执行与幂等停止。
- `tests/server/protocol/protobuf_contract_tests.cpp`
  - 项目 protobuf 消息的序列化契约。
- `tests/server/ServerUnitTests.vcxproj` 统一注册上述模块并生成一个 GoogleTest 报告。
- `tests/scripts/validation/chatserver-instances.tests.ps1`
  - 实例配置的 Name、Log.Name、TCP/RPC、文件名和缺失输入校验。
- `tests/scripts/lifecycle/chatserver-instances.tests.ps1`
  - 子进程启动失败诊断、陈旧 PID 防误杀、Status 状态判定和跨批次实例冲突。
- `VarifyServer/test/config/config.test.js`
  - `--config`/`CHAT_CONFIG`/默认路径优先级和畸形 JSON 非零退出。
- `VarifyServer/test/protocol/protocol.test.js`
  - 错误常量和 gRPC proto 描述符。
- `scripts/windows-local.ps1` 已提供 `RunServerTests`、`RunClientTests`、`RunScriptTests`、`RunVarifyTests` 和统一的 `TestPhase1` 入口。
- CI 已验证 Server/Qt/VarifyServer 发布目录和 ZIP 的基本完整性。

### 尚不存在

- GateServer、StatusServer 的 Config/main 参数单元测试，以及 ChatServer 的 main 参数子进程测试。
- VarifyServer 验证码 handler 的 Redis/SMTP/UUID 行为测试。
- Redis、MySQL、gRPC 的可重复 Integration 环境。
- 跨服务业务 E2E。

第三方 `chat/packages/spdlog/tests` 不属于本项目覆盖率，不得把它计入项目测试成果。

## 3. Phase 3A：Windows PR 快速测试（当前优先）

本阶段不依赖真实 Redis、MySQL、SMTP 或公网。所有用例应能在当前 Windows GitHub runner 上稳定运行。

### T00 测试基础设施

**目标：** 建立最小而统一的测试运行入口。

- 为 Server 增加独立 GoogleTest 测试目标，不把测试源码编入发布 EXE。
- 测试目标使用和 Server 相同的 MSVC、`/MD`/`/MDd`、vcpkg triplet及头文件配置。
- 在 `scripts/windows-local.ps1` 中增加统一测试任务，支持 Debug/Release 和失败退出码。
- Server 测试输出 JUnit/XML；Qt 继续使用 CTest；VarifyServer 使用 `node --test`；PowerShell 配置和生命周期测试均使用仓库内无网络轻量运行器。
- CI 在现有 job 内运行对应测试，避免重复恢复大体积 Server 依赖。

**验收：** 空白/示例测试能在本地命令和 GitHub CI 中被发现、执行并正确传播失败。

### 阶段一首批四模块 Test Plan（已批准并实现）

| Test ID | 接口与行为 | 类型 / 输入 | Expected Result / Invariant | 外部依赖与超时 | 本地入口 / CI / 报告 |
| --- | --- | --- | --- | --- | --- |
| F01-CFG | `ConfigMgr::SetConfigPath`、构造函数和 `operator[]` | Unit；临时 INI、两份发布示例配置、缺失字段、非法端口、peer 边界 | 显式路径优先；有效单/双实例加载；必填项、端口冲突、自引用、重复和空 peer 项立即抛出可诊断错误 | 无 Redis/MySQL/网络；单例未用于用例隔离；通常小于 1 秒 | `RunServerTests` / `servers-release` / `server_unit.xml` |
| F02-MSG | `MsgNode`、`SendNode`、`RecvNode` 公开字段与 `Clear()` | Unit；空、嵌入 NUL、UTF-8 字节、`MAX_LENGTH` | header 为网络字节序；body 按指定长度复制；Clear 清零并重置进度；RecvNode 保留 ID | 无外部服务；通常小于 1 秒 | `RunServerTests` / `servers-release` / `server_unit.xml` |
| A02-PS | `chatserver-instances.ps1 -Task Start` 的启动前校验边界 | Component；临时配置和无效占位 EXE | 重复 Name/LogName、跨类型端口冲突、`08090`、危险/重复文件名和缺失输入在创建进程前失败 | 不启动真实 Server；每个子进程有同步退出边界；通常小于 10 秒 | `RunScriptTests` / `static-check` / 控制台逐项 PASS/FAIL |
| Q01-MODEL | `MessageListModel`、`MessageModelStore` | Unit；未知 ID、多批历史、删除/ack、Unicode/空文本 | 未知 ID 不改变模型；跨批去重且顺序稳定；索引随删除/ack 同步；文本无损 round-trip | Qt minimal/offscreen，无显示器/网络；通常小于 1 秒 | `RunClientTests` / `client-release` / `client_unit.xml` |

RED-GREEN 证据要求：四组测试至少各有一个可恢复的故障探针；临时更改断言或 fixture 后必须观察非零退出/CTest 失败，并在提交前完全恢复且复跑 GREEN。不得把故意失败内容提交到分支。

本批保留的覆盖缺口：

- `ConfigMgr` 已验证“被引用的 peer section 缺失”会失败；整个 `[PeerServer]` section 缺失目前与空列表等价，目标契约尚未明确，因此没有固化相反预期。
- `SendNode` 对负数或大于源 buffer 的 `msgLen` 没有安全校验 seam；直接构造可能触发未定义行为，本批只覆盖空值与最大应用层合法长度，不用崩溃测试代替输入契约。
- 实例脚本没有独立 `Validate` 动作；合法配置会进入进程启动路径，所以本批无进程测试只覆盖所有能够在启动前确定失败的输入。运行中状态冲突、批量启动回滚和优雅停止留给进程生命周期测试。
- GateServer、StatusServer 的配置与 `main` 参数差异、Redis/MySQL/gRPC 集成、VarifyServer 行为和跨服务 E2E 不属于本次四模块范围。

### 阶段一剩余可单测模块 Test Plan（已批准并实现）

| Test ID | 接口与行为 | 类型 / 输入 | Expected Result / Invariant | 外部依赖与超时 | 本地入口 / CI / 报告 |
| --- | --- | --- | --- | --- | --- |
| F03-ASIO | ChatServer `AsioIOServicePool::GetIOService`、`stop` | Unit；16 个异步任务、重复 stop | 已提交任务各执行一次并在 5 秒内完成；重复 stop 不抛异常、不二次 join | 无网络和外部服务；条件变量硬超时 5 秒 | `RunServerTests` / `servers-release` / `server_unit.xml` |
| F02-PROTO | `TextChatMsgReq`、`GetVarifyRsp` 生成类型 | Unit；路由字段、重复消息、嵌入 NUL、UTF-8、错误码 | 序列化/反序列化后字段值、消息顺序和二进制字符串完全保留 | 仅使用已锁定 protobuf；通常小于 1 秒 | `RunServerTests` / `servers-release` / `server_unit.xml` |
| A02-LIFE | `chatserver-instances.ps1` Start/Status/Stop | Component；立即退出子进程、当前测试进程的匹配/不匹配状态记录 | 启动失败包含退出码且无状态残留；陈旧 PID 显示 Stopped 且 Stop 不误杀；运行实例冲突在启动前失败 | 不启动真实 ChatServer，不占固定监听端口；启动失败上限 2 秒 | `RunScriptTests` / `static-check` / 控制台逐项 PASS/FAIL |
| V01-CONFIG | VarifyServer `config.js`、`const.js`、`proto.js` | Unit；隔离子进程和临时 JSON | 显式参数高于环境变量和默认文件；畸形 JSON 非零退出；错误常量与 RPC 路径保持稳定 | 无 Redis/SMTP/公网；使用 `node:test`；通常小于 1 秒 | `RunVarifyTests` / `varify-release` / `varify_unit.xml` |

本批 RED-GREEN 已分别用临时坏断言验证：C++ GoogleTest、PowerShell 生命周期测试和 Node 测试都产生非零退出；恢复后 Server 28/28、脚本 13/13、Varify 6/6 全部 GREEN。

本批仍保留的覆盖缺口：

- GateServer/StatusServer 的 ConfigMgr 构造入口私有且与 ChatServer 存在重复类型；在不复制解析逻辑、不扩展生产接口的前提下不生成伪单元测试，留待统一配置模块或独立进程测试。
- GateServer/StatusServer 的 Asio pool 与 ChatServer 已有实现漂移；本批只锁定合并后主 ChatServer 的幂等停止契约，另外两份实现若要套用同一 contract test，需要先统一公开 seam。
- `LogicSystem` 紧耦合 Singleton、Session、Redis/MySQL/gRPC 回调且没有可注入队列处理边界；不为测试而修改业务逻辑，留待后续最小 test seam 设计。
- VarifyServer `server.js` 加载即监听端口，handler 直接引用 Redis/SMTP/UUID；本批不改变启动逻辑，只覆盖可隔离的配置和协议模块。
- 畸形 JSON 的 Node 解析错误可能包含损坏配置行；若配置可能含秘密，应在后续安全修复中净化错误输出，而不是在本批测试中假定当前已经具备该行为。
- 批量启动中后续实例失败时对既有状态文件的回滚策略、真实端口占用、Redis 登记清理及优雅停止仍属于 Integration/缺陷修复范围。

---

### F01 Config 与启动参数

**测试对象：** 三个 C++ Server 的 `ConfigMgr`、三个 Server 的 `main` 参数解析，以及 VarifyServer `config.js`。

**ChatServer 已确认契约：**

- 显式配置路径高于 `CHAT_CONFIG`，最后回退工作目录 `config.ini`。
- `[SelfServer]` 的 Name、Host、TCP Port、RPCPort 必填。
- TCP 和 RPC 端口必须是 1..65535 且互不相同。
- Redis、Mysql、StatusServer endpoint 必须有效。
- Mysql User/Schema、Log Name/LogDir 必填。
- Peer 列表允许为空；非空时拒绝空项、自引用和重复 peer name。

**必须覆盖：**

- `chat-01.ini`、`chat-02.ini` 均能加载，并产生不同实例名、TCP、RPC 和日志身份。
- 单实例配置允许空 `PeerServer.Servers`。
- 缺失文件、缺失 section/key、空值、非数字端口、0、65536、前后垃圾字符。
- TCP/RPC 同端口立即失败并包含可诊断错误。
- peer section 缺失、peer 自引用、重复 peer、列表中的空项。
- `--config` 缺少路径或包含未知参数时必须非零退出，不得静默使用默认配置。

**当前差异：** GateServer 和 StatusServer 尚未实现与 ChatServer 等价的完整校验，且畸形 `--config` 可能静默回退。测试应表达统一的 fail-fast 目标并报告缺陷，不能把差异固化为正确行为。

**测试性注意：** ConfigMgr 是进程级 Singleton。多个配置场景优先使用参数化子进程，或先抽取无全局状态的最小配置解析器；不得依赖用例执行顺序重置静态对象。

---

### F02 TCP 消息节点与协议边界

**测试对象：** `MsgNode`、`SendNode`、`RecvNode`、CSession 的 header/body 解析边界、protobuf round-trip。

**必须覆盖：**

- 消息 ID 和 body length 使用网络字节序。
- 空 body、普通 body、中文/UTF-8、最大合法长度。
- `Clear()` 重置长度和缓冲区。
- 分段 header、分段 body、连续多包。
- 负数/超限长度、损坏 header、未知消息 ID 安全失败。
- send queue 达到 `MAX_SENDQUE` 时不越界、不重复发送。
- protobuf 序列化后反序列化保持字段一致。

不得测试 protobuf 自动生成类的内部实现；只测试项目传输契约。

---

### F03 AsioIOServicePool 生命周期

**测试对象：** GateServer、StatusServer、ChatServer 当前各自的 `AsioIOServicePool`。

**必须覆盖：**

- `hardware_concurrency()` 返回 0 时仍创建可用 worker。
- round-robin 获取有效 `io_context`。
- 提交的任务不丢失、不重复。
- `Stop()` 唤醒线程并可完成 join。
- 重复 Stop/析构不会死锁或二次 join。
- 并发提交与关闭有确定结果和超时。

三份实现存在漂移风险，应使用同一组参数化 contract tests，而不是复制三套预期。

---

### A01 LogicSystem 单队列契约

**测试对象：** ChatServer `LogicSystem` 的消息队列、worker 和 shutdown。

**当前架构事实：** 当前只有一个 `_worker_thread` 和一个 `_msg_que`，不存在 `chat_id -> worker` 分片。

**必须覆盖：**

- 单 worker 全局 FIFO。
- 空队列等待时不忙等。
- 多 producer 投递时每个节点只处理一次。
- 达到 `MAX_DEALQUE` 时拒绝策略明确且不破坏已有消息。
- shutdown 唤醒等待 worker，并对队列中剩余消息采用明确策略。
- 析构在有限时间内完成。

在未来真正引入分片前，不生成 `SameChatSameWorker` 或 `DifferentChatsRunConcurrently` 测试。

---

### A02 ChatServer 多实例和进程管理

**测试对象：** `scripts/chatserver-instances.ps1`、ChatServer 启动顺序和实例状态文件。

**配置合同测试：**

- 同一批次拒绝重复 `SelfServer.Name`、TCP、RPC、跨类型端口和 `Log.Name`。
- 端口以整数规范化，`08090` 与 `8090` 必须视为同一端口。
- 与脚本已记录且仍运行的实例冲突时拒绝启动。
- 配置文件名只能生成安全且唯一的 instance id。
- 每个实例使用独立工作目录、stdout、stderr 和状态文件。

**生命周期/回归测试：**

- 子进程在 startup timeout 内退出时，错误包含退出码和捕获输出。
- 批量启动第二个实例失败时，只停止和删除本次新建状态；原来已经运行的实例状态必须保留。
- 状态文件 PID 被复用但 executable/start time 不匹配时，Status/Stop 不得误操作其他进程。
- Stop 后不遗留由本次测试启动的进程和状态文件。
- TCP 或 gRPC 端口占用时 ChatServer 非零退出，不触发 `std::terminate`。
- 启动异常路径不得留下本次写入的 Redis `logincount` 字段。

**已知边界：** 直接手工启动 EXE 无法由管理脚本保证 Name/Log.Name 跨进程唯一。自动多实例测试必须通过管理脚本启动，或明确验证手工模式只依赖 OS 端口冲突保护。

---

### Q01 Qt 消息模型回归

**测试对象：** `MessageListModel`、`MessageModelStore`、`MessageItemDelegate`。

在现有测试基础上补充：

- 空 client message id、重复 client id、重复 server message id。
- acknowledge 未知 ID 不修改模型。
- 历史分页跨批次去重和稳定顺序。
- 状态只能按允许的方向变化时，对非法回退的处理。
- model reset/remove 后索引同步更新。
- 空文本、超长文本、emoji、组合字符和多行文本。
- `QT_QPA_PLATFORM=minimal` 下可重复运行，不依赖显示器和字体像素完全一致。

布局测试应验证相对关系或合法范围，不固定某个操作系统字体产生的精确像素值。

---

### V01 VarifyServer 验证码处理

**测试对象：** `GetVarifyCode`、配置选择、Redis/SMTP 错误映射。

**实现测试前需要的最小 seam：**

- `server.js` 仅在直接执行时调用 `main()`，被测试加载时不自动监听 50051。
- 导出 handler，并注入 Redis、mailer 和 UUID 生成器。
- `config.js` 抽取可传入路径的纯加载函数，默认行为保持不变。

**必须覆盖：**

- Redis 已有验证码时复用，不重新生成。
- Redis 无值时生成 4 字符验证码并设置 600 秒 TTL。
- Redis 写入失败返回 `RedisErr`，且不发送邮件。
- 邮件成功返回 `Success`；SMTP 拒绝/抛错返回 `Exception`。
- handler 始终只调用一次 gRPC callback。
- 显式 `--config` 高于 `CHAT_CONFIG`，缺失或畸形 JSON 立即失败。
- 测试不得发送真实邮件或连接开发者 Redis。

## 4. Phase 3B：真实组件 Integration

只有临时依赖能够自动初始化和清理后才实施。Windows 主线不具备服务容器时，可以等待 Linux/Docker 阶段，不得连接仓库配置中的固定局域网地址。

### I01 Redis

- `GET/SET`、`HGET/HSET/HDEL`、不存在 key、二进制/空值边界。
- `RedisConnectionPool` 获取、归还、耗尽、关闭唤醒和断线。
- `DistLock` owner、非 owner release、过期、竞争和 Redis 故障。
- 使用 run-id key 前缀并在 fixture teardown 清理。

### I02 MySQL

- `MysqlPool` 获取、归还、耗尽、断线和 shutdown。
- 用户、好友、私聊创建、消息批量写入和分页查询。
- 事务失败时不产生半写入状态。
- 使用专用临时 schema，迁移和清理步骤必须版本化。

### I03 gRPC

- Status、Chat、Varify 的 request/response round-trip。
- unavailable、deadline exceeded、对端关闭和连接池 shutdown。
- 两个 ChatServer 配置实例之间的 peer name 到 RPC endpoint 映射。
- 不测试 gRPC 库内部，只验证项目 service 和错误映射。

### I04 HTTP/TCP Transport

- GateServer loopback HTTP 正常请求、畸形 JSON、断开和超时。
- ChatServer TCP accept、认证前断开、半包/粘包、多连接隔离和关闭。
- 测试动态获取端口，不依赖 8080/8090/8091 空闲。

### I05 服务启动与退出

- Gate、Status、Chat、Varify 使用临时配置启动并达到 ready 条件。
- 缺失依赖、错误凭据和端口冲突按合同非零退出。
- SIGINT/SIGTERM 或平台等价退出路径释放监听端口和注册状态。
- CI 失败时保存每个进程 stdout/stderr，finally 中按已记录 PID 清理。

## 5. Phase 3C：业务 E2E

在 Foundation 和 Integration 稳定后覆盖：

1. 获取验证码。
2. 注册和登录。
3. 搜索用户、申请好友、接受好友。
4. 创建私聊并发送消息。
5. 两个用户位于不同 ChatServer 进程时跨实例投递。
6. 下线、重新登录及 session/token 更新。
7. 历史消息分页、顺序和去重。

E2E 只验证公开结果，不依赖内部 Redis key 或具体 worker 实现。测试用户、邮箱、Redis key 和数据库记录必须使用运行 ID 隔离并清理。

## 6. 暂不固化的设计点

以下行为在当前源码中仍有实现差异或设计未收敛，写测试前必须先确认目标契约：

- StatusServer 应按最小连接数、固定顺序还是其他策略选择 ChatServer。
- Redis/MySQL 在启动时不可达是否必须立即退出，以及允许的重连窗口。
- LogicSystem shutdown 是 drain 队列还是拒绝未处理消息。
- ChatServer 管理脚本 Stop 是否需要优雅退出而不是强制终止。
- 发送队列和逻辑队列满时的客户端错误码或丢弃策略。

可以先写“表征当前行为”的临时测试帮助讨论，但不得把未确认行为标记为永久架构契约。

## 7. 实施顺序

```text
T00 测试入口和 CI 报告
  ↓
F01 Config/参数 + A02 PowerShell 实例管理
  ↓
F02 MsgNode/协议 + F03 AsioIOServicePool
  ↓
A01 LogicSystem 单队列 + Q01 Qt 回归
  ↓
V01 VarifyServer handler
  ↓
I01/I02 Redis/MySQL
  ↓
I03/I04 gRPC/HTTP/TCP
  ↓
I05 服务生命周期
  ↓
Phase 3C 业务 E2E
```

优先 Config 和实例脚本，是因为它们依赖少、执行快，并且能直接覆盖阶段一、阶段二审查已经发现的回归风险。

## 8. 阶段三验收标准

- 本地和 GitHub CI 使用同一测试入口。
- Server、Qt、VarifyServer、实例脚本至少各有一组真实行为测试。
- PR Unit 不依赖 Redis、MySQL、SMTP、公网或个人配置。
- 测试失败会使对应 CI job 失败，并上传可诊断日志/XML。
- 现有 Windows clean build、发布包和 cache 验证继续通过。
- 测试不修改业务预期，不引入 ChatServer1/ChatServer2 重复实现。
- Integration/E2E 不遗留进程、端口、Redis key、数据库数据和凭据。
- 已确认缺陷使用回归测试保护，未确认设计点不会被错误固化。

所有测试设计和实现同时遵循 `tests/auto/test-strand.md`。
