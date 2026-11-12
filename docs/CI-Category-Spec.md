# CI 分类、触发与测试范围规范

**适用范围：Chat 仓库的 GitHub Actions。** 类别编号用于归属与检索；实际作业可共享编译结果、并行执行。文中的用例数表示运行器期望值。

## 一、事件如何选择测试

| 事件 | 模式 | 实际选择 |
| --- | --- | --- |
| 向 develop 提 PR，或推送到 develop | quick | 默认执行 Windows 静态、构建和模块回归。只有完整差异全部属于指定文档时走精简路径 |
| 向 master 提 PR | full | Windows、Linux、真实依赖、双服 E2E、应用 ZIP；额外检查版本是否已经公开发布 |
| 推送或合并到 master | full | 对实际推送的提交执行全量；成功后用本次应用 ZIP 组装发行包，在新 Windows runner 冒烟。当前配置会继续回验并发布 |
| 默认分支 develop 每周一北京时间 03:17 | full | 双平台全量并刷新 Windows 工具链；Linux 默认复用依赖缓存。定时运行只生成应用包 |
| 手动 quick 或 full | 默认 full | 按所选分支和模式执行；手动 quick 也执行应用构建。特殊开关见下文 |
| 手动 performance | 独立专项 | 使用专属 Linux 构建和测量作业。档位可选 smoke、baseline、stress，默认 baseline |

- PR 按**目标分支**选模式，活动包括 opened、synchronize、reopened、edited、ready_for_review。功能分支单独 push 不直接触发此入口；已开 PR 的新提交通过 synchronize 触发。
- 文档精简仅适用于 develop 的自动 PR/push。允许路径为根目录 README.md、WINDOWS_BUILD.md 和 docs/ 下的 Markdown。混合修改、空差异、无法取得比较范围均回到普通 quick。
- 手动 real_acceptance 增加四账号与控件验收；cold_linux 验证 Linux 无缓存恢复；refresh_tools 刷新 Windows 工具链且仅允许默认分支。任一开关会将手动 quick 提升为 full。performance 不接受这些开关。

配置来源见 [总入口](../.github/workflows/ci.yml)、[模式选择](../scripts/ci/ciPolicy.js) 和 [路径判定](../scripts/ci/ciScope.js)。

## 二、十类检查的触发矩阵

表格表示上游成功、运行未取消时的执行范围。“文档”指 develop 文档精简路径；“普通全量”包括 master PR、周检和手动 full；master push 单列。专项列只列确实进入性能工作流的部分。

| ID | 类别 | 文档 | 普通 quick | 普通全量 | master push | 手动 performance |
| --- | --- | --- | --- | --- | --- | --- |
| 01 | 静态与 CI 自检 | Windows 静态作业 | 同左 | 同左 | 同左 | 性能框架与策略合同 |
| 02 | 构建基础设施与复用 | 仅缓存/工具脚本自检，无应用依赖恢复 | Windows 恢复 | 双平台；周检刷新 Windows | 双平台 | Linux 专项恢复与制品校验 |
| 03 | 平台构建 | 仅 CMake 配置合同，无应用构建 | Windows | Windows + Linux | Windows + Linux | 仅性能运行包 |
| 04 | 组件与协议回归 | — | Windows 模块回归 | 含 Windows 模块回归 | 同左 | — |
| 05 | 运维脚本与进程管理 | Windows 实例脚本 | 同左 | 另含 Linux POSIX 进程检查 | 同左 | — |
| 06 | 真实依赖与服务集成 | — | — | Linux 3C | Linux 3C | — |
| 07 | 真实业务 E2E | — | — | Linux 3D；手动可加四账号验收 | Linux 3D | — |
| 08 | 打包与成品冒烟 | 打包合同 | 打包合同 | 合同 + 五个应用 ZIP | 再加发行包组装、下载冒烟 | — |
| 09 | 性能测试 | — | — | — | — | 按选定档位实测 |
| 10 | 报告与失败定位 | 已选静态作业及其报告 | Windows 门禁 | 双平台门禁，按需验收门禁 | 双平台门禁；成品冒烟另行报告 | 专项报告校验 |

01 中的版本已发布检查仅在 master PR/push 执行。06、07 的“—”指对应的 3C、3D 套件未运行；性能模式会使用真实 MySQL、Redis 和 Mailpit，但按 09 的负载与正确性合同出报告。

## 三、分类目录与范围协议

以下英文名为类别名称，Actions 步骤使用相同编号标识职责。每类列出当前测试内容、主要观察边界和原始证据。触发条件统一按第二节编号引用。

### 01 · Static Analysis and CI Validation · 静态检查与 CI 自检

