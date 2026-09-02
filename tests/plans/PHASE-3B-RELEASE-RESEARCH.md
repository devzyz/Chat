# Phase 3B～Release gate 技术研究

**研究日期：** 2026-08-30

**范围：** Phase 3B、3C、3D 与 Release gate 的正式规划输入

**总体置信度：** HIGH（代码库现状）；MEDIUM（2026 年外部平台事实，均引用官方资料）

**研究性质：** 只读代码与官方资料；本文不给出生产实现，也不预先承诺未知 testcase 数量。

## User Constraints（锁定合同）

以下内容来自 `tests/plans/PHASE-3B-RELEASE-DECISIONS.md`，状态为 Confirmed。规划者不得在 PLAN 中改写或弱化这些边界。[VERIFIED: codebase `tests/plans/PHASE-3B-RELEASE-DECISIONS.md`]

- **DG-09 阶段边界：** 3B 只处理不依赖 Redis/MySQL/SMTP 的真实 transport/process Integration；3C 处理 Linux 原生 build/run、disposable Redis/MySQL/SMTP、schema/migration、四进程真实依赖与兼容；3D 处理双 ChatServer/双客户端跨实例业务 E2E；Release gate 单独负责不可变 artifact smoke、版本化人工 UAT 和晋升。
- **DG-10 Docker 范围：** Phase 3 不制作应用 Docker 镜像；四应用必须在 GitHub 托管 Ubuntu 原生 build/run；正式镜像、编排与生产容器部署均延期。
- **DG-11 runner 拓扑：** `develop` 保持托管 Windows 全量快速门禁；不新增 self-hosted Required runner；3C/3D 使用托管 Ubuntu 与 disposable service containers；runner/依赖不可用、启动失败、超时、报告缺失或清理失败均为 CI 失败。
- **DG-12 数据政策：** 只用版本化、生产形状一致的合成数据；覆盖 Unicode、边界长度、重复输入、历史分页、异常关系与批量/顺序规模；每次 run-id 隔离 schema、Redis key、邮箱、端口与临时目录。
- **DG-13 兼容基线：** Phase 2.5 descriptor/wire fixture 是初始协议基线；第一次通过 Release gate 的不可变 artifact 成为完整 N-1 artifact/schema 基线；以后默认只验 N/N-1。
- **DG-14 3B transport：** Gate HTTP、Status gRPC、Chat TCP、Qt `QNetworkAccessManager`/`QTcpSocket` 都要真实 loopback Integration；transport 内部链接 Phase 3A 生产业务 Module 与 in-memory Adapter，不访问真实依赖或公网；确定性套件进入 `develop` Required gate。
- **DG-15 schema：** 3C 同时验证 fresh 初始化与 N-1 schema/data 到 N；只对声明可逆的 migration 自动 rollback；不可逆 migration 记录备份、前滚恢复和阻断条件；结果、schema 版本、fixture 版本可审计。
- **DG-16 3D 拓扑：** 两个 ChatServer 与两个客户端，客户端分别连不同 ChatServer；固定流程含验证码、注册、登录/服务发现、Chat Token 登录、好友申请/接受、私聊、跨实例投递、断线重登、历史分页、顺序与去重；单 ChatServer 不能替代，更大 mesh/高并发归 stress/soak。
- **DG-17 发布集合：** 晋升一个统一版本化 Windows x64 集合，至少含 Gate/Status/Chat EXE、Varify Node 包、配置模板、canonical proto、migration、manifest 与校验值；smoke、兼容、UAT 使用同一集合，通过后不得重建。
- **DG-18 Linux：** Linux build 链接与 Windows 发布目标相同的生产 Module/Adapter 实现，禁止复制第二套业务实现；Windows 仍是发布平台，Linux 首先是 Integration/E2E 执行平台，不承诺 Linux 正式发布支持。
- **DG-19 test host：** 3B test host 组合真实生产 transport、真实生产业务 Module 与 in-memory Adapter；正式 EXE 不得增加 fake-dependencies、测试宏或测试专用公开 Interface；正式 EXE 由黑盒启动/ready/停止测试保护，`CheckTestStructure` 保护 composition root 接线。
- **DG-20 故障矩阵：** Required 覆盖分片/合包、最大长度、畸形输入、中断、拒绝连接、deadline、重复/迟到 completion、端口冲突及进程/线程/socket/端口/临时目录释放；随机 fuzz、负载、长 soak 只进 scheduled/explicit lane。
- **DG-21 兼容矩阵：** 覆盖 N-1 client→N server、N client→N-1 server、N/N-1 server RPC、N-1 schema/data→N；不支持组合 fail-fast 且给出稳定诊断；静态 fixture 不能代替跨版本进程证据。
- **DG-22 UAT：** 仓库维护版本化清单；自动 smoke 全绿后用户在同一 artifact 上签署；证据随 release 保存；缺陷阻断，能自动化的缺陷形成回归并重走相应门禁。
- **DG-23 本地 Linux VM：** 仅可选 parity 环境，不是 Required runner；只用标准 SSH key，不依赖 MobaXterm GUI/交互密码/个人会话；托管 Ubuntu 是权威证据。
- **DG-24 私聊：** 允许重试、幂等持久化，不宣称网络 exactly-once；`(sender,msg_uuid)` 只生成一个服务端消息，重复请求返回原 `message_id`；提交顺序由服务端 `message_id` 表达；通知/历史可重复到达，客户端按稳定 ID 去重；schema/migration、服务端写入、跨实例投递、客户端模型、断线重试共同验证。
- **全局停止条件：** 个人凭据/固定开发 endpoint/共享数据库/公共 SMTP；Linux 复制业务算法；通过 private container、`clearForTest` 或 fake-mode EXE 测试；将 Adapter-only 误称完整四进程 Integration；无不可变 N-1 artifact 时伪造完整跨版本矩阵——任一出现都必须停止并重构合同。

## Summary

当前 Phase 2.5 已建立 12 份报告、173 个 testcase 的基线和四个 Windows Required checks，但这些数字只是当前冻结基线，不是未来计划额度；新增测试后必须由实际 runner manifest 与 `CheckTestStructure` 同步得出新计数，PLAN 不得先写死未知数量。[VERIFIED: codebase `tests/plans/PHASE-2.5-07-SUMMARY.md`, `tests/REGRESSION.md`, `scripts/windows-local.ps1`, `.github/workflows/windows-ci.yml`] 当前服务端只有 Visual Studio solution/project，没有顶层 CMake/CMakePresets；Qt 客户端已有 CMake production libraries；Varify 是 Node 包。因此 Linux 原生 build/run 是真实迁移工作，不是声明“跨平台”或新增 Ubuntu job 即可完成。[VERIFIED: codebase `GateServer/*.sln`, `StatusServer/*.sln`, `ChatServer/*.sln`, `chat/CMakeLists.txt`, `VarifyServer/package.json`; negative search: no server/top-level `CMakeLists.txt` or `CMakePresets.json`]

