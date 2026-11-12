# CI 测试清单

本清单按 2026-11-08 工作区中的 GitHub Actions 配置整理，用于优化前核对覆盖范围。下列数量是运行器要求的用例数。执行耗时、失败率和缓存命中率需从具体运行记录取得。

## 触发与执行范围

| 事件 | 模式 | 执行下文哪些类别 |
| --- | --- | --- |
| 向 develop 提 PR 或推送 | quick | 1–6。若整个变更仅涉及根目录 README.md、WINDOWS_BUILD.md 或 docs/ 下的 Markdown，只执行 1、5、6 |
| 向 master 提 PR | full | 1–10，生成 Windows 应用 ZIP |
| 向 master 推送（含合并） | full | 1–10，通过后执行 14 |
| 每周一北京时间 03:17，默认分支 develop | full | 1–10，并执行 13 中的 Windows 工具链刷新 |
| 手动 quick / full | 默认 full | 同模式的常规回归；可勾选 11 或 13，勾选后 quick 提升为 full。Windows 工具链刷新仅限默认分支 |
| 手动 performance | 独立运行 | 仅执行 12，可选 smoke、baseline、stress |

入口与规则见 [CI 工作流](../.github/workflows/ci.yml)、[模式选择](../scripts/ci/ciPolicy.js)、[文档变更范围判定](../scripts/ci/ciScope.js)。

## Windows 回归

Windows 的 static-check 作业包含第 1、5、6 类。其成功后，服务端、Qt 和 Varify 三个作业可以并行。四条模块回归通道共要求 **13 份报告、383 条用例**；第 1、6 类另行校验。配置见 [Windows 工作流](../.github/workflows/windows-ci.yml) 和 [报告注册表](../scripts/windows-local.ps1)。

### 1. 静态配置与 CI 自检

- 验证增量规范检查器，再检查本次引入的 Git、命名和注释规范问题。
- 用 actionlint 检查工作流；测试分支触发、文档分流、执行预算和报告校验规则。
- 检查解决方案只有一个正式 ChatServer 项目，解析各 MSBuild 项目 XML，排查旧实例路径和旧静态 triplet。
- 核对 vcpkg 基线、必需依赖，以及 x64、动态链接、Release-only 设置。
- 检查测试源、CTest 标签、Node 文件清单、PowerShell 用例与模块说明是否按规则注册。
- 测试 GitHub 资产下载、二进制缓存刷新和原生工具链快照恢复。
- 检查纯服务端 CMake 配置，并覆盖启用验收资源的配置变体。
- 验证双服 E2E、控件和首版验收的证据校验逻辑。真实服务和控件场景分别在第 9、11 类执行。
- 对 master PR 和 push，额外检查待发布版本是否已经发布。

### 2. C++ 服务端

作业 servers-release 调用 RunServerTests。6 份报告覆盖通用单元、组件、集成、Chat gRPC 集成、Gate 单元和资源存储集成。各组精确数量由 `$regressionReportGroups` 校验。[模块与报告映射](../tests/server/README.md)

- 协议与消息：验证帧编解码、消息分发、回执处理，以及 protobuf 兼容性和生成文件漂移。
- Gate：验证注册、登录、重置密码请求的编排、依赖失败时的响应，以及请求到响应的交付。
- Gate 选服与签发涵盖实例租约、负载并列和写入失败。Chat 与 Resource 通过 Redis 校验当前凭据。
- Chat：验证会话注册与发送状态、Redis 连接池生命周期、异步处理与关闭。
- 本机通信：用回环连接测试协议和生产 gRPC 客户端，包含 C++ 到 Node Varify 的调用。
- 进程生命周期：测试命令行、配置加载、端口绑定、就绪与关闭；运行器检查子进程回收。
- ResourceStore：用临时文件测试上传归属、重启续传、内容完整性和路径校验。Windows 常规报告覆盖存储层。
- 构建产物：编译 Gate、Chat、Resource，核验各服务独立目录及运行库；full 额外生成各自的 ZIP。

### 3. Qt 客户端

作业 client-release 调用 RunClientTests。单元 25、组件 20、集成 34，共 **3 份报告、79 条**。[Qt 模块清单](../chat/tests/README.md)

- 网络收发：测试 TCP 半帧、粘包、字节序、空包和长度边界，以及真实本机 HTTP/TCP 交互。
- 登录与退出：测试 HTTP、TCP、Chat 登录结果的传递、重复事件和异常重置；退出后检查账号状态、待处理批次和页面模型清理。
- 消息显示：测试模型规则、分页状态、delegate 布局、列表滚动和底部通知。
- 消息保存与重试：测试 SQLite 持久化、回执处理，以及重新认证后重发待确认消息。
- 输入与搜索：测试输入草稿、提交落盘、JSON 字节预算和搜索请求与结果的匹配。
- 客户端进程：测试独立客户端进程登录、控制通道生命周期和限时关闭。full 额外部署 Qt 运行库并生成客户端 ZIP。