- **当前测试内容**：增量代码规范、Actions 语法、项目 XML、vcpkg 清单和 triplet、测试源注册、分支分流、CI 执行预算、异常报告与失败传播。master PR/push 检查发布版本冲突。
- **范围协议**：以检查器诊断、配置契约和策略输入输出为依据。缓存脚本合同和 CMake 配置合同能在纯文档路径运行；应用编译及真实缓存恢复归 02、03。
- **入口与证据**：Windows static-check 的具体 step、文件行号或策略测试名。见 [Windows 工作流](../.github/workflows/windows-ci.yml)。

### 02 · Build Infrastructure and Artifact Reuse · 构建基础设施与复用

- **当前测试内容**：验证工具链版本与快照、vcpkg 二进制包恢复与实际复用、编译器及 ABI 诊断；下游校验同次构建运行包的提交 SHA 与摘要。周检冷构建 Windows 依赖，Linux 冷恢复由手动开关启用。
- **范围协议**：同一 job 直接读取本地构建目录；独立 hosted job 传递其所需运行包；不同运行复用兼容依赖和已验证工具链。应用与测试结果绑定本次提交。
- **入口与证据**：依赖恢复日志、实际命中包数、工具链来源、制品 SHA。恢复及构建耗时见对应 Actions 步骤。见 [缓存脚本](../scripts/ci/vcpkgBinaryCache.js)。

### 03 · Platform Builds · 平台构建

- **当前测试内容**：Windows 编译 Gate、Chat、Resource 和 Qt Release 目标，核对原生服务独立运行目录。Linux 检查配置、编译、链接、启动，另以关闭 Qt 的配置编译 Gate、Chat。性能模式单独构建服务端和监督器。
- **范围协议**：通过结论绑定目标、操作系统、工具链和构建选项。Linux 无 Qt 变体以实际列出的目标为限；发行包在干净机器启动归 08。
- **入口与证据**：保留平台、构建目标、失败阶段和编译器首条有效诊断。见 [Windows](../.github/workflows/windows-ci.yml)、[Linux](../.github/workflows/linux-ci.yml)。

### 04 · Component and Protocol Regression · 组件与协议回归

| 结果归属（英文 / 中文） | 当前测试内容 |
| --- | --- |
| Qt Client / Qt 客户端 | 登录与退出、会话重置、消息模型和分页、草稿与 SQLite 持久化、控件信号、头像、客户端收发与重试 |
| GateServer / 网关服务 | 注册、登录和重置密码的请求校验、依赖调用顺序、异常传播、HTTP 响应、实例选择与凭据签发 |
| ChatServer / 聊天服务 | TCP 凭据初验、连接与会话、分发、发送与回执、重复提交、生产 gRPC 客户端及回环协议 |
| ResourceServer / 资源服务 | 临时文件上的上传归属、分片偏移、重启续传、完整性、大小边界、路径及媒体签名 |
| VarifyServer / 验证码服务 | 注册与重置用途隔离、并发去重、冷却与消费、错误响应、本机 gRPC 和 TLS |
| Shared Protocols and Runtime / 共享协议与运行库 | 帧边界与字节序、半帧与粘包、protobuf 兼容与生成文件漂移、通用生命周期 |

- **范围协议**：按主要断言归属组件，包含单元、组件和已注册的确定性回环/进程集成。使用替身的测试只证明本组件如何处理结果。Resource 常规 Windows 报告限于九条存储合同；真实依赖、双服业务另见 06、07。
- **入口与证据**：RunServerTests、RunClientTests、RunVarifyTests 的 JUnit。摘要按报告、测试源码和 suite 归属统计本次已记录的用例；部分 C++ 模块共享二进制和 XML。见 [报告注册表](../scripts/windows-local.ps1)、[用例实现与边界](CI-Test-Implementation.md)。

### 05 · Operations Scripts and Process Lifecycle · 运维脚本与进程管理

- **当前测试内容**：实例名、日志名、端口和配置文件冲突；缺文件与参数；启动失败、PID 身份、状态、停止及残留清理。Linux full 另检验真实 POSIX 子进程终止与回收。
- **范围协议**：观察管理命令的退出状态和实际进程状态。服务被重启后用户消息能否恢复归 07。Windows 脚本位于静态作业，因此 develop 纯文档也执行。
- **入口与证据**：RunScriptTests、Linux posix-process-contracts；记录命令、用例、进程身份和清理结果。见 [脚本测试](../tests/scripts/README.md)。

### 06 · Service and Dependency Integration · 真实依赖与服务集成