最深的发布前 correctness 缺口是 DG-24。客户端生成 `msg_uuid`，服务端也短暂读取并回送映射，但 SQL 只插入 `chat_id/send_id/recv_id/content/status`；仓库没有 schema/migration，无法建立 `(sender,msg_uuid)` 唯一约束。`AddChatMessageList` 关闭 autocommit 后成功路径没有显式 `commit()`，异常路径也没有显式 rollback，而 defer 会把 autocommit 改回 true；MySQL 官方说明从 autocommit=0 改为 1 会隐式提交，因此当前结构存在部分写入被清理动作提交的风险，不能作为幂等基线。[VERIFIED: codebase `chat/chatpage.cpp:265`, `ChatServer/ChatServer/LogicSystem.cpp:590-604`, `ChatServer/ChatServer/MysqlDao.cpp:888-937`; CITED: https://dev.mysql.com/doc/refman/8.4/en/commit.html; CITED: https://dev.mysql.com/doc/refman/8.4/en/server-system-variables.html]

**首要建议：** 以 Phase 3A 的生产业务 Module 为内核，先建立“同一生产源文件、两种 composition root（正式 EXE 与 IntegrationHost）”的深接口，再完成共享 CMake/Linux 路径、真实 adapters/schema/idempotent MessageCommit，最后在同一 RunContext 下做双实例 E2E；发布只晋升 Windows 构建一次产生的不可变集合。

## Architectural Responsibility Map

| Capability | Primary owner | Secondary owner | 规划理由 |
|---|---|---|---|
| Gate HTTP / Status gRPC / Chat TCP transport | Server transport Module | Business Module | transport 负责 framing、deadline、取消与连接生命周期；业务 Module 不感知 socket。 |
| Qt HTTP/TCP transport | Client transport Module | Client session/model Module | `QNetworkAccessManager`/`QTcpSocket` 是生产 transport；model 只消费稳定结果。 |
| 验证码、用户、好友、路由业务 | Business Module | Adapter Interface | Phase 3A 的 Module 是唯一算法 owner，in-memory/real adapters 只替换外部 I/O。 |
| `msg_uuid` 幂等提交 | MessageCommit business/data Module | MySQL migration + client model | 唯一性最终由数据库约束保证；Module 定义事务和重复返回语义；客户端负责 retry/dedup。 |
| schema/migration | Database/Storage Module | CI fixture coordinator | migration 是发布资产，不能藏在容器 init 或测试脚本中。 |
| Redis TTL/断线恢复 | Redis Adapter | Session/routing business Module | Adapter 负责 timeout、连接淘汰/重连；Module 负责 TTL/缺失时的业务语义。 |
| SMTP sink 验证 | Email Adapter | Varify business Module | 同一生产 Adapter 用可配置 SMTP endpoint；测试由 Mailpit 观察外部效果。 |
| 多进程编排、ready、证据、teardown | Test infrastructure Module | OS Process Adapter | 深 `ProcessHarness` 隐藏 Win32/POSIX 差异，不污染正式 EXE。 |
| N/N-1 下载与兼容 | CI compatibility lane | Artifact store/release assets | 跨版本程序必须从不可变产物取得，不能从源码重建“旧版”。 |
| Windows 发布晋升 | Release pipeline | UAT evidence | 一次构建、多阶段验证、原样晋升，维持 SHA→artifact→UAT 可追溯链。 |

## Fact Inventory

### 1. Build system 与平台边界

| 事实 | 证据与置信度 | 对规划的含义 |
|---|---|---|
| Gate/Status/Chat 服务端均由 `.sln/.vcxproj`、`Directory.Build.props/targets` 驱动；没有服务端或仓库顶层 CMake。 | [VERIFIED: codebase file inventory and negative `rg --files -g CMakeLists.txt -g CMakePresets.json`] | Linux Wave 0 必须先完成真实 compile/link spike；不得假装已经 portable。 |
| Qt `chat/CMakeLists.txt` 要求 CMake 3.21/C++17，链接 Qt Widgets/Network，并已有 `chat_network_core`、`chat_message_model`、`chat_session_core` production libraries 与 CTest labels。 | [VERIFIED: codebase `chat/CMakeLists.txt`] | Qt 可作为共享 CMake 模式参考，但 Linux Qt 的确切版本/插件必须另锁。 |
| 现有 Windows CI 固定 `windows-2022`、Node 22、Qt 6.5.3 MinGW 11.2 与 vcpkg baseline；四个 Required checks 分别是 static/server/Qt/Varify。 | [VERIFIED: codebase `.github/workflows/windows-ci.yml`, `tests/CI-GOVERNANCE.md`] | 保持 Windows develop 快门禁；新增 Linux lane 不替代它。 |
| 本地 vcpkg 安装状态显示 Boost 1.90.0、gRPC 1.76.0、GTest 1.17.0、hiredis 1.3.0、JsonCpp 1.9.6、MySQL Connector/C++ 9.1.0、protobuf 6.33.4、spdlog 1.17.0。 | [VERIFIED: codebase `vcpkg_installed/vcpkg/status`] | 这是当前锁定环境事实，不等于建议升级或全平台已验证。 |
| 当前安装导出 CMake targets：`hiredis::hiredis`、`JsonCpp::JsonCpp`、`gRPC::*`、`protobuf::*`、`mysql::concpp-jdbc`、`spdlog::spdlog`、`GTest::*`。 | [VERIFIED: codebase generated configs under `vcpkg_installed/x64-windows-chat-release/share`] | 最小 Linux 路线可用 vcpkg manifest + imported targets，禁止手写库文件绝对路径。 |
| CMake 的 target 模型让同一 `add_library` 被多个 executable/test target 链接；vcpkg toolchain 必须在 `project()` 前生效，manifest mode 可按声明安装依赖。 | [CITED: https://cmake.org/cmake/help/latest/command/add_library.html; CITED: https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration; CITED: https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode] | 生产算法应归 production library target，正式 EXE 与 test host 仅组合，不各编译复制实现清单。 |

**最小 CMake/依赖路线（推荐，不是已完成事实）：**

1. 在 Phase 3A 完成后的生产 Module 边界上建立仓库顶层 CMake 和 presets；Windows preset 保留现有 release triplet/toolchain，Linux preset 使用 manifest 与明确 triplet，首次 CI proof 必须真实 configure/compile/link 四应用。
2. 把 canonical proto 生成与 `chat_protocol_cpp`（名称可按现有规划）做成单一 target；Gate/Status/Chat 的 business、transport、real-adapter 分别是 production targets；composition-root executable 只解析配置和组装依赖。
3. tests 链接相同 targets；不得再次列出生产 `.cpp` 形成第二份 target。`CheckTestStructure` 增加“production target 与正式 composition root/test host 接线”静态合同。
4. 先验证当前 vcpkg baseline 在 `x64-linux` 上能解析所有 feature/port，再锁 Ubuntu image、compiler、CMake、Qt 与 vcpkg triplet。某 port 不能在 Linux 构建即是 spike stop condition，不能用复制算法绕过。
5. MySQL 代码当前使用 legacy JDBC API，因此在本阶段保留 Connector/C++ 9.1 及 `mysql::concpp-jdbc`，不借迁移改成 X DevAPI。[VERIFIED: codebase includes under `ChatServer`, `GateServer`, `StatusServer`; VERIFIED: installed `mysql-concpp-config.cmake`]

### 2. 配置与启动入口

| 应用 | 当前生产入口事实 | 规划缺口 |
|---|---|---|
| Gate | 支持 `--config path`，启动 Beast HTTP server，接收 SIGINT/SIGTERM，Windows 另有 SIGBREAK；signal path 停 server/pool/io。[VERIFIED: codebase `GateServer/GateServer/GateServer.cpp`] | 需要稳定 ready probe、动态 loopback port/配置模板和跨平台 process evidence。 |
| Status | 支持 config 参数，启动 gRPC server并处理 SIGINT/SIGTERM/SIGBREAK，signal thread 可 join。[VERIFIED: codebase `StatusServer/StatusServer/StatusServer.cpp`] | 需要生产协议级 ready，不以“进程尚存”代替 ready。 |
| Chat | 支持 config 参数，同时启动 TCP 与 gRPC、Redis 注册；只注册 SIGINT/SIGTERM。[VERIFIED: codebase `ChatServer/ChatServer/ChatServer.cpp`] | `CServer::stop()` 只清 sessions/cancel timer，未显式 close acceptor；accept error 后继续启动 accept；需先收敛跨平台停止语义。[VERIFIED: codebase `ChatServer/ChatServer/CServer.cpp`] |
| Varify | `createServer(handler)` 已可注入 production gRPC handler，但 `main()` listen address 固定 `0.0.0.0:50051`。[VERIFIED: codebase `VarifyServer/server.js:67-98`] | 需要 runtime host/port、graceful signal、ready contract；不可引入 fake EXE mode。 |
| Qt chat | GUI `main.cpp` 从应用目录配置启动；当前无 CLI/headless app mode。[VERIFIED: codebase `chat/main.cpp`] | 3B 应测试生产 Qt transport Module，不应为 CI 给 GUI EXE 加 fake mode；3D 客户端驱动方式需在 PLAN 明确。 |

### 3. Redis/MySQL/SMTP adapters

| 领域 | 当前事实 | blocker/验证要求 |
|---|---|---|
| C++ Redis | Gate、Status、Chat 各有 RedisMgr/pool；Gate/Status 用同步 `redisConnect` 且 borrow 无限 wait；Chat borrow 有有限等待与 heartbeat，但连接/命令仍缺统一 connect/command timeout 和坏连接淘汰合同。[VERIFIED: codebase `GateServer/GateServer/RedisMgr.cpp`, `StatusServer/StatusServer/RedisMgr.cpp`, `ChatServer/ChatServer/RedisMgr.cpp`] | 3C 必须在真实 Redis 上测 TTL、refused、断线、恢复、deadline、pool 析构；不是只测 manager mock。当前 hiredis 头文件提供 `redisConnectWithTimeout`、`redisSetTimeout`、`redisReconnect`。[VERIFIED: codebase installed `include/hiredis/hiredis.h:298-323`] |
| Varify Redis | `ioredis` 在模块加载时创建全局 client；`setRedisExpire` 分两步 SET 和 EXPIRE。[VERIFIED: codebase `VarifyServer/redis.js`] | 改为可组合 Adapter、原子 `SET key value EX seconds`；验证到期与进程断开。[CITED: https://redis.io/docs/latest/commands/set/] |
| C++ MySQL | 三服务有 singleton/config-coupled DAO/pool；Chat pool heartbeat thread `detach()`，析构无法 join。[VERIFIED: codebase `ChatServer/ChatServer/MysqlDao.cpp:44`] | 四进程可重复 teardown 前必须去掉 detached lifetime；真实 DB tests 必须有有限 connect/borrow/query timeout。 |
| schema | 仓库没有 `.sql` schema、migration、schema version ledger 或 seed/dump，代码却依赖 `user/apply_friend/friend/chat/private_chat/chat_message` 与 `reg_user` stored procedure。[VERIFIED: codebase SQL strings plus negative `rg --files -g *.sql`] | fresh DB 和 N-1 migration 当前不可执行；3C 要先建立 versioned migration Module 与 synthetic fixtures。 |
| SMTP | Varify email Adapter 固定 `smtp.qq.com:465`/secure，并读取环境凭据。[VERIFIED: codebase `VarifyServer/email.js`] | production Adapter 必须支持 host/port/secure/auth 配置；CI 用无公网、无个人凭据的 sink。 |

### 4. Protocol、compatibility assets 与 test runner

- canonical protocol 位于 `proto/{varify,status,chat}.proto`，C++ 生成物在 `generated/proto/cpp`；Phase 2.5 还保存 descriptor/wire fixtures 和 `scripts/protocol-compatibility.js`。[VERIFIED: codebase file inventory, `tests/server/protocol/fixtures`, `tests/plans/PHASE-2.5-07-SUMMARY.md`]
- 初始 fixtures 是静态协议基线，不是一个已晋升旧发布；DG-13 要等第一次 Release gate 后才有完整 N-1 artifact/schema。[VERIFIED: codebase `tests/plans/PHASE-2.5-07-SUMMARY.md`, locked DG-13]
- 现有 process startup tests 使用 Win32 `CreateProcessW`、process group/`CTRL_BREAK_EVENT`；`cpp_node_varify_loopback_tests.cpp` 直接包含 `windows.h`。[VERIFIED: codebase tests under `tests/server`] Linux 需要共享 ProcessHarness Interface 加 Win32/POSIX Adapter，不能复制每个测试的编排。
- 黑盒测试已有动态 loopback port、有限 deadline 与清理模式；`CheckTestStructure` 还对报告、source wiring 和计数做硬合同。[VERIFIED: codebase `tests/server`, `tests/CheckTestStructure.cmake`, `scripts/windows-local.ps1`] 新阶段必须沿用“稳定 test ID→case→report→runner gate”链，但实际 case 存在后才能确定计数。

## Recommended Architecture

### System flow

```text
                     production composition roots
  config/templates ──> Gate EXE / Status EXE / Chat EXE / Varify main
                              │
                              ▼
       HTTP / gRPC / TCP / Qt transport Modules (production code)
                              │ stable request/result Interfaces
                              ▼
                    Phase 3A business Modules
                              │ dependency Interfaces
                  ┌───────────┴────────────┐
                  ▼                        ▼
        real Adapters (3C+)       in-memory Adapters (3B only)
      MySQL/Redis/SMTP/gRPC         deterministic state/faults

                     test composition roots
  RunContext ──> IntegrationHost ─────────┘
       │          (real transport + business + in-memory adapters)
       └──> ProcessHarness ──> Win32ProcessAdapter / PosixProcessAdapter
                 │
                 ├── protocol-level ready probes
                 ├── bounded logs/evidence
                 └── reverse-order teardown ledger
```

这里的 **Module** 是隐藏大量实现决策的深模块，**Interface** 是调用者真正需要知道的稳定合同，**seam** 是可被两个真实 Adapter（如 in-memory 与 MySQL）替换的位置。推荐“replace, do not layer”：一旦 Phase 3A 已抽出生产 Module，就让旧 composition root 和新 host 都调用它，不在旧 singleton 外再包一层同样复杂的 facade。[VERIFIED: required skill `codebase-design/SKILL.md`, `codebase-design/DEEPENING.md`]

### Interface 1：生产业务 Module 与 IntegrationHost

**推荐合同：** 每个 transport handler 只把解析后的 request + authenticated context 交给 Phase 3A Module；Module 通过 constructor/factory 接受 Repository/Cache/Email/PeerRoute Interfaces。3B 的 IntegrationHost 是测试目录里的 composition root，可配置 loopback endpoint、in-memory adapters、deterministic fault schedule 和 ready channel，但它链接生产 transport/business targets。

**不得做：**

- 在正式 EXE 增 `--fake-dependencies`、测试宏、`clearForTest`、公开内部容器或 test-only virtual method。
- 重新实现一个“简化 HTTP/gRPC/TCP server”后称为 production transport Integration。
- 测试直接包含 production `.cpp` 且与正式 target 形成不同编译定义/源清单；应链接 production target。

**3B host 具体形状：**

| Host | 必须复用 | 可替换 seam | ready/行为证据 |
|---|---|---|---|
| GateIntegrationHost | Beast HTTP connection/router + Gate business Module | account/cache/verify-client in-memory adapters | 实际 HTTP request/response，协议级 probe，断连/畸形/最大长度/deadline |
| StatusIntegrationHost | production gRPC service + Status business Module | user/route/cache in-memory adapters | 实际 generated stub 调用与 gRPC deadline/cancel/completion |
| ChatIntegrationHost | production acceptor/session/frame codec/logic Module | message/user/route repositories in memory | 实际 TCP bytes，分片/合包/中断/queue/teardown |
| Qt transport tests | production QNAM/QTcpSocket transport Modules | loopback peer 仅扮演远端协议端点 | `QSignalSpy`/event loop 观察 production signals/results；拒绝连接、late reply、session reset |

Qt 当前 manager 是 singleton 且夹杂状态；若它阻碍测试，优先深化为 `GateHttpTransport` 与 `ChatTcpTransport` 之类的 production Module，让 GUI 与 tests 都调用。这个重构是生产设计，不是暴露 private 测试 seam。

### Interface 2：MessageCommit（DG-24 的 owner）

推荐的深接口语义如下；名字是规划占位，PLAN 可按 Phase 3A 命名规范落地：

```cpp
CommitResult CommitTextBatch(
    const AuthenticatedPrincipal& sender,
    ChatId chat,
    UserId recipient,
    span<const PendingTextMessage> messages);

// PendingTextMessage = { ClientMessageUuid, Content }
// CommitResult item = { ClientMessageUuid, MessageId,
//                       Disposition::Created | Existing }
```

**规定：**

1. sender 必须来自已认证 session，而不是信任 JSON `from_uid`。当前 handler 读取 caller payload 的 `from_uid` 并传入 DAO，因此 spoofing 与幂等 key owner 必须在此 seam 收敛。[VERIFIED: codebase `ChatServer/ChatServer/LogicSystem.cpp`]
2. MySQL `chat_message` 新增持久化 client UUID，并用 `UNIQUE(send_id, client_msg_uuid)` 作为最终并发仲裁；唯一索引可阻止重复 key，插入冲突必须查询并返回已存在 row 的同一 `message_id`。[CITED: https://dev.mysql.com/doc/refman/8.4/en/constraint-primary-key.html; CITED: https://dev.mysql.com/doc/refman/8.4/en/create-index.html; CITED: https://dev.mysql.com/doc/refman/8.4/en/insert.html]
3. 推荐一个 batch 是一个显式事务：验证 sender/chat membership/recipient，逐项 insert-or-read-existing，显式 COMMIT；任何非幂等错误显式 ROLLBACK，绝不依赖 `setAutoCommit(true)` 清理产生提交。MySQL 在 autocommit disabled 时要求 COMMIT/ROLLBACK，而切回 autocommit 会隐式提交。[CITED: https://dev.mysql.com/doc/refman/8.4/en/commit.html; CITED: https://dev.mysql.com/doc/refman/8.4/en/server-system-variables.html]
4. 同一 `(sender,msg_uuid)` 若 payload 的 chat/recipient/content 与既存记录不同，必须 fail-fast 为稳定 conflict，而不是静默当重复成功；否则 UUID 重用会篡改调用意图。这是从 DG-24 唯一性合同推导的推荐语义。[INFERENCE: locked DG-24]
5. 只有 commit 成功后才通知。跨实例 `TextChatData` 和同实例通知均传 server `message_id` 与 client UUID；历史至少有稳定 server `message_id`。通知与历史可重复，client model 按 server ID 去重。[INFERENCE: locked DG-24; VERIFIED: current cross-instance notification drops UUID in `ChatServer/ChatServer/LogicSystem.cpp:590-620`]
6. 客户端 pending queue 在 disconnect 后不能直接丢失；重连以原 UUID bounded retry，收到 Created/Existing 的同一 server ID 后完成。当前 `TcpMgr` 写 socket 时未验证完整 write，并在 reset 清 pending batches，因此这是实现前缺口。[VERIFIED: codebase `chat/tcpmgr.cpp`]

### Interface 3：SchemaMigration

推荐一个应用拥有的 migration Module，而不是散落 CI SQL：

```text
Inspect(database) -> {current_version, applied_checksums}
Plan(from, to)     -> ordered immutable migration IDs
Apply(plan)        -> audit record per migration + final version
Verify(expected)   -> structural/data invariants
```

- 版本化 migration 与 `schema_version`/checksum ledger 随 DG-17 发布集合交付；fresh schema 必须由从 0 应用同一 migrations 得到，不另维护一份易漂移的“最终 schema.sql”。
- N-1 fixture 只从已晋升 N-1 schema/data 产生；首次发布前只能保留 Phase 2.5 protocol fixtures，不能虚构旧 schema。
- MySQL 许多 DDL 会隐式 commit 或不能 rollback，因此每个 migration 要声明 reversible/irreversible；irreversible 必须先备份、定义 forward-recovery 和 stop condition。[CITED: https://dev.mysql.com/doc/refman/8.4/en/cannot-roll-back.html; CITED: https://dev.mysql.com/doc/refman/8.4/en/upgrade-before-you-begin.html]
- N-1 evidence 应保存输入 fixture version/hash、migration plan/checksums、前后 schema version、数据 invariant 结果和日志；可用官方 dump 工具生成/恢复 definition+data fixture，但 fixture 本身必须去真实数据化并版本化。[CITED: https://dev.mysql.com/doc/refman/8.4/en/mysqldump-upgrade-testing.html]

### Interface 4：RunContext 与 ProcessHarness

`RunContext` 是一次 lane 的唯一资源 owner，包含不可变 run-id、端口 bundle、临时根目录、synthetic schema/key/email namespace、child process registry、log paths、deadline 与 cleanup ledger。`ProcessHarness` 提供 `Start(ProcessSpec)`、`WaitReady(Probe, deadline)`、`Stop(deadline)`、`CollectEvidence()`；OS 细节落在 Win32/POSIX Adapters。

**ready：** 必须是协议级 probe，不以 sleep、PID 存活或单纯日志文本作为 ready。Gate 发真实 HTTP probe，Status/Varify 发有 deadline 的真实 gRPC probe，Chat 建立 TCP 并完成最小合法 frame/response；依赖服务先过自己的 health 再启动应用。

**端口：** service containers 使用 runner 分配的随机 host port；应用端口由 RunContext 申请 loopback port bundle并在启动配置中显式传入。端口冲突测试使用独立预占端口，不能污染正常端口分配。自动重跑到绿色会掩盖生命周期 bug，禁止把 retry 当正确性策略。

**teardown ownership：** 客户端→ChatServer 2→ChatServer 1→Gate/Status/Varify→应用临时资源；service containers 由 GitHub job 生命周期最终销毁。测试必须证明 child process、线程、socket、端口和 temp dir 已释放。失败路径先保全日志，随后 `always()` cleanup；cleanup 自身失败要让 job 失败，而不是覆盖原始诊断。

## Disposable dependencies on GitHub-hosted Ubuntu

GitHub 官方说明：service container 每个 job 新建、job 完成即销毁；直接在 runner 上执行 job 时服务由 `localhost:<mapped-port>` 访问，只有 Linux runner 支持。省略 host port 会随机映射空闲端口，可由 `job.services.<id>.ports[...]` 读取；`options` 可配置 Docker health check。[CITED: https://docs.github.com/en/actions/tutorials/use-containerized-services/use-docker-service-containers] GitHub 的 Redis 示例使用 `redis-cli ping` 与 health interval/timeout/retries。[CITED: https://docs.github.com/en/actions/tutorials/use-containerized-services/create-redis-service-containers]

**推荐 topology：** 一个明确锁定的 `ubuntu-24.04` job（不是 `ubuntu-latest`）声明 Redis、MySQL、Mailpit services；应用在 runner 原生启动，分别从随机映射端口读取配置。GitHub 当前列出 `ubuntu-24.04`/`ubuntu-22.04` 等标签，并说明 `-latest` 不保证等同发行商最新 OS，因此固定标签更适合可审计 evidence。[CITED: https://docs.github.com/en/actions/reference/runners/github-hosted-runners]

| Service | Pin/health | 可执行验证结构 | Cleanup |
|---|---|---|---|
| Redis | PLAN 锁 exact image tag + digest；`redis-cli ping` health | production Redis Adapter：SET/GET、原子 TTL、过期后 miss、server restart/disconnect、有限 deadline、坏连接淘汰、恢复后新连接；run-id key prefix | 测试删除 prefix 并检查无残留；job 销毁容器 |
| MySQL | 锁 MySQL LTS exact tag + digest；`mysqladmin ping` health；synthetic job-only credential | migration Module：fresh 0→N；独立 DB 恢复 N-1 fixture→N；真实 DAO 事务/unique race/idempotent retry；四进程共享服务但 schema/run-id 隔离 | reverse teardown 后 drop run-id DB；失败也上传 migration log；job 销毁容器 |
| Mailpit | 锁 exact tag + digest；内置 `/mailpit readyz`; SMTP 1025、API 8025 | production SMTP Adapter 指向 random mapped SMTP port，secure=false/auth=none；调用业务后通过 API 按 run-id recipient 查询 exactly expected message/headers/body；验证失败、deadline、无公网 | API 删除 run-id message 或全空检查；job 销毁容器 |

MySQL 官方 image 只在 fresh data directory 执行 `/docker-entrypoint-initdb.d`，并在初始化完成前不接受连接。[CITED: https://hub.docker.com/_/mysql] 这里不把正式 migrations 隐藏在 image init：容器 health 后从 runner 调用产品 migration Module，才能同时验证 fresh 与 N-1 路径。Mailpit 官方 Docker 文档列出 1025/8025，API 可查询/读取/删除邮件，默认 SMTP 监听 1025 且无需认证/TLS。[CITED: https://mailpit.axllent.org/docs/install/docker/; CITED: https://mailpit.axllent.org/docs/api-v1/; CITED: https://mailpit.axllent.org/docs/configuration/smtp/]

**secret policy：** 使用 CI-only synthetic credential；第一步立即 mask，不能在 command line、ready log、report、manifest、artifact 中输出。GitHub `services` 在 steps 前启动，因此 PLAN 必须先做一个小型 workflow proof，确认选定表达式可在 service env/command 中获得稳定的 per-run value；若不能，停止并选择 step-managed disposable containers 或受保护的非个人 CI secret，而不是回退到个人/共享凭据。[INFERENCE: GitHub service lifecycle + locked DG-11/DG-12]

## Validation Architecture

### Lanes 与 evidence

| Lane | Runner / trigger | 内容 | Evidence / gate |
|---|---|---|---|
| Windows fast | existing hosted `windows-2022`, PR/develop | Phase 2.5 + 3B deterministic loopback/process；server Release、Qt、Varify | Required；稳定 IDs/JUnit/日志；实际 manifest 数量；cleanup failure fails |
| Linux build proof | pinned hosted Ubuntu, PR while 3C develops | configure/compile/link/run `--help`/startup proof for four apps + Qt production Modules | 必须显示同一 production target/source ownership；失败停止 Linux 扩展 |
| Linux real adapters | pinned Ubuntu + disposable services, PR/develop or protected master per runtime | fresh/N-1 migration、Redis、MySQL DG-24、SMTP | service health、migration audit、adapter reports、resource audit |
| Four-process | same Ubuntu services, master/release candidate | Gate/Status/Chat/Varify with real dependencies and synthetic dataset | topology manifest、ready timeline、ports（非 secrets）、logs、teardown report |
| 3D E2E | Ubuntu services, protected master/release candidate | two ChatServers + two clients, fixed DG-16 flow | per-step correlation/run-id、server message IDs、history/order/dedup evidence |
| scheduled robustness | Ubuntu/Windows schedule/explicit | randomized fuzz、load、soak | 非每 PR Required，但失败必须建可追踪 defect；确定性复现再进 regression |
| Artifact smoke/compat | artifact-only consumer jobs | 同一 Windows x64 set 的 N smoke 与可用时 N/N-1 | artifact ID/digest/manifest verification + matrix reports |
| Release UAT | user after smoke | versioned checklist against exact artifact | signed result, timestamp, artifact digest, environment, deviations; defect blocks |

GitHub 普通 hosted job 是 fresh VM。[CITED: https://docs.github.com/en/actions/reference/runners/github-hosted-runners] 这使跨 job 不能依赖 workspace/process state；每条 downstream lane 必须下载明确 artifact/fixture 或在同 job 建立 dependencies。

### Phase requirements → executable test map

本研究不预先创造 testcase 数量；下表给出 behavior/report family，具体 ID 和计数在对应 PLAN Wave 0 由实现后的 manifest 固定。

| Contract | Behavior | Test type | 建议 bounded command family | 当前是否具备 |
|---|---|---|---|---|
| DG-14/19/20 | Gate/Status/Chat/Qt real loopback + faults + EXE lifecycle | integration/process | Windows local runner 的定向 phase/report selector；CTest/Node 单 target | ❌ Wave 0/3B；现有局部基础可复用 |
| DG-18 | 四应用同源 Linux configure/build/start/stop | build/process | `cmake --preset <linux>` + target-specific `ctest -L ...`; `npm ci && npm test` | ❌ Wave 0/3C |
| DG-15 | fresh 0→N、N-1→N、audit、irreversible recovery | database migration | migration CLI/test target with per-run DB | ❌ 无 schema/migration |
| DG-24 | concurrent duplicate UUID returns same ID; conflict; rollback; retry/dedup | DB integration + component + E2E | MessageCommit test target + client model/transport target + 3D selector | ❌ schema/server/client chain incomplete |
| DG-11/12 | disposable Redis/MySQL/Mailpit、health、namespace、cleanup | infrastructure/integration | Linux lane coordinator | ❌ Wave 0/3C |
| DG-16 | two-server/two-client fixed business journey | E2E | one scenario driver with step-level assertions/deadline | ❌ Phase 3D |
| DG-13/21 | fixture baseline; later real N/N-1 matrix | compatibility/process | descriptor checker then artifact matrix driver | ⚠ fixtures exist; artifact baseline absent |
| DG-17/22 | build once, smoke, UAT, promote same bytes | release verification | manifest verifier + smoke runner + UAT evidence validator | ❌ Release gate |

### Sampling policy

- **每个任务：** 只跑所改 Module 的既有快速 unit/component + 新的 bounded selector，目标不是“全套每次都跑”。
- **每个 wave：** 对该 phase 的全部新 report family 跑一次，并运行 `CheckTestStructure`/runner manifest。
- **phase gate：** 对应 Required lanes 全绿，报告与 cleanup 完整；没有报告或 lane unavailable 都是失败。
- **release gate：** artifact-only smoke、适用的 N/N-1、版本化 UAT 全部指向同一 digest。

### Wave 0 gaps

- [ ] server/shared CMake targets 与 Linux preset；首次只要求 proof，不得在 proof 未通过前承诺 E2E。
- [ ] IntegrationHost factories、RunContext、portable ProcessHarness/ready probes/report schema。
- [ ] versioned schema/migration ledger、fresh fixture builder、真实 N-1 fixture bootstrap policy。
- [ ] service-container workflow proof（随机 ports、health、synthetic credential、`always()` logs/cleanup）。
- [ ] release set staging schema、machine-readable manifest/hash verifier、artifact-only consumer interface。
- [ ] 每个新增 family 的 stable test IDs/report registration；case 存在后更新实际 totals。

## Release artifact and evidence chain

现有 workflow 分别上传 server、Qt、Varify 的短期 artifacts，retention 为 7 天；它不是 DG-17 的统一发布集合，也没有让后续 smoke/UAT 消费同一 staged directory。[VERIFIED: codebase `.github/workflows/windows-ci.yml`] Release 规划必须增加一个独立“build once”边界：

```text
source SHA + locks
      │
      ▼
Windows x64 release-build (exactly once)
      │ stage one directory
      ├── Gate / Status / Chat EXE + runtime DLLs
      ├── Varify package + lockfile
      ├── config templates (no values/secrets)
      ├── canonical proto + migrations
      └── manifest.json + SHA-256 per file
      │
      ▼
immutable Actions artifact {artifact-id, artifact digest, build run-id}
      ├── artifact-only smoke
      ├── N/N-1 compatibility (when baseline exists)
      └── versioned human UAT on same digest
                     │
                     ▼
        promote same bytes to durable release asset
                     │
                     └── becomes next N-1 baseline
```

`actions/upload-artifact@v4+` 的 artifact 是 immutable，输出 artifact ID/URL/digest；同名 overwrite 会删除旧 artifact，所以 release lane 必须用 version+SHA 唯一名字且 `overwrite:false`。[CITED: https://github.com/actions/upload-artifact/blob/main/README.md] GitHub upload/download artifact actions会计算并在下载时验证 SHA-256 digest。[CITED: https://docs.github.com/en/actions/tutorials/store-and-share-data] 内部 manifest 仍必要，因为它逐文件描述内容、版本、toolchain/lock identity，并能在 artifact 转存为 Release asset 后继续验证。

**manifest 最小字段（推荐）：** schema version、release version、git SHA、build run ID、artifact ID（上传后单独 evidence 可回填，不能改变 archive bytes）、Windows/VS/CMake/Qt/Node/vcpkg baseline identity、dependency lock hashes、每个 relative path/size/SHA-256、canonical proto hashes、migration IDs/checksums、config-template list、生成时间。不得包含 secret、真实 email/token、绝对 runner path。

**N/N-1 获取：** GitHub dependency cache 只用于可重建依赖/中间物，不是发布 artifacts；cache restore 可能命中前缀或缺失，并不提供发布身份，因此 N-1 必须来自已晋升 Release asset 或保留期足够且按 artifact ID 锁定的 immutable artifact，每次下载后验证外部 digest 与内部 manifest。[CITED: https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching] 第一次 release 前显式标记“bootstrap compatibility：descriptor/wire only”，不能输出伪造的 N-1 process pass。

**保留与晋升：** 当前 7 天不足以作为长期 N-1 基线。[VERIFIED: codebase `.github/workflows/windows-ci.yml`] Release artifact 应在 UAT 通过后原样复制/晋升为按 tag 固定的 durable Release asset；原 build artifact 至少保留到 UAT/晋升结束。若仓库权限/方案支持，可用 GitHub artifact attestation 把 artifact 关联到 repository、workflow、commit SHA 和触发事件；官方使用 `actions/attest@v4`，验证可用 `gh attestation verify`，但 private repository 可用性取决于 GitHub plan，因此 attestation 是条件增强，manifest/hash 是必需合同。[CITED: https://docs.github.com/en/actions/concepts/security/artifact-attestations; CITED: https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations]

**UAT evidence 最小链：** `source SHA → build run ID → artifact ID/external digest → internal manifest hash → smoke run/report hashes → compatibility run/matrix → UAT checklist version/signer/timestamp/result → release tag/asset digest`。任一链接缺失、digest 不同、使用重新构建文件、UAT defect 未关闭都停止晋升。

## Phase dependency graph and implementation waves

```text
Phase 3A production Modules complete
       │
       ├──────────────> Phase 3B real transport/process, in-memory adapters
       │                         │
       ▼                         ▼
Phase 3C Linux same-source build + real adapters/schema/DG-24
       │                         │
       └──────────────┬──────────┘
                      ▼
        Phase 3D two ChatServer / two clients E2E
                      │
                      ▼
 Release build-once → artifact smoke/compat → signed UAT → promotion
                      │
                      └── first promotion creates full N-1 baseline
```

3B 可先于大部分 3C dependency work，但必须依赖 Phase 3A 真实 production Modules。3D 同时依赖 3B 的 transport/process harness 与 3C 的 Linux、real adapters、schema、DG-24 commit chain。Release artifact staging 可与后期 3C 并行设计，但晋升 gate 必须等 3D 及适用兼容证据。

### Phase 3B 建议 PLAN 切片

1. **3B-00 — contract/Wave 0：** 冻结 IntegrationHost/ProcessHarness/RunContext Interfaces；建立 report/ID registration；证明 tests 链接 production targets。退出条件：Phase 3A Module 尚未成为可组合 target 则停止。
2. **3B-01 — process infrastructure：** portable lifecycle abstraction先落 Windows Adapter、bounded ready/log/stop/kill escalation/resource audit；保护正式 EXE config/start/ready/stop。
3. **3B-02 — Gate + Qt HTTP：** production Beast Gate transport + business Module + memory adapters；production QNAM client；覆盖 DG-20 相应 HTTP cases。
4. **3B-03 — Status gRPC：** production service/stub、business Module、memory adapters；deadline/cancel/refused/late completion/port conflict。
5. **3B-04 — Chat + Qt TCP：** production acceptor/session/frame codec 与 QTcpSocket；fragment/coalesce/max/malformed/interrupted read/write/backpressure/cleanup。
6. **3B-05 — composition gate：** 三个正式 C++ EXE 与 Varify 正式 main 的黑盒 lifecycle；`CheckTestStructure` 防 fake composition；注册 Windows Required lane，按实际报告更新 totals。

3B non-service stop condition：任何测试开始真实 Redis/MySQL/SMTP 或公共网络即越界到 3C。

### Phase 3C 建议 PLAN 切片

1. **3C-00 — Linux build spike：** 顶层 CMake/presets + vcpkg/Qt toolchain 选择；四应用 configure/compile/link/start proof。必须记录每个平台 guard 和 dependency blocker。未证明同源不得继续称 Linux-ready。
2. **3C-01 — shared build ownership：** 正式 Windows/Linux executables 与 tests 链接同一 production targets；Win32 process/signal/path 细节移入 platform Adapter；Windows release gate保持全绿。
3. **3C-02 — migration Module：** version ledger/checksum、0→N migrations、fresh synthetic dataset、不可逆 metadata/recovery；建立“首次 release 前无 N-1 schema”的 bootstrap 诊断。
4. **3C-03 — Redis real Adapter：** disposable service、atomic TTL、timeout、disconnect/restart/pool cleanup；统一或至少共享连接 policy，避免三套不一致 lifecycle。
5. **3C-04 — MySQL real Adapter + DG-24：** 修复 joinable pool lifetime；显式 transaction；持久化 UUID/unique constraint；created/existing/conflict；fresh and N-1 migration tests；并发重复。
6. **3C-05 — SMTP sink：** Varify runtime config、production SMTP Adapter 指向 Mailpit、API evidence、failure/deadline/no-secret checks。
7. **3C-06 — four-process topology：** 一个 RunContext 启 Gate/Status/Chat/Varify + services，协议 ready、synthetic core flow、reverse teardown、always-upload evidence。此时才可称真实依赖 Integration。
8. **3C-07 — compatibility bootstrap：** 保留 descriptor/wire；定义 artifact resolver/manifest verifier/schema fixture interface；没有 full N-1 时稳定 skip/block reason，不伪造 pass。

### Phase 3D 建议 PLAN 切片

1. **3D-00 — deterministic fixture/scenario DSL：** versioned production-shape users/relationships/messages；RunContext 为双 server/双 client 分配 endpoints/namespace。
2. **3D-01 — topology/ready：** 两个 ChatServer 注册不同 server identity，两个 production-protocol client drivers 分别登录；证明实际跨实例路由，不允许同 server 降级。
3. **3D-02 — identity/friend journey：** 验证码→注册→Gate login/service discovery→Chat token login→friend apply/accept，逐步断言 DB/Redis/API-visible state。
4. **3D-03 — private chat cross-instance：** 发送、commit 后跨实例投递、两端 stable `message_id`/UUID mapping、授权/错误路径。
5. **3D-04 — reconnect/history/idempotency：** commit 后断 ACK、原 UUID retry、返回同 ID；notification/history 重复；client dedup；分页边界、order；server restart/route recovery。
6. **3D-05 — compatibility matrix：** full N-1 artifact 存在后才启 N-1 client↔N server、N/N-1 peer RPC；unsupported combination fail-fast。首个 release 只输出 bootstrap 状态。

3D 的“客户端”推荐使用链接生产 client transport/session/model Modules 的 test driver，而非 GUI 像素自动化；若产品必须验证 GUI 操作，应另增薄 UI UAT，不把它替代 protocol/business E2E。[INFERENCE: DG-16 plus current GUI-only main]

### Release gate 建议 PLAN 切片

1. **R-00 build once：** Windows x64 unified staging、config templates、proto/migrations、manifest/per-file hashes；唯一 artifact name，上传后记录 artifact ID/digest。
2. **R-01 artifact-only automation：** downstream job 只 download/verify，运行 smoke 和适用兼容矩阵；禁止 checkout 后重建被测 EXE。
3. **R-02 UAT：** 版本化 checklist 与 evidence validator；用户只在同一 digest 上签署；defect 阻断并回归到正确分支 gate。
4. **R-03 promotion/baseline：** 原字节晋升 durable Release asset；再次 hash verify；登记为下一轮 N-1 artifact/schema baseline。

## Standard Stack（prescriptive）

不为这些阶段引入新的 application framework；保留仓库已锁的生产依赖与 lockfiles。Linux 路径的任务是证明相同版本/targets 的平台支持，失败则显式评审 version change，不能顺手追 latest。

| Layer | Required stack | Version policy | Provenance |
|---|---|---|---|
| C++ build | CMake target model + repo vcpkg manifest/toolchain | 锁 baseline 与 triplets；CI 固定 CMake/compiler image | [VERIFIED: codebase manifest/workflow; CITED: Microsoft/CMake official docs above] |
| C++ test | existing GoogleTest/CTest + existing report/runner governance | 当前 installed GTest 1.17.0；不因 phase 升级 | [VERIFIED: codebase installed status and tests] |
| transports | Boost.Asio/Beast, gRPC/protobuf, Qt Network | 保留 repo locks；production targets single-source | [VERIFIED: codebase projects/CMake/vcpkg status] |
| data | MySQL Connector/C++ JDBC API, hiredis, ioredis | 保留 Connector 9.1/current lock; changes require isolated compatibility proof | [VERIFIED: codebase package locks/status/source includes] |
| Varify | Node 22 lane + `npm ci`, current `package-lock.json` | exact lock is authority | [VERIFIED: codebase workflow/package-lock] |
| ephemeral services | official Redis/MySQL images + Mailpit SMTP sink | 每个 image exact tag+digest；digest在 PLAN implementation 时记录 | [CITED: https://hub.docker.com/_/mysql; CITED: https://mailpit.axllent.org/docs/install/docker/] |
| artifacts | pinned `actions/upload-artifact@v4` / download action + SHA-256 manifest | action pin、artifact unique identity、no overwrite | [VERIFIED: codebase workflow; CITED: GitHub artifact docs above] |

### Varify package legitimacy audit

当前 lock 精确解析 `@grpc/grpc-js 1.14.3`、`@grpc/proto-loader 0.8.0`、`ioredis 5.10.1`、`nodemailer 8.0.6`、`uuid 8.3.2`；registry 查询时这些包均存在且未发现 `postinstall`。这是只读核验，不是升级建议。[VERIFIED: npm registry + codebase `VarifyServer/package-lock.json`]

| Package | Locked | Legitimacy seam | Disposition |
|---|---:|---|---|
| `@grpc/grpc-js` | 1.14.3 | OK；官方 grpc-node repo signal | 保留 lock |
| `@grpc/proto-loader` | 0.8.0 | OK；官方 grpc-node repo signal | 保留 lock |
| `ioredis` | 5.10.1 | SUS（seam 以 latest release “too-new” 标记；非指当前 lock 恶意） | 保留 lock；任何 refresh 前 human verify |
| `nodemailer` | 8.0.6 | SUS（同上） | 保留 lock；任何 refresh 前 human verify |
| `uuid` | 8.3.2 | SUS（同上） | 保留 lock；任何 refresh 前 human verify |

没有建议新增 npm package，也没有 SLOP package。`npm ci` 必须使用现有 lock；如果 Linux work 迫使 lock 变化，PLAN 增 `checkpoint:human-verify`，重新跑 legitimacy、registry、postinstall 与 test evidence。

## Don't Hand-Roll

| Problem | 不要自造 | 使用/复用 |
|---|---|---|
| production/test dependency switching | fake EXE flags、global singleton reset hook | Phase 3A constructor/factory Interfaces + two Adapters |
| Linux build | 复制一套 Linux source/算法或 shell 编译清单 | CMake production targets + vcpkg imported targets |
| message idempotency | process-local UUID set/Redis-only lock/“exactly once”说法 | MySQL unique constraint + explicit transaction + retry/dedup contract |
| schema lifecycle | CI 中一份 ad-hoc final SQL | versioned application migration Module/checksum ledger |
| SMTP test | 公共 SMTP/真实账户/只 mock `sendMail` | production nodemailer Adapter + disposable Mailpit + API evidence |
| process waiting | sleep/poll PID/log substring alone | protocol-ready probes + bounded ProcessHarness |
| artifact identity | 文件名/Actions cache/重建旧 SHA | artifact ID/digest + internal manifest + durable Release asset |
| service cleanup | 依赖 happy-path finally | RunContext teardown ledger + `always()` evidence/cleanup + job container lifecycle |
| compatibility | descriptor fixtures 冒充 N-1 binaries | fixture bootstrap then actual promoted N-1 artifact matrix |

## Common Pitfalls and Stop Conditions

| Pitfall | Warning sign | Prevention / stop |
|---|---|---|
| 把“编译过部分 Qt”称四应用 Linux portable | 没有四 executable link/start evidence | 3C-00 proof 失败即停止扩展，记录 blocker |
| target 双份源清单漂移 | test target 与 EXE 手列不同 `.cpp` | production library target单一 owner；structure check |
| `msg_uuid` 只在 response 中存在 | DB row/history/peer RPC 无 UUID | schema unique + commit interface + end-to-end assertions |
| 隐式事务提交 | defer `setAutoCommit(true)`、catch 无 rollback | explicit transaction guard；fault injection 验证零 partial commit |
| 信任 payload sender | `from_uid` 可与 session UID 不同 | authenticated principal owns unique key/access checks |
| detached thread/acceptor 未关闭 | teardown 偶发端口占用或 UAF | joinable lifecycle + bounded resource audit；cleanup failure gates |
| fixed ports 并行冲突 | Varify 50051、shared config | per-run dynamic ports; explicit conflict test only |
| service “started”但未 ready | connection refused/flaky sleep | Docker health + application protocol ready |
| MySQL init script掩盖 migration | fresh works但 N-1 runner不同 | health 后调用同一 migration Module |
| SMTP test触公网 | qq.com host/credentials | Mailpit config; network egress不作为成功条件 |
| cache当 release | N-1 cache miss/restore近似 key | immutable Release asset，hash verify |
| overwrite artifact | 同名 artifact identity变化 | unique version+SHA, no overwrite |
| 失败日志被 cleanup 覆盖 | only teardown exception visible | preserve primary failure + separate cleanup report，两者任一 fail |
| 预先承诺 test count | PLAN 写目标总数但 case 未落地 | 只承诺 behavior/IDs；merge时用实际 manifest更新 |
| 首发伪造 N-1 | 从旧源码重建自称 release | 显式 bootstrap；第一次晋升后开始 full matrix |

## Risks and explicit planning decisions still needed

1. **Linux Qt toolchain exact version（MEDIUM）：** 仓库 Windows 锁 Qt 6.5.3/MinGW；Qt 官方当前 Linux requirements 页面描述受支持 Linux/X11 依赖，但这不证明仓库锁版本在 hosted Ubuntu 有现成 binary。[CITED: https://doc.qt.io/qt-6/supported-platforms.html; CITED: https://doc.qt.io/qt-6/linux-requirements.html] 3C-00 必须锁获取方式与版本；若 Linux 只构建 headless production Modules而不构建 GUI EXE，需要合同明确是否满足“四应用”中的 chat client 范围。
2. **Service credential expression（MEDIUM）：** GitHub services 在 steps 前创建；per-run secret 生成时机与 `services.env/command` 可用 context 需要小型 workflow proof。失败时采用 step-managed disposable containers或受保护 CI-only secret，绝不个人凭据。[INFERENCE: official service lifecycle]
3. **3D client driver boundary（MEDIUM）：** 当前 Qt 只有 GUI main；建议 driver 链接 production transport/session/model targets。PLAN 需要明确“两个客户端”是否要求可见 GUI；默认不做 GUI 自动化。
4. **Batch atomicity（MEDIUM）：** DG-24 未明确一个 multi-message batch 部分成功语义；本研究推荐 whole-batch atomic，避免 ACK 丢失后的部分状态模糊。锁 PLAN 前应把它写成 contract。
5. **Current authentication security（HIGH code finding）：** 当前 handler 对 payload sender 的信任以及密码/Token实现需要独立 security review；本阶段可验证既定 flows，但不得因此宣称互联网生产安全。[VERIFIED: codebase Gate/Chat auth handlers and DAO]
6. **Artifact retention/plan permissions（MEDIUM）：** attestation/private retention 能力取决于 repository plan。manifest/hash/release asset 是无条件最小路线；attestation 只能条件启用。[CITED: GitHub attestation docs above]

## Environment Availability

本机只是 Windows 开发环境，不是 DG-23 权威 Linux evidence。只读 probe 结果：[VERIFIED: local command probe 2026-08-30]

| Dependency | Local availability | Version / note | Planning fallback |
|---|---|---|---|
| CMake | ✓ | 4.3.1 | CI 必须另锁 version，不能引用本机作为证据 |
| Node/npm | ✓ | Node 24.18.1 / npm 11.16.0 | 现有 CI 是 Node 22；以 workflow lock 为准 |
| Git | ✓ | 2.51.0.windows.1 | — |
| Qt | ✓ | qmake at Qt 6.5.3 MinGW | 只证明 Windows install，不证明 Linux availability |
| MySQL client | ✓ | local MySQL 8.0 path | 不使用本机 shared DB；CI service container authoritative |
| Docker | ✗ | command absent | 不阻断研究/本地 Windows 3B；3C 由 hosted Ubuntu services |
| MSBuild / `cl` in PATH | ✗ | 未在当前 shell PATH | 现有 workflow通过 VS setup；不运行本地 build |
| Redis CLI | ✗ | absent | CI Redis image内 health client/runner tooling |
| Linux VM/WSL | command present, runtime未探测 | 非 Required | hosted Ubuntu is authoritative |

**Missing with no local fallback：** Docker、Redis service、本 shell MSBuild 不影响本研究，但本机不能提供 3C 权威 evidence。

**CI prerequisite：** hosted Ubuntu image、Docker service-container capability、exact service digests和 action pins必须在 implementation Wave 0 probe 中实际验证。

## Runtime State Inventory（migration/release）

| Category | Items found | Required action |
|---|---|---|
| Stored data | 仓库代码假定 MySQL tables/stored procedure、Redis session/route/code keys，但没有可审计 schema/migration/data fixture。[VERIFIED: codebase SQL/Redis key searches; negative `.sql` inventory] | code edit：定义新 schema/migrations；data migration：首次 N-1 baseline 后迁移既存 rows，给 UUID backfill/nullability/unique rollout 明确步骤；旧 row 无 client UUID 时不得伪造发送者 supplied UUID。 |
| Live service config | Git 内 config templates/current INI shape 含固定 section/ports；真实部署外部配置不在仓库可见范围。[VERIFIED: codebase config loaders/templates; external state unavailable] | 保持 config precedence，新增 host/port/timeout/SMTP options；发布前由用户确认部署配置迁移。不得读取或复制真实 secret 值。 |
| OS-registered state | 未发现仓库声明的 Windows service/systemd/Task Scheduler registration。[VERIFIED: negative repository search] | repo plan 无自动 OS migration；release UAT 明确“外部 registrations 未审计”，由部署 owner确认。 |
| Secrets/env vars | Varify 读取 SMTP/Redis env，GitHub workflow有 CI env；真实 secret store不可由仓库只读研究验证。[VERIFIED: codebase Varify modules/workflow] | code edit只改 key contract/config validation；CI 使用 synthetic/masked values；若 production env names变化，需独立 operator checklist，不把 secret写进 artifact。 |
| Build artifacts/installed packages | 当前 Windows CI 是多个短期 zip；本地有 vcpkg installed tree；没有统一 release baseline。[VERIFIED: codebase workflow/status] | 不迁移本地 installed tree；从 locks重新构建一次统一 set。第一次晋升后将其登记为 N-1，旧临时 zips不能冒充。 |

**Canonical migration question：** 即使 repo 文件全部更新，外部 MySQL schema/data、Redis volatile keys、secret/config store、OS registrations 与已发布 binaries仍不会自动变化。PLAN 必须把 code edit、data migration、operator/release step 分开，不以 source grep 完成代替 runtime migration。

## Security Domain

### Applicable ASVS categories

| ASVS area | Applies | Required control in these plans |
|---|---|---|
| V2 Authentication | yes | 验证码/login/token真实 flow；sender identity只从 authenticated session进入 business Module；synthetic credentials。 |
| V3 Session Management | yes | Redis token/session TTL、断线/重登、expiry/fail-closed、跨 server route cleanup。 |
| V4 Access Control | yes | chat membership、recipient/friend relationship、authenticated sender与 request fields一致；禁止越权 history/send。 |
| V5 Validation | yes | HTTP JSON、protobuf、TCP frame length、UUID/content/page-size/config bounds；畸形输入稳定失败。 |
| V6 Cryptography | yes at release boundary | 不手写 crypto；CI loopback明示测试环境。TLS/password storage若不在本阶段修复，release claim必须限定，不能宣称公网 production-ready。 |
| V8 Data Protection | yes | synthetic-only；logs/reports/manifest/UAT 不含 password/token/code/real email/secret；bounded log retention。 |
| V9 Communications | yes | CI service端口只暴露 fresh runner；公网 SMTP禁止；真实部署 TLS 是独立明确合同，不能被 loopback smoke隐式“通过”。 |
| V12 Files/Resources | yes | artifact extraction/path validation、relative manifest paths、temp ownership、cleanup、no secret config。 |
| V13 API/Web Service | yes | deadlines、replay/idempotency、unsupported version fail-fast、stable error diagnostics。 |

### Threat patterns and mandatory mitigations

| Pattern | STRIDE | Mitigation / evidence |
|---|---|---|
| payload `from_uid` spoof | Spoofing/Elevation | authenticated principal owns sender; negative cross-user tests |
| UUID replay/race | Tampering/Repudiation | DB unique constraint + explicit transaction; concurrent duplicate returns same ID; mismatched replay conflicts |
| SQL injection / malformed inputs | Tampering | prepared statements plus validation; current inserts are prepared but schema/membership semantics仍需补。[VERIFIED: codebase `MysqlDao.cpp`] |
| secret/PII in logs/artifact | Information disclosure | structured redaction, synthetic data, manifest allowlist; failure-path scan |
| frame/queue/connection exhaustion | Denial of service | max lengths, finite queues/timeouts, deadline/cancel, resource release tests |
| stale Redis route/session | Spoofing/DoS | TTL + restart/disconnect tests + fail-closed business semantics |
| artifact replacement/rebuild | Tampering/Repudiation | immutable artifact ID/digest, internal hashes, no overwrite/rebuild, UAT chain |
| unsupported N/N-1 silent parse | Tampering/Data loss | version negotiation/diagnostic fail-fast; actual process matrix |

本文未对完整 ASVS compliance 做认证；上述是 Phase 3B～Release 与当前 stack直接相关的 planning controls。[INFERENCE: phase scope]

## Non-goals

- 不制作或发布 Gate/Status/Chat/Varify application Docker images，不设计 Kubernetes/production orchestration。
- 不新增 self-hosted Required runner，不把本地 Linux VM、MobaXterm 会话或个人电脑作为证据。
- 不承诺 Linux 正式发布支持；只建立同源 CI build/run 与 Integration/E2E 平台。
- 不扩大默认兼容到 N-2 或更老版本；首个 release 前不伪造 N-1 process/schema。
- 不把 fuzz/load/soak 放入每个 develop PR；只建立 scheduled/explicit lane边界。
- 不重写协议、认证、密码体系或新增 TLS 产品范围；发现的安全阻断应单独决策，不能在测试计划中暗中扩 scope。
- 不升级 C++/Node/Qt dependencies，除非 Linux spike证明当前 lock不可用并经过显式合同/legitimacy/compatibility评审。
- 不做 GUI 像素自动化来替代 production client transport/session/model E2E。
- 不使用真实用户数据、真实邮件、个人 token/password、共享数据库或公共 SMTP。
- 不在 PLAN 中预先承诺新 testcase/report总数；只承诺 behaviors、stable IDs、evidence，落地后更新真实 manifest。

## Assumptions Log

本文没有把未核验训练知识写成实施事实。以下仅是需要在 PLAN/implementation proof 锁定的显式推断，不是已核验的 package/platform 事实：

| # | Inference | Risk if wrong | Required resolution |
|---|---|---|---|
| I1 | whole-batch atomic 是 DG-24 最安全语义 | 产品期望 partial success 时 API/result schema不同 | PLAN前锁 batch contract |
| I2 | 3D client driver链接 production Qt modules即可满足“两客户端” | 用户可能要求 GUI-driven client | PLAN scope checkpoint；GUI只作为独立薄 UAT |
| I3 | GitHub services expression可提供可接受的 CI-only per-run credential | context限制可能阻止 | 3C Wave 0 workflow proof；失败用 step-managed disposable containers/CI-only secret |
| I4 | 当前 vcpkg baseline所有 ports可在锁定 Ubuntu/triplet构建 | MySQL/Qt/platform port可能失败 | 3C-00 compile/link spike；失败触发版本合同评审 |

## Open Questions for the planner

1. **Batch atomicity：** 正式锁 whole-batch atomic，还是定义 per-message partial result？研究推荐前者。
2. **Qt Linux exact toolchain：** 锁 Qt version、获取方式、headless platform plugin和GUI EXE是否纳入四应用 proof。
3. **3D client definition：** production-module protocol driver 是否是 Required E2E；GUI manual/UAT是否另列。
4. **Repository plan/retention：** 是否支持 artifact attestation、需要多长 UAT window、Release asset权限由谁持有。
5. **First N-1 bootstrap：** 首次晋升的 version/tag、schema fixture生成/去敏/签名 owner。
6. **Production security claim：** 当前 auth/password/TLS gaps是否阻断“release”这一标签，或此次 release明确限定受控环境。此项不能由测试研究自行决定。

## Sources

### Primary codebase sources（HIGH）

- `tests/plans/PHASE-3B-RELEASE-DECISIONS.md` — DG-09..24 and stop conditions.
- `tests/plans/PHASE-3A-PLAN.md`, `tests/plans/PHASE-3A-TEST-PLAN.md` — upstream production Module/test seams.
- `tests/CI-GOVERNANCE.md`, `tests/REGRESSION.md`, `tests/TEST-CONTRACT-MATRIX.md`, `tests/plans/PHASE-2.5-07-SUMMARY.md` — gates, report/test baseline, compatibility policy.
- `.github/workflows/windows-ci.yml`, `scripts/windows-local.ps1`, `tests/CheckTestStructure.cmake` — current Windows jobs, actions/toolchains, runner/report contracts.
- GateServer/StatusServer/ChatServer/VarifyServer/chat production sources, project/build files, proto/generated assets, tests and package locks — entrypoints, transports, adapters, schema/idempotency gaps.
- `C:/Users/Lenovo/.codex/skills/codebase-design/SKILL.md`, `DEEPENING.md` — Module/Interface/seam/depth design vocabulary.
- `AGENTS.md` — absent at repository root; no additional project directives found.[VERIFIED: negative file check]

### Official external sources（MEDIUM; current platform facts）

- [GitHub service containers](https://docs.github.com/en/actions/tutorials/use-containerized-services/use-docker-service-containers) — lifecycle, Ubuntu requirement, port mapping, health options.
- [GitHub Redis service example](https://docs.github.com/en/actions/tutorials/use-containerized-services/create-redis-service-containers) — Redis health pattern.
- [GitHub-hosted runners](https://docs.github.com/en/actions/reference/runners/github-hosted-runners) — fresh VMs and current labels.
- [GitHub artifacts](https://docs.github.com/en/actions/tutorials/store-and-share-data), [upload-artifact v4](https://github.com/actions/upload-artifact/blob/main/README.md) — digest and immutability.
- [GitHub dependency caching](https://docs.github.com/en/actions/concepts/workflows-and-actions/dependency-caching) — cache/artifact distinction.
- [GitHub artifact attestations](https://docs.github.com/en/actions/concepts/security/artifact-attestations), [attestation how-to](https://docs.github.com/en/actions/how-tos/secure-your-work/use-artifact-attestations/use-artifact-attestations) — provenance and permission/plan conditions.
- [CMake `add_library`](https://cmake.org/cmake/help/latest/command/add_library.html), [vcpkg CMake integration](https://learn.microsoft.com/en-us/vcpkg/users/buildsystems/cmake-integration), [vcpkg manifest mode](https://learn.microsoft.com/en-us/vcpkg/consume/manifest-mode) — shared targets/toolchain/manifest route.
- [Qt supported platforms](https://doc.qt.io/qt-6/supported-platforms.html), [Qt Linux requirements](https://doc.qt.io/qt-6/linux-requirements.html) — Linux support scope and dependency caveat.
- [MySQL Connector/C++ usage](https://dev.mysql.com/doc/dev/connector-cpp/latest/usage.html), [MySQL transactions](https://dev.mysql.com/doc/refman/8.4/en/commit.html), [autocommit](https://dev.mysql.com/doc/refman/8.4/en/server-system-variables.html), [non-rollbackable statements](https://dev.mysql.com/doc/refman/8.4/en/cannot-roll-back.html), [unique indexes](https://dev.mysql.com/doc/refman/8.4/en/create-index.html), [upgrade backup](https://dev.mysql.com/doc/refman/8.4/en/upgrade-before-you-begin.html), [dump upgrade testing](https://dev.mysql.com/doc/refman/8.4/en/mysqldump-upgrade-testing.html).
- [Redis `SET`](https://redis.io/docs/latest/commands/set/), [hiredis guide](https://redis.io/docs/latest/develop/clients/hiredis/) — atomic expiry and client context.
- [Official MySQL image](https://hub.docker.com/_/mysql), [Mailpit Docker](https://mailpit.axllent.org/docs/install/docker/), [Mailpit API](https://mailpit.axllent.org/docs/api-v1/), [Mailpit SMTP](https://mailpit.axllent.org/docs/configuration/smtp/) — disposable service behavior and evidence interface.

## Metadata and confidence

| Area | Level | Reason |
|---|---|---|
| Current build/code blockers | HIGH | Direct source/build/project/negative inventory; no build was run. |
| Module/seam architecture | HIGH | Locked DG contracts + Phase 3A plans + required codebase-design vocabulary. |
| DG-24 gap and recommended transaction model | HIGH for gap, MEDIUM for final batch semantics | Code and MySQL docs verify gap/risk; batch atomicity remains a product contract. |
| GitHub runner/services/artifacts | MEDIUM | Current official docs verified on 2026-08-30; platform is time-sensitive. |
| Linux dependency viability | LOW until spike | Current locks/targets known, but no Linux compile/link execution was authorized. |
| Validation/release waves | HIGH as planning recommendation | Directly derived from locked dependencies and current governance. |

**Valid until：** 2026-09-06 for GitHub runner/action/service-image facts; code findings remain valid until the cited sources change.

**Research status：** Ready for formal planning, subject to the six explicit open questions and Wave 0 proofs.
