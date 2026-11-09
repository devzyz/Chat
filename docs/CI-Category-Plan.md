# Chat CI/CD 分类与调整方案

## 最终 CI 类别与测试内容（审核版）

分类用于回答“失败的是哪项行为”。共十类，组件回归按七个模块分别展示，平台构建按 Windows/Linux 分别展示。各类可以共享作业、编译结果和运行环境。

| 类别 | 主要测试内容 |
| --- | --- |
| 1. 静态检查与 CI 自检 | 检查代码规范、工作流语法、项目配置与用例注册；用分支、路径和异常报告样本验证运行范围、缺项检测与失败传播。 |
| 2. 构建基础设施与复用 | 验证工具链版本和快照摘要、vcpkg 依赖 ABI、缓存恢复与冷构建；检查下游取到同次构建的正确制品。实际复用包数与恢复耗时用于判断复用收益。 |
| 3. 平台构建 | **Windows** 检查原生服务和 Qt 客户端的 Release 编译、链接及独立运行目录。**Linux** 检查配置、编译、链接、启动，并覆盖禁用 Qt 的纯服务端构建。 |
| 4. 组件与协议回归 | Qt、Gate、Status、Chat、Resource、Varify、共享协议与运行库各有结果，具体范围见下表。 |
| 5. 运维脚本与进程管理 | 检查实例名、配置文件和端口冲突，验证启动失败、PID 身份、状态查询、停止及残留清理。 |
| 6. 真实依赖与服务集成 | 用临时 MySQL 验证迁移、事务和消息持久化；用 Redis 验证原子写、TTL 与恢复；用 Mailpit 验证 SMTP 投递；检查正式进程就绪、依赖中断和有序关闭。 |
| 7. 真实业务 E2E | 常规 full 覆盖双服注册登录、好友、跨服收发、离线历史、丢确认重发和重启恢复。手动四账号专项增加群管理、资源权限、媒体下载和客户端落盘失败恢复。 |
| 8. 打包与成品冒烟 | 验证 ZIP 文件清单、运行库、配置模板、协议与迁移文件、版本和 SHA；在干净 Windows 环境下载同次成品，实际启动程序并探测窗口、端口或接口。 |
| 9. 性能测试（手动） | 测登录、同服/跨服消息、群补拉、离线恢复与附件传输的吞吐、延迟、错误和资源占用；核对消息丢失、重复、采样缺失。 |
| 10. 报告与失败定位 | 对照本次选中的用例核验报告、提交 SHA、失败/跳过项与清理结果；摘要应能定位到失败模块、步骤、用例或编译诊断，并能打开原始日志。 |

| 第 4 类的独立结果项 | 主要测试内容 |
| --- | --- |
| Qt 客户端 | 登录与退出、会话清理、消息模型与分页、草稿与 SQLite 落盘、信号与控件、头像刷新、客户端收发和重试。 |
| GateServer | 注册、登录、重置密码的输入校验、下游调用顺序、失败处理和 HTTP 响应。 |
| StatusServer | 实例选择、路由、Token 校验，以及存储缺失或故障时的处理。 |
| ChatServer | 连接与会话、消息分发、发送状态与回执、重复提交、跨实例 RPC 调用。 |
| ResourceServer | 上传归属、分片偏移、重启续传、大小边界、内容摘要、路径与媒体签名校验。 |
| VarifyServer | 注册/重置验证码隔离、并发去重、冷却与消费后重发、异常响应、gRPC 路由。 |
| 共享协议与运行库 | 帧边界、半帧与粘包、序列化、protobuf 兼容与生成文件漂移、共用进程监督和生命周期。 |

**运行安排**：develop 日常走 quick；master 和周检走 full。默认分支每周一北京时间 03:17 的周检沿用全量清单并刷新 Windows 工具链。性能和四账号专项手动启用。公开发布改造和自动部署暂缓；现有 master push 自动发布仍按当前配置运行。

## 最终执行清单（2026-11-09 待审核）

本节是拟实施的顺序。序号用于阅读 Actions 摘要；实际等待关系见“何时执行”。先调整结果展示和故障定位，再评估是否拆 runner。下文较早的分类和外部调研保留作设计依据。