- **当前测试内容**：临时 MySQL 的迁移、注册与消息持久化；真实 Redis 的数据、TTL 与故障恢复；SMTP 投递到 Mailpit；四个生产服务的就绪、通信、中断恢复和有序关闭。
- **范围协议**：运行在本次作业拥有的临时环境。以实际数据库记录、收件、服务响应和清理结果判定。单个适配器被模拟时，不据此声明真实依赖覆盖。
- **入口与证据**：Linux disposable-services 运行 3C-07，产出 phase3c-services-evidence。见 [服务测试说明](../tests/services/README.md)。

### 07 · End-to-End Business Tests · 真实业务端到端测试

- **当前测试内容**：常规 full 要求当前版本双 ChatServer 的 33 条场景，涵盖公开注册登录、好友、跨服收发、离线历史、丢确认重试和重启恢复。手动 real_acceptance 另加 14 条四账号业务场景，并单独校验 18 项 Qt 控件和 8 项资源存储检查。
- **范围协议**：通过公开入口关联客户端、服务端与必要的持久化结果。常规 3D、四账号业务和自动控件证据分别标记；普通周检和 master 全量只承诺常规套件。
- **入口与证据**：Linux two-server-contract；场景 ID、最后成功步骤、首个失败观察、脱敏消息 ID 和清理报告。见 [双服与专项验收](../tests/services/README.md)。

### 08 · Packaging and Artifact Smoke Tests · 打包与成品冒烟

- **当前测试内容**：静态作业内用临时 ZIP 验证缺文件、错版本、摘要、配置清理与布局。full 生成三个 C++ 服务、Qt 客户端及 Varify 五个 ZIP。仅 master push 在全量门禁通过后组装发行包、重新下载，在新 Windows runner 探测窗口、监听端口、HTTP 或运行库加载。
- **范围协议**：打包规则、应用 ZIP 和成品启动分别记录。测试范围止于已验证制品；公开发布改造、目标环境自动部署留待后续审核。当前发布工作流在冒烟通过后仍执行上传回验和公开发布。
- **入口与证据**：原始 ZIP、源码 SHA、发行包摘要、冒烟报告与程序启动日志。见 [发布包验证](../tests/release/contracts/README.md)、[当前发布链](../.github/workflows/release.yml)。

### 09 · Performance Tests · 性能测试

- **当前测试内容**：真实服务登录、同服/跨服文本、群补拉、离线消息恢复、图片和附件及混合负载；记录吞吐、延迟、错误、服务 CPU/RSS 和调度延迟。
- **范围协议**：仅由手动 performance 触发，档位和环境随报告记录。缺样本、超时、内容不符、丢失或重复落库会失败；当前速度指标没有硬回归阈值。
- **入口与证据**：专项 build → measure；提交 SHA、档位、失败阶段、metrics.json、performance.xml。见 [性能工作流](../.github/workflows/performance.yml)、[负载定义](../tests/services/performance/profiles.json)。

### 10 · Test Reporting and Failure Diagnostics · 报告完整性与失败定位

- **当前测试内容**：Windows 模块运行器校验其负责的报告。普通 Windows 回归的注册基线为 13 份报告、383 条用例；纯文档只核验选中的作业。Linux full 校验 3C/3D 场景、源码 SHA、报告摘要与环境清理；性能模式独立校验采样和报告。
- **范围协议**：Regression checks 汇总策略与 Windows；Full regression checks 汇总双平台；显式请求四账号验收时另有 Real environment acceptance。master push 的发行包冒烟在 full 门禁之后出结果。测试失败、实际未运行与上游阻塞须分别解释。
- **查看失败**：在运行页 Summary 找到对应通道，按模块查看用例、原始报告、作业日志和当前 job 复跑命令。GoogleTest 提供仓库内文件/行号时可直接打开该提交的源码。摘要只摘录受支持的诊断类别和数值比较，详细输出留在原始日志。无 JUnit 的准备、编译或工件错误从 Failure diagnostics 进入本次 attempt 的具体步骤。来源见 [门禁条件](../.github/workflows/ci.yml) 和 [报告规则](../tests/README.md)。

## 四、执行依赖与审核边界

1. plan 先选模式。常规 Windows 先运行 static-check，再并行运行服务端、Qt 和 Varify。服务端共用一次编译，分类结果可分别展示。
2. full 中 Linux 与 Windows 可并行启动。Linux 构建预检和 POSIX 检查通过后，3C 与 3D 在独立临时环境并行执行，再校验两套报告。性能有独立依赖链。
3. 摘要读取已有报告；必需用例数、原有门禁及失败传播仍由所属运行器验证。额外的 Failure diagnostics 只在回归或性能通道失败时建立步骤日志索引，发布作业沿用原有依赖。

详细用例见[现有测试清单](CI-Test-Inventory.md)，分类设计依据见[调整方案](CI-Category-Plan.md)。本文核对仓库配置，未验证 GitHub 在线分支保护设置或某次运行结果。