### 4. Node 验证码服务

作业 varify-release 调用 RunVarifyTests。单元 35、集成 22，共 **2 份报告、57 条**。[Varify 模块清单](../VarifyServer/test/README.md)

- 检查 JavaScript 语法、锁定依赖树、协议错误码和 gRPC 服务描述。
- 测试注册与重置密码使用独立的验证码键，拒绝无效用途。
- 同一邮箱 12 个并发请求只生成和发送一次；验证冷却期复用、消费后重发和日志脱敏。
- 测试 Redis 延迟建连、带 TTL 的原子写入、失败与关闭；测试 SMTP 配置、截止时间和本机故障连接。
- 用独立子进程验证配置读取、启动状态和端口已占用时的失败处理；用本机 gRPC 验证真实注册和路由。
- 测试 TLS 通道、未知 CA、主机名不匹配、大响应背压和绑定失败后的回滚。

### 5. ChatServer 多实例管理脚本

static-check 内调用 RunScriptTests。配置组件 9、进程集成 4，共 **2 份报告、13 条**。[配置用例](../tests/scripts/validation/README.md) · [进程用例](../tests/scripts/lifecycle/README.md)

- 拒绝重复的实例名、日志名和配置文件名。
- 规范化端口后检测冲突，覆盖 TCP 与 RPC 占用同一端口。
- 拒绝不安全的配置文件名、缺少的配置文件、启动参数和必填值。
- 验证启动失败、进程状态和冲突处理；通过 PID 身份校验保护进程操作。

### 6. 发布打包规则

static-check 用临时 ZIP 运行发布合同，输出 release_candidate_contracts.xml。实际发布包的启动检查见第 14 类。[打包合同](../tests/release/contracts/README.md)

- 检查输入缺文件、文件内容被替换、摘要错误和版本不一致。
- 检查运行配置清理、协议文件位置和包内布局。
- 检查失败报告、草稿重试及已发布版本保护。
- 测试 MySQL 工具路径解析，覆盖 Windows 临时目录短文件名。

## Linux 全量回归

第 7 类包含两个可并行的前置作业。两者通过后，第 8、9 类分别启动自己的临时服务并行运行，第 10 类汇总证据。[Linux 工作流](../.github/workflows/linux-ci.yml)

### 7. 构建预检与 POSIX 进程

- posix-process-contracts 运行真实子进程，检查生命周期与回收，可独立于数据库执行。
- linux-preflight 检查编译器版本、恢复依赖、配置、编译、链接和启动；另跑 schema 迁移脚本的合同测试。
- 显式关闭 Qt 和客户端构建，单独编译 Gate、Chat 两个服务。
- 测试共享生产目标和进程监督器，验证监听端口被占用，以及主动关闭后重新绑定。
- 构建同一提交的启动器、驱动和服务运行包，为后续作业提供带 SHA 和摘要的产物。

### 8. 真实依赖与多进程集成

disposable-services 使用临时 MySQL、Redis、Mailpit，运行 3C-07。[适配器和多进程用例表](../tests/services/README.md)

- MySQL：验证从空库迁移、注册数据及消息持久化；迁移、消息报告分别要求 12 和 20 项。
- Redis：分别运行 C++ hiredis 的 9 项与 Node ioredis 的 6 项适配器检查，覆盖真实数据操作和故障恢复。
- SMTP：运行 12 项适配器检查，通过 Mailpit 验证邮件投递及故障行为。
- 多进程联动共 18 项：检查 Varify、Chat、Gate 就绪和 TCP 登录拒绝码，从邮件取得验证码，注册、登录、选服，经 TCP 认证后私聊，并核对消息持久化和重试 ID。
- 故障注入：重复绑定 Gate 端口，令 SMTP 或 Redis 中断后恢复，验证断开客户端后的 Redis 实例计数归零。
- 关闭与回收：用 MySQL 表锁阻塞 Gate 读取，检查进程能在时限内关闭；最后检查进程、监听端口和临时数据清理。

### 9. 双 ChatServer 端到端业务

