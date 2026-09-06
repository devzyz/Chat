# CI 与自动测试治理规范

状态：治理合同已确认；Phase 2.5 已由 clean PR、合并后 develop CI 与 branch protection 远端验证；Plan 3A-01 已完成本地合同验证
适用分支：`develop`、`master` 与正式发布
决策来源：2026-08-25 完成的 12 项测试治理问答；2026-08-31 补充的本机 vcpkg 不可变决策

## 1. 目标与承诺边界

CI 的目标是尽可能全面地证明所有已登记、已确认的 Interface 合同没有回归，并以此作为合并与发布的必要条件。

CI 通过不代表未测试行为绝对正确。自动测试遗漏由人工 UAT 补位；人工测试或线上发现的缺陷，原则上必须在修复前转化为能够复现问题的永久回归测试。无法安全自动化时，必须记录原因、人工验证步骤和剩余风险。

测试以 Module 的稳定 Interface 为表面。Interface 包括输入输出、状态不变量、顺序、错误、超时、配置和生命周期，而不只是类型签名。只有生产 Adapter 与测试 Adapter 确实需要在同一 Seam 上替换时才增加 Seam；不得为覆盖率或 mock 方便创建假设性浅接口。

## 2. 已确认决策

| ID | 决策 | 治理结果 |
| --- | --- | --- |
| D-01 | CI 绿色的含义 | 保护全部已登记合同；不承诺未测试行为，人工 UAT 负责发现遗漏 |
| D-02 | 合并与发布门禁 | `develop` 使用全量快速回归；`master` 和发布增加真实数据、业务 E2E 与兼容性 |
| D-03 | 真实依赖环境 | 使用 disposable 基础设施；短期可用专用 runner，长期期望 Docker 部署；禁止个人服务 |
| D-04 | 合同变更授权 | 修改或删除既有回归合同必须经过专项评审并记录兼容与迁移影响 |
| D-05 | `develop` 范围 | 每个 PR 运行全仓 Static、Unit、Component 和确定性 loopback Integration |
| D-06 | `master` 范围 | 运行所有历史测试、真实 Adapter Integration、固定核心 E2E 和新 Module 新增测试 |
| D-07 | 未完成真实测试的新 Module | 可在默认关闭的 feature flag 下进入 `develop`；进入 `master` 前必须补齐 |
| D-08 | 时间预算 | 完整性优先；快速门禁应尽快反馈，`master` 和发布允许更宽预算，不以耗时为跳过理由 |
| D-09 | flaky 与错误 | 任一 Required Check 错误即阻断；禁止自动重试刷绿和无记录绕过 |
| D-10 | 覆盖率 | 使用 changed-lines coverage 发现遗漏；不为固定全仓比例制造无意义测试 |
| D-11 | 兼容性 | 公开协议、持久化和迁移至少验证当前版本与上一已发布版本 |
| D-12 | 发布产物 | 同一 SHA 生成一次不可变 artifact；完成 artifact smoke 与人工 UAT 后晋升，不重建 |
| D-13 | 本机 vcpkg 变更授权 | `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 默认只读；任何修改必须先展示精确命令/目标/影响并取得用户明确批准（DG-25） |

任何后续计划都必须引用相关决策 ID。若计划与表中决策冲突，必须先按 D-04 完成评审，不能在实施过程中静默改变治理规则。

### 2.1 比例化执行合同

本节是所有未完成正式计划的唯一过程分层权威。执行者先按变更的最高风险面选择一层；同一行为切片只执行该层要求的
证据，不因计划文件较长而自动升级。用户明确要求、适用 DG/G owner、phase dependency 或下列 closeout 规则仍优先：

| 层级 | 适用范围 | 必要执行与验证 |
| --- | --- | --- |
| S0 | 简单只读检查、文档/静态链接、无行为注册、已获批的依赖操作 | `inspect -> execute -> focused verify`；除非用户或既有合同明确要求，不扩展正式计划，不做 mutation、threat model、全量回归、文档矩阵或 subagent 工作。依赖操作只验证本次获批目标，不派生新的 package 机制。 |
| S1 | Module 或可观察行为变更 | 每个行为切片执行 focused RED/GREEN；只对可观察行为、失败传播、lifecycle、兼容性或 gate enforcement 做有意义 mutation；代码稳定后每个 plan 运行一次 owning focused/public runner。 |
| S2 | process、transport 或 persistence 变更 | 在 S1 基础上执行 focused real Integration；只清理由本任务创建且可精确归属的进程、端口和临时资源，并由 owning runner 证明成功/失败均有界。 |
| S3 | phase、CI 或 release closeout | 聚合报告、secret、residue、diff 与远端证据；每 phase 只运行一次 full lane，并在其权威环境证明 applicable dependency gate、Required Checks、UAT 和 promotion 合同。 |

以下去重规则对所有层级具有约束力：

1. `RunAllTests` 或等价 full lane 只由 phase closeout 执行一次。closeout 后的纯文档改动不触发 full rerun；source/runner
   改动只重跑 affected owner，除非改动使 phase final evidence 失效。
2. secret scan 只在处理 secret/data/log/artifact 的任务执行，并在 closeout 聚合一次；process/port/temp cleanup 只在创建这些
   资源的任务执行，并在 closeout 聚合一次。
3. staged index、protected path、git diff、文档 link/count audit 只在 closeout 聚合一次。结构注册可在 closeout 使用一个静态
   negative probe；prose、简单 registration、静态链接、无害 plumbing 和 package installation 不要求 mutation。
4. focused RED/GREEN 按行为切片执行；owning public runner 在同一 plan 代码稳定后只执行一次。每个任务的 `read_first` 只保留
   edited source、closest analog 与一个必要 authority；共享 DG/G、baseline、lane 与 threat prerequisites 在 phase entry 读取一次。
5. phase entry 对工具和依赖只做一次 read-only preflight。不得重复计算 tool hash、package identity 或 vcpkg fingerprint；缺失或
   不一致必须 fail closed 并请求对精确修改的批准，不能自动 restore。巨型 inline verifier 应由 planned/public owning script 暴露
   等价、确定性、非零退出合同。

## 3. 测试维度与依赖分类

测试继续使用两个独立维度：

- Domain：Foundation、Architecture、Business。
- Level：Unit、Component、Integration、E2E、Stress。

目录由生产 Module 所有权决定，Level 只通过 Test ID、标签、runner、报告和 CI lane 表达，不创建平行的 `tests/unit`、`tests/integration` 目录树。

依赖按以下类别规划：

| 依赖类别 | 示例 | 测试策略 |
| --- | --- | --- |
| In-process | 状态机、选择策略、序列化规则 | 直接通过 Module Interface 做 Unit |
| Local-substitutable | 临时文件、临时 SQLite、可控时钟 | 使用本地替身做 Unit/Component；真实 Adapter 另有 Integration |
| Remote but owned | Gate/Status/Chat/Varify 间 HTTP、TCP、gRPC | 业务 Module 通过 port Interface 使用 in-memory Adapter；真实 transport 使用 loopback Integration |
| True external | SMTP provider、摄像头、麦克风 | 通过窄 Interface 注入 Adapter；自动测试使用本地 sink/合成设备，真实硬件保留专用或人工验证 |

fake Redis、MySQL、SMTP 或 gRPC 只能证明 Unit/Component，不能计入真实 Adapter Integration。

## 4. 分支与发布门禁

### 4.0 Phase 2.5 develop 门禁实现状态

本地公开入口为 `scripts/windows-local.ps1`。`RunAllTests` 只编排
`RunScriptTests`、`RunServerTests`、`RunClientTests` 和 `RunVarifyTests`，并在结尾通过
`CheckTestReports` 同一实现核验精确的 13 份 XML、313 个 testcase、零 failure、零 error、零 skipped/disabled/unavailable/timeout，并保留原始 12 份报告 / 232 testcase floor。
每个子入口先移除自己的旧报告；缺报告、数量漂移、失败节点、credential-shaped assignment、cleanup evidence 缺失、非零测试退出码或本次新增的已知临时目录残留都会返回非零。

GitHub workflow 的稳定 job/check 名为：

- `Static configuration checks`
- `Server Release build`
- `Qt client Release`
- `VarifyServer dependency and package check`

四个 job 分别调用上述公开 `Run*Tests` 入口；报告上传使用 `if: always()` 和
`if-no-files-found: error`，且 workflow 禁止 `continue-on-error`。这些名称只有在 clean PR
实际全绿后才能加入 `develop` branch protection；本地通过不能替代远端 check 存在性证明。

### 4.1 `develop` PR：全量快速回归

每个进入 `develop` 的 PR 必须运行全仓快速回归，不先使用文件影响分析裁剪：

1. 静态配置、依赖锁定、测试注册和凭据检查。
2. Server、Qt、VarifyServer、PowerShell 的构建或语法检查。
3. 全部 Unit。
4. 全部确定性 Component。
5. 不依赖 Redis/MySQL/SMTP 的 HTTP/TCP/gRPC loopback Integration。
6. protobuf、HTTP、TCP 等公开协议的兼容静态检查与 golden fixture。
7. changed-lines coverage 采集和新增代码遗漏提示。
8. JUnit/XML 存在性、失败传播和 teardown 结果检查。

`develop` 快速门禁不得访问开发者 Redis、MySQL、邮箱、固定局域网端点或个人配置。动态 loopback 端口、受控子进程和本次运行的临时目录可以属于快速门禁，只要具备确定超时和清理。

目标是通常在 60 分钟内完成。该数字是优化目标，不是跳过 Required Test 的理由。

### 4.2 `master` PR：完整回归

每个进入 `master` 的 PR 必须运行：

1. `develop` 的全部 Required Check。
2. 全部既有历史回归测试。
3. disposable Redis、MySQL 和本地 SMTP sink Integration。
4. Gate、Status、Chat、Varify 的真实进程启动、ready、停止和失败恢复。
5. 固定核心业务 E2E。
6. 本次新增或修改 Module 的专项 Integration/E2E。
7. 当前版本与上一已发布版本的兼容性矩阵。
8. 发布候选 artifact 的基本完整性检查。

固定核心 E2E 至少覆盖：验证码、注册、登录和服务发现、Chat Token 登录、好友申请与接受、私聊、双 ChatServer 跨实例投递、下线重登、历史分页/顺序/去重。

`master` 不允许依靠 feature flag 掩盖待发布 Module 的缺失 Integration/E2E。默认关闭且明确不属于本次发布范围的实验 Module 可以保留在代码中，但它的禁用路径、配置和不会影响既有行为的合同必须通过测试。

### 4.3 正式发布：不可变产物晋升

1. 选择已经通过 `master` 完整门禁的 SHA。
2. 从该 SHA 生成一次不可变 artifact，并记录校验值。
3. 使用该 artifact 完成安装、配置、启动、升级、停止和端口释放 smoke。
4. 执行版本化人工 UAT 清单。
5. UAT 全部通过后晋升同一 artifact；不得重新构建一个未经相同验证的新产物。

如果 UAT 发现缺陷，发布阻断。能够自动化的缺陷必须形成回归测试，并重新从相应分支门禁开始验证。

## 5. 新 Module 准入合同

缓存、群聊、文件、语音、音视频、局域网或其他新 Module 进入 `develop` 前必须记录：

1. Module Interface：正常结果、不变量、错误、超时、顺序和生命周期。
2. Domain 与每项 Test ID 的 Level。
3. 所有依赖的类别及其生产 Adapter、测试 Adapter。
4. Unit/Component 测试和 RED-GREEN 证据。
5. 真实 Integration/E2E 的状态、runner、资源和清理策略。
6. CI lane、报告名及 Required 状态。
7. 与上一发布版本的协议、schema、缓存或数据兼容影响。
8. feature flag 的默认值、配置来源、禁用路径和删除条件。

真实 Integration/E2E 尚未完成时，仅在以下条件全部满足后允许进入 `develop`：

- Module 默认关闭；
- 关闭后不改变既有公开行为和发布包启动行为；
- Unit/Component 完整保护已实现 Interface；
- 缺口有负责人、验收条件和计划归属；
- 无法完全隔离时不允许提前合并。

进入 `master` 前必须补齐真实 Adapter Integration、公开工作流 E2E 和兼容性验证。

## 6. 合同变更与测试维护

修改、替换或删除既有测试前必须提交合同变更说明：

1. 原 Test ID 和旧合同。
2. 新合同及其业务理由。
3. 受影响生产者、消费者和发布版本。
4. 兼容、迁移和回滚方案。
5. 更新后的 Unit/Component/Integration/E2E。
6. 专项评审结论。

实现重构但 Interface 不变时，现有 Interface 测试应保持不变。如果测试仅因私有类、容器或调用顺序调整而必须重写，说明测试穿过了错误的 Seam，应先修正测试表面。

旧浅 Module 被一个更深 Module 取代后，应由新 Interface 测试替换穿透内部实现的旧测试，而不是永久叠加两套同义预期。

## 7. 失败、flaky 和例外

- 任一 Required Check 失败、超时、崩溃、缺少报告或清理失败都视为 CI 错误并阻断合并。
- 自动重试可以收集诊断信息，但不能把首次失败改写为绿色结果。
- flaky 测试必须创建缺陷记录，包含负责人、复现信息、影响 lane 和修复期限。
- required flaky 测试在修复前仍然阻断；不得通过 `continue-on-error`、吞退出码或人工无记录放行绕过。
- 只有 D-04 的合同评审可以改变 Required 合同；紧急程度不是跳过门禁的授权。

## 8. 覆盖率策略

覆盖率是遗漏探测器，不是正确性证明：

1. 先采集并保存每个主要 Module 的基线。
2. 优先报告新增和修改行的 changed-lines coverage。
3. 新纯逻辑和业务分支缺少覆盖时阻断或要求评审说明。
4. 不立即设置武断的全仓统一百分比。
5. 不得增加只执行代码、没有有效行为断言的测试来提高数字。
6. 覆盖率例外必须记录无法自动化的原因和替代验证。

当基线稳定后，可以为不同 Module 分别设定合理阈值；阈值调整属于治理变更，必须评审。

## 9. 兼容性策略

`master` 至少维护“当前版本 ↔ 上一已发布版本”的兼容验证：

- protobuf message、字段号、RPC 方法和未知字段行为；
- Gate HTTP 与 Chat TCP 的请求/响应；
- 新旧客户端与服务端的允许组合；
- MySQL migration 的升级及必要回滚；
- 本地缓存 schema 与重启恢复；
- Docker 滚动部署时新旧服务共存和依赖顺序。

更老版本不默认进入完整矩阵。若存在长期支持版本、无法强制升级的客户端或跨多版本数据迁移，再通过专项决策扩大范围。

## 10. 隔离、超时和证据

- 端口动态分配；固定端口仅用于明确的冲突测试。
- Redis key、数据库 schema、用户、邮箱、实例和临时目录带唯一 run-id。
- 每个阻塞操作和测试有硬超时；异步正确性使用事件、future、condition variable 或协议 ready 探针，不用固定 sleep。
- teardown 必须清理本次进程、socket、key、schema、文件和线程；清理失败也使测试失败。
- 失败时保留 JUnit/XML、stdout/stderr、实例、PID、端口、seed 和非敏感业务 ID。
- 测试和 artifact 不得包含密码、Token、验证码、邮件授权码或个人连接信息。

### 10.1 本机 vcpkg 不可变门禁（DG-25）

以下规则适用于本机持久开发环境；普通构建、测试、诊断和 phase 执行授权不扩大为 package-manager 写权限：

1. vcpkg 工具检出固定为 `D:\vcpkg\test-vcpkg`，package install root 固定为
   `D:\git\Chat\vcpkg_installed`。两者的只读检查允许执行，任何内容或路径修改均需单独批准。
2. package restore/install/remove/update/upgrade、MSBuild manifest 自动安装、目录删除/重建/清理，以及
   `VCPKG_ROOT`、`VcpkgInstalledDir`、triplet、baseline、tool checkout/install root 变化，都属于受控修改。
3. 执行受控修改前必须向用户给出精确命令、精确目标、原因、影响和回滚边界，并取得针对该次操作的明确批准。
   缺包、状态不一致、编译失败或超时都不构成默示批准。
4. 本机普通 MSBuild 与测试入口必须显式传递 `/p:VcpkgManifestInstall=false` 和固定的
   `/p:VcpkgInstalledDir=D:\git\Chat\vcpkg_installed\`；运行前只读核验工具/installed tree 身份，失败时非零退出，
   不调用 `RestoreServers`，不切换到另一个安装目录，也不删除整个 tree 后重建。
5. 获批的修改必须在 Summary 中记录批准范围、实际命令、目标、开始/结束状态和残留风险；不得把一次批准复用于以后
   不同命令、不同目标或不同依赖变更。
6. GitHub 托管 runner 使用与本机隔离的 run-owned 临时 install root。已审查并合并的 workflow 可按固定
   manifest/baseline/triplet 正常恢复依赖；若要改变 CI install root、manifest、baseline、triplet 或 tool identity，
   仍须先取得批准并按 D-04 更新计划、workflow 与供应链证据。

## 11. 门禁变更流程

CI lane、Required Check、报告名或测试入口变更必须同时更新：

1. 本文；
2. `tests/TEST-CONTRACT-MATRIX.md`；
3. 对应 Module README；
4. runner/build registration；
5. workflow 和 branch protection；
6. 本地与 clean-runner 的 RED-GREEN 证据。

不得先删除旧门禁再等待新门禁补齐。迁移期间至少有一条真实路径持续阻断回归。