| 顺序 | Actions 显示项 | 验证范围 | 何时执行 | 失败后首先查看 |
| --- | --- | --- | --- | --- |
| 01 | 运行范围 | 提交、事件、目标分支、文档豁免、quick/full/专项及选择理由 | 所有入口首先执行 | 事件、比较提交、命中的路径和策略规则 |
| 02 | 静态与 CI 自检 | 代码规范、工作流、项目配置、用例注册、CI 自检；实例管理脚本和打包规则分别显示结果 | quick/full 的前置检查；develop 纯文档按现有范围执行此组 | 对应检查器、配置文件及行号；脚本失败显示命令、用例和退出码 |
| 03 | 平台构建 | Windows C++ 服务端、Windows Qt、Linux Release、Linux 无 Qt 构建分别显示；区分依赖恢复、配置、编译、链接、启动 | Windows 通过 02 后启动各通道；full 的 Linux 预检与 POSIX 进程检查可在 01 后并行启动 | 首条有效诊断、构建目标、文件与行号；依赖故障显示包名及恢复步骤 |
| 04 | 组件与协议回归 | Qt、Gate、Status、Chat、Resource、Varify、共享协议与运行库各自显示结果 | 各通道就绪即测；Varify 仅依赖自身 Node 环境。C++ 服务复用同次构建，按 suite/Test ID 拆摘要 | 模块、suite/test、测试源码、断言位置、预期与实际、原始 JUnit |
| 05 | 真实依赖与服务集成 | MySQL、Redis、SMTP、四进程就绪/故障/清理 | full；Linux 预检和 POSIX 检查均通过后启动，与 06 并行 | 依赖就绪记录、适配器用例、失败操作、所属服务日志和清理结果 |
| 06 | 真实业务 E2E | 常规双服 33 条；勾选四账号验收时额外展示群、资源和 Qt 控件结果 | full；与 05 使用各自临时环境；独立报告各自结果 | 场景 ID、最后成功步骤、首个失败步骤、关联消息 UUID 或服务端 ID、客户端/服务端日志 |
| 07 | 制品验证 | 核验同次构建的应用 ZIP、版本和摘要；成品冒烟复用同一发行包 | full 生成应用 ZIP；发行包组装与干净 Windows 冒烟沿用已有 master push 条件；已有发布链改造暂缓 | 包名、SHA、缺失文件或 DLL、启动失败的程序及日志 |
| 08 | 结果汇总 | 集合本次应运行项、实际结果、报告完整性与清理状态，保留各门禁既有依赖 | 各门禁在其上游结束后执行，测试失败也出摘要；现有 Full regression checks 先于 master 发行包冒烟，后者单列结果 | 展开独立失败项并链接原始 job/step；下游阻塞显示其失败前置项 |
| 专项 | 性能 | smoke、baseline、stress；构建测量环境后检查结果正确性、样本和指标 | 仅手动 performance；独立于 quick/full | 档位、场景、预热或采样阶段、目标速率/完成量、错误及性能报告 |

普通 develop PR/push 走 Windows quick。master PR/push、周检及手动 full 增加 Linux 和制品。每周 Windows 工具链刷新、手动 Linux 冷恢复标在对应构建项下。暂缓的是公开发布和部署的新增或改造；当前 master push 全量与成品冒烟成功后仍会自动发布。此审核稿没有修改触发器。[现有工作流入口](../.github/workflows/ci.yml) · [现有发布链](../.github/workflows/release.yml)

### 周检与编译结果复用

建议在 CI 清单中单列 **构建基础设施（工具链、依赖缓存、制品复用）**。它是第 03 项构建及其下游的配置类别，各消费作业就地记录结果；无需新增一个所有测试都要等待的缓存作业。周检保留为第 01 项的定时触发策略。

| 复用范围 | Chat 建议的做法 | 传输开销 |
| --- | --- | --- |
| 同一个 job | 四个 Windows C++ 服务共享一次构建，分模块显示结果，直接运行本工作目录中的程序 | 步骤之间直接读本地磁盘 |
| 同一次运行、不同 hosted job | Linux 集成和双服 E2E 各自取得同一 SHA 的运行包，传递程序及其运行依赖 | 构建作业上传，消费作业各自下载；评估耗时时计入打包、传输和解压 |
| 不同运行 | Windows/Linux 原生依赖按现有策略恢复 vcpkg 二进制归档；vcpkg 决定哪些包可以复用 | 当前 hosted runner 从远端恢复归档。若将来采用带持久磁盘的专用 runner，可从本机缓存读取，仍需核对工具链、triplet 与 ABI |