two-server-contract 使用真实依赖、两个 ChatServer 和独立 Qt 客户端，常规 full 要求 **33 条**当前版本用例。[3D 业务场景](../tests/services/README.md#phase-3d-foundation)

- 基础拓扑：检查真实服务发现、两个客户端的账号和进程隔离、实际连接的服务实例。
- 账号与好友：验证错误验证码或密码、重复注册、跨账号 Token 被拒；验证跨实例好友申请、重复申请、接受，以及重复接受后的数据一致性。
- 跨服收发：双向发送 Unicode 消息，核对发送者范围内的 UUID、发送中及完成后的重试、同 UUID 内容冲突。
- 权限：拒绝无效会话、非成员发送或拉取历史，以及伪造发送者。
- 离线历史：发送 21 条消息后重启客户端，跨至少三页拉取，与 MySQL 逐条核对 ID、发送者、UUID、内容摘要和顺序；另测重叠页、末尾空页和无效游标。
- 异常恢复：丢弃已提交消息的确认并断线，重连后重放原请求；注入重复通知，重启 Chat 实例，检查服务发现恢复且跨服消息仍可交付。

### 10. 集成报告与清理校验

- downstream-contract 测试失败结果能否向上传递。
- 下载本次 3C、3D 作业报告，核验源码 SHA、摘要及必需场景。
- 缺报告、用例失败或跳过、摘要变化、清理未完成，均使校验失败。
- 顶层 Regression checks 汇总 Windows；Full regression checks 汇总 Windows 与 Linux；显式请求 11 时，Real environment acceptance 另行检查本次双平台结果。

## 专项与发布

### 11. 四账号真实环境验收

手动启用 real_acceptance，在既有 3D 环境增加生产 ResourceServer。额外要求 **14 条业务场景、18 项 Qt 控件、8 项资源存储检查**。[验收入口与证据](../tests/services/README.md#按需首版真实环境验收)

- 四个账号从验证码邮件开始，完成注册、登录、好友申请与通过。
- 验证群管理、入群边界、旧会话代次被拒、离线后重新登录。
- 验证图片、视频、文件以及独立私聊中的资源引用。下载后比对摘要，实际解码 PNG 和视频，十秒视频检查 250 帧。
- 验证备注与本地查找；用第二个 SQLite 连接持有写锁，制造服务端已成功、本地落盘失败，再解锁并重试原请求。
- 运行自动 Qt 控件和资源存储测试，保存群面板截图，并分别核对控件证据和真实服务报告。

### 12. 服务端性能

手动 performance 单独构建并运行 Gate、双 Chat、Resource、Varify 和真实依赖。三个档位复用场景，改变负载。[性能测试说明](../tests/services/performance/README.md) · [固定负载配置](../tests/services/performance/profiles.json)

| 档位 | 连接数 | 目标速率（请求/秒） | 预热 | 正式采样 | 资源并发 |
| --- | ---: | ---: | ---: | --- | ---: |
| smoke | 20 | 10 | 5 秒 | 15 秒 × 1 轮 | 1 |
| baseline（默认） | 100 | 50 | 15 秒 | 60 秒 × 3 轮 | 2 |
| stress | 500 | 250 | 15 秒 | 60 秒 × 3 轮 | 4 |

- 测量登录、同服和跨服 256 字节文本、20 人群每两秒补拉、离线 100 条恢复。
- 测量 PNG、1 MiB 和 10 MiB 附件，以及多类操作混合负载。
- 压测前检查 Gate 空闲超过 60 秒后的首请求、255 字中文名称与描述重登、20 条 UUID 批量确认与幂等重试，以及真实 Redis 的 TTL 和验证码消费。
- 记录吞吐、延迟、调度滞后、未发送数、服务端 CPU/RSS 和压测端事件循环延迟。
- 检查报告覆盖和业务正确性；超时、丢失、重复落库、内容不符或清理失败会阻断。速度指标当前仅记录，未设置回归阈值。

### 13. 工具链刷新与冷恢复

- Windows 周检或手动 refresh_tools：选择新稳定版 PowerShell、CMake、Ninja 和 runner 的 MSVC 2022/SDK，冷构建并执行完整 Windows 回归。
- 验证原生工具链快照、编译器版本与摘要；四个 Windows 作业成功且依赖缓存保存后，新的工具链记录才供日常 CI 复用。
- Linux 手动 cold_linux：绕过二进制依赖缓存进行恢复，并继续执行完整 Linux 回归。普通周检的 Linux 路径可复用缓存。

规则见 [CI 治理中的维护周期](../tests/CI-GOVERNANCE.md#11-工具链维护周期)。

### 14. Windows 发布包启动冒烟

仅 master push 全量通过后执行 package → smoke → publish。[发布工作流](../.github/workflows/release.yml) · [发布包验证](../tests/release/contracts/README.md)

- 取同次 CI 产生的 Gate、Chat、Resource、Qt 客户端和 Varify 五个 ZIP，组装发行包并核验文件、版本与摘要。
- 在全新 Windows runner 下载该包，在源码树外启动程序，验证包内运行库齐全。
- 验证 Gate HTTP 与 Chat 监听启动和控制台停止；检查 Qt 窗口和包内 Node/gRPC 模块加载。
- 为 ResourceServer 启动独立临时 MySQL，测试 HTTP 拒绝未认证请求及正常停止。
- 冒烟通过后上传同一 ZIP，回验上传内容后发布。