优化依据为同一提交、同一工具链下的冷/热对照。分别记录缓存大小、恢复耗时、实际复用包数、新编译耗时、整条 CI 关键路径与 runner 分钟。缓存键匹配只能说明取到了归档；实际包复用量由现有 vcpkg 日志确认。托管 runner 的作业隔离与工作目录共享规则见 [GitHub 官方说明](https://docs.github.com/en/actions/how-tos/manage-runners/github-hosted-runners/use-github-hosted-runners)。

- 默认分支 develop 计划每周一北京时间 03:17 执行 full。Windows 刷新工具链并冷构建原生依赖，通过验证的工具链快照供后续日常运行使用。Linux 周检正常使用依赖缓存，手动选择 cold_linux 时验证冷恢复。四账号验收和性能测试继续按需触发。
- 跨运行复用已验证的 Windows 工具链和 ABI 匹配的 vcpkg 二进制依赖。普通代码变更仍构建当前提交的应用并执行本次测试。
- 同次运行内，Linux 真实依赖和双服 E2E 下载同一构建作业的服务运行包并核对源码 SHA；性能测量复用其专属构建作业的运行包。Windows 成品验证复用本次应用 ZIP。
- 建议第 03 项摘要列出工具链来源、依赖实际复用与重新编译情况；消费服务运行包的作业列出来源作业和源码 SHA。这样能区分依赖恢复失败、源码编译失败和下游制品校验失败。

依据见[每周维护策略](../tests/CI-GOVERNANCE.md#11-工具链维护周期)、[Windows 缓存条件](../.github/workflows/windows-ci.yml)、[Linux 构建与制品交接](../.github/workflows/linux-ci.yml)和[性能构建与测量](../.github/workflows/performance.yml)。

### 失败摘要的验收条件

1. 首屏按独立失败项列出平台、类别、模块、失败步骤及直接证据。编译器指出文件行号时链接本次提交对应行；断言给出测试位置，原因仍待诊断时保留原始错误。
2. 每项提供原始日志或报告入口，以及可用的最小复跑方式。GoogleTest 给出测试目标与过滤器；CTest 给出注册用例名；托管 E2E 给出 selector、提交和重跑作业。运行依赖随命令列出。
3. 区分“测试失败”“环境准备失败”“因上游失败未执行”“本次未选择”“报告缺失”和“清理失败”。原始故障与随后发生的清理故障分别保留。并行通道同时失败时分别列出；日志中对配置、密码及 Token 脱敏。
4. 审核后的实施验收用已有报告或受控失败样本覆盖三条路径：编译失败能找到诊断行；组件断言失败能找到用例和实际值；依赖启动失败能看到就绪日志及受影响场景。Windows 正常回归继续核验 14 份报告、410 条预期用例，保留现有分支保护汇总名称。

例如，双服重试发生重复落库时，期望摘要显示下列内容（模拟失败，非运行结果）：

> Linux / 真实业务 E2E / Chat / E03-RECOVER-07。重新认证已完成；重放原 UUID 后预期 1 条持久化记录，实际 2 条。查看该用例断言、双服日志和本次业务报告。

证据入口见[恢复场景源码](../tests/services/faultRecoveryCases.js)、[现有覆盖清单](CI-Test-Inventory.md)和[测试方式与边界](CI-Test-Implementation.md)。

现状基于 2026-11-08 工作区配置。建议在 Actions 摘要里使用下面八类，让失败项直接回答“哪个组件、哪种验证失败”。`quick`、`full`、`performance` 继续表示一次运行选择的范围。按类展示和拆分 runner 可以分两步实施。[当前测试清单](CI-Test-Inventory.md) · [入口条件](../.github/workflows/ci.yml)

## 建议的八类检查

| 类别 | 建议显示的子项 | 当前落点及调整 |
| --- | --- | --- |
| 1. 代码规范与 CI 自检 | 静态检查、测试注册、CI 策略与工具脚本 | 从 Windows `static-check` 区分 actionlint、代码规范、注册检查和真正执行进程的脚本测试。打包规则归第 7 类 |
| 2. 组件回归 | Qt、Gate、Status、Chat、Resource、Varify、共享协议与运行库 | Qt 和 Varify 已有单独作业。四个 C++ 服务当前共享构建入口，先按模块汇总测试结果 |
| 3. 平台构建 | Windows Release、Linux Release、Linux 无 Qt 服务端构建 | 验证编译、链接、动态库和可启动产物。保留现有 Windows MSBuild/Qt 与 Linux CMake 的平台差异，用同次构建产物供后续作业使用 |
| 4. 真实依赖与服务集成 | MySQL、Redis、SMTP、多进程协作与恢复 | 从 Linux 全量中的 `disposable-services` 单列。覆盖生产适配器、数据库迁移、端口、故障和关闭 |
| 5. 真实业务 E2E | 双服消息与好友、四账号首版验收 | 常规 full 运行双 ChatServer 33 条场景，手动验收另加群、资源与控件证据。报告里区分协议旅程与自动控件结果 |
| 6. 性能与容量测试 | smoke、baseline、stress | 继续独立手动运行；记录负载、吞吐、延迟、资源和正确性。当前没有速度回归阈值 |
| 7. 打包与发布 | 打包规则、应用 ZIP、成品冒烟、发布 | quick 可测临时 ZIP；full 生成应用包；仅 master push 在全新 Windows runner 验证同一发行包后公开发布。目前交付物为 ZIP |
| 8. 部署与部署后验证 | 测试环境部署、就绪检查、部署后业务冒烟、回滚验证 | **规划项**。现有工作流止于发布。实施前需确定部署目标、可部署产物和配置来源；生产部署策略单独决定 |

工具链刷新、Linux 冷恢复是第 3 类的专项运行条件。报告完整性与清理检查归入其验证的类别；顶层汇总检查继续服务分支保护。这样 Actions 上既能看到失败职责，也能知道失败发生的平台。[Windows 作业](../.github/workflows/windows-ci.yml) · [Linux 作业](../.github/workflows/linux-ci.yml) · [治理规则](../tests/CI-GOVERNANCE.md)

## 组件怎么分，跨服务场景放哪里

| 归属 | 应收进该项的检查 | 进入系统级测试的分界 |
| --- | --- | --- |
| Qt 客户端 | 解帧、登录状态、退出清理、模型分页、SQLite 落盘、UI 控件与回环网络 | 两个正式客户端通过不同 Chat 实例互发，归业务 E2E |
| Gate | 注册、登录、重置密码的字段校验、下游失败与 HTTP 响应 | 从 Mailpit 取码并经 Gate 完成注册、选服和 TCP 登录，归跨服务 |
| Status | 实例选择、路由、Token 校验、存储失败 | 生产 Status 在真实 Redis 故障及重启后的行为，归集成 |
| Chat | 连接与会话、分发、消息回执、重试、gRPC 客户端 | 跨服消息、丢确认、重启恢复、双端历史一致性，归业务 E2E |
| Resource | 临时文件上的归属、续传、摘要和路径校验 | 登录用户经生产服务上传、群聊或私聊引用并下载校验，归业务 E2E |
| Varify | 验证码用途、并发去重、冷却、超时和失败响应 | 生产 Redis 与 SMTP 投递至 Mailpit，归真实依赖集成 |
| 共享协议与运行库 | 多个服务共用的帧格式、protobuf 漂移、进程监督器、通用生命周期 | 共享协议变更关联所有生产者和消费者；实例管理脚本单列为运维脚本检查 |

“服务端其余”可拆成 **Varify、共享协议与运行库、运维脚本**。用例按主要断言指定一个归属，例如“Gate 在验证码服务失败时返回约定错误”归 Gate，“邮件实际到达”归 SMTP 集成。同一个可执行文件内的用例仍可有不同归属。[服务端模块表](../tests/server/README.md) · [Qt 模块表](../chat/tests/README.md) · [Varify 模块表](../VarifyServer/test/README.md)

## 分支与执行成本

| 场景 | 第一阶段建议 |
| --- | --- |
| develop 普通 PR / push | 保留 Windows quick 和现有文档豁免，按组件展示结果。纯文档目前仍会执行实例脚本和打包合同，可单独测量这两步的成本 |
| master PR / push、每周检查 | 保留 Windows 与 Linux full、真实依赖和双服 E2E。每周附加 Windows 工具链刷新；master push 独有成品冒烟和发布 |
| 手动专项 | 四账号验收、Linux 冷恢复、性能档位分别选择。结果摘要列出实际执行项和未请求项 |
| 未来部署 | 首先手动将指定版本部署到测试环境；验证制品摘要和运行版本，检查服务就绪，再用测试账号跑登录、发送、接收。演练失败后的恢复，再决定是否接入自动触发 |

**两个仓库特有的边界。** Windows 服务端作业恢复 vcpkg 并一次编译多个生产和测试目标。直接拆成四个 runner 会重复准备依赖，且部分 XML 混有多个服务的用例。建议先按测试套件或 Test ID 生成每服务摘要，保留原始报告计数检查。未来若部署目标是 Linux，须先定义有版本、含运行依赖、可校验的 Linux 服务端交付物；目前 Linux 工作流的临时测试运行包与正式 Windows 发行 ZIP 用途不同。[Windows 构建入口](../scripts/windows-local.ps1) · [发布合同](../tests/release/contracts/README.md)

## 建议实施顺序

1. **先改结果展示。** 给现有步骤和测试报告建立“类别、组件、平台、测试层级”的映射。在一次 quick 和一次 full 的摘要里能从失败项跳到原 job、日志和用例；保留现有 required check 名称与 Windows 14 份报告、410 条用例的校验。
2. **再调整作业边界。** 先记录典型 PR 的排队时间、关键路径、依赖恢复、编译、测试和制品传输耗时。优先检查所有 Windows 构建共同等待的 static-check。用相同提交比较“单次编译后分组执行”和“传递产物后并行测试”，据耗时和 runner 分钟决定要拆哪些 job。
3. **最后优化触发。** 若要按路径缩减 develop 检查，先并行计算“原策略会跑什么、拟议策略会跑什么”。覆盖共享 proto、构建配置、锁文件、多服务改动、重命名与无法确定范围等情形；共享影响或无法判定时扩大运行范围。抽查全量结果后再启用跳过。
4. **部署单独落地。** 明确目标操作系统、服务启动方式、测试环境与版本恢复方式，再接第 8 类。优先复用已验证制品，部署记录关联提交、摘要和环境，以便判断失败发生于程序、打包还是环境。


## 外部仓库的实际做法

| 仓库 | 从官方 CI 配置观察到的做法 | 对 Chat 的启示 |
| --- | --- | --- |
| Nextcloud Desktop | Qt 桌面项目的 Actions 分别展示 Windows Build and Test、Linux Clang 编译测试和 QML 标签检查。[工作流列表](https://github.com/nextcloud/desktop/actions) · [Linux Clang 入口](https://github.com/nextcloud/desktop/actions/workflows/linux-clang-compile-tests.yml) | 客户端可以有平台和组件两个视角。Chat 可以在同一摘要里展示 Qt 的构建、模型、网络和控件结果。 |
| Helm | build-test 在一个 Ubuntu 作业内依次检查源码头、Go 模块一致性、单元测试和构建。[构建测试配置](https://github.com/helm/helm/blob/main/.github/workflows/build-test.yml) | 可以先用步骤区分检查类别，共享同一份工具链和工作目录。 |
| Google Online Boutique | PR 工作流按服务目录执行 Go 和 C# 单元测试。部署工作流将镜像部署到 GKE 命名空间，逐服务等待就绪，再由负载生成器产生请求并检查错误数。[PR 测试](https://github.com/GoogleCloudPlatform/microservices-demo/blob/main/.github/workflows/ci-pr.yaml) · [部署验证](https://github.com/GoogleCloudPlatform/microservices-demo/blob/main/.github/workflows/ci-main.yaml) | 多服务系统可以区分组件正确、服务就绪和部署后用户操作成功。Chat 可借用这三种验证。 |
| Bitcoin Core | 先固定提交，再交叉编译 Windows 程序并上传 DLL、EXE 和配置。下游 Windows 作业取同一提交和同次运行的产物，检查导入的 DLL、清单，再运行单元及功能测试。[CI 工作流](https://github.com/bitcoin/bitcoin/blob/master/.github/workflows/ci.yml) | 多个服务可以共用一次原生构建。只有需要不同环境、并行执行或独立重试时，才值得上传产物并另启测试作业。 |
| Neovim | 单列 lint 作业；将构建配置与单元、功能、历史测试套件组成矩阵，显式排除部分组合。安装后还执行已安装的程序、打开帮助并核对安装文件。[测试工作流](https://github.com/neovim/neovim/blob/master/.github/workflows/test.yml) | Windows/Linux、测试层级和交付验证可独立表达。Qt 源码通过测试与用户解压后的客户端能启动，需要分别留证。 |
| Redis | 常规 Ubuntu 作业用标签排除慢用例，还检查生成文件漂移。Daily 工作流由定时、手动或发布分支 PR 触发，包含 Sentinel 和 Cluster 套件。[常规 CI](https://github.com/redis/redis/blob/unstable/.github/workflows/ci.yml) · [Daily](https://github.com/redis/redis/blob/unstable/.github/workflows/daily.yml) | 双 ChatServer 和故障恢复属于业务覆盖范围；是否在每次提交、周检或手动运行，则是可独立调整的触发策略。 |
