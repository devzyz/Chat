# CI 与自动测试规范

目标：新增功能和重构后，已有功能的可观察结果保持不变。单元测试为主体，真实依赖集成和 E2E 补足进程、网络与数据边界。
本次用户确认的双分支方案取代旧发布审批、首次 attempt 和版本占用规则；历史计划保留执行记录，不再作为这些机制的实施要求。

## 1. 分支与运行时机

默认分支为 develop；功能分支通过 PR 合入 develop，再通过 PR 合入 master。
入口为 `.github/workflows/ci.yml`，Windows、Linux、Release 文件作为 reusable workflow，不再分别触发重复运行。

| 事件 | 必需验证 | 发布 |
| --- | --- | --- |
| develop PR / push | Windows 全部单元、确定性组件、loopback/进程集成、编译和静态检查 | 否 |
| 每周一北京时间 03:17 | 默认分支 develop 的 Windows 回归和 Linux 全量集成/E2E | 否 |
| master PR | 同上全量检查，成功才允许合并 | 否 |
| master push | 实际合并 SHA 的全量检查、Windows 包启动冒烟 | 成功后自动发布 |
| workflow_dispatch | 所选分支全量检查 | 否 |

GitHub schedule 使用默认分支，可能延迟。默认分支须配置为 develop。
develop Required Check 为 `Regression checks`；master 为 `Regression checks` 和 `Full regression checks`。
两者都是汇总真实 job 结果，失败或 skipped 不算通过。master 禁止直接推送、强推和删除，不要求人工审批。
切换保护检查须在新工作流出现并验证后完成，避免只改名称导致合并失去保护或永久等待。

### 1.1 工具链维护周期

日常 PR、push 和普通手动运行使用已验证的 Windows 工具链版本；每周在默认分支获取
最新稳定 PowerShell/CMake/Ninja，以及 runner 提供的最新 MSVC 2022/SDK，执行双平台冷构建和完整回归。
只有完整回归成功且依赖缓存已保存，才发布新的不可变工具链记录供日常 CI 选择；失败保留旧记录。
显式手动刷新仅允许默认分支的 `refresh_tools=true`。PR、其他分支或其他 workflow 的记录不可被采用。
本次调整由用户要求的“每周升级、日常固定”策略授权；不升级业务依赖、Qt、Node、GCC 或 vcpkg baseline。
编译器漂移在依赖恢复前失败，不自动修改 Visual Studio、不静默回退。首次引导、缓存和记录保留期
见 [构建测试入口](build/README.md#validated-weekly-windows-toolchain)。本机 DG-25 边界不变。

### 1.2 规范检查合同

命名和注释标准见 [Standards](../docs/Standards.md#类与函数注释)，Git 格式见
[Quality](../docs/Quality.md#git-规范)。以下为待实现的门禁合同，规则先由评审执行；本次文档落地不代表 CI 已启用。

- Git 检查覆盖 PR 来源分支、标题及目标分支尚未包含的新增普通提交；标题编辑后重跑。
  push 检查本次新增提交，不扫描全部历史。历史豁免以门禁启用时固定的 SHA/分支清单为界，范围不得自动扩大。
- 代码检查覆盖新增类/函数及修改的旧函数、所属类，不因同文件未触及的遗留项阻塞。使用语法感知定位，排除生成和第三方代码；未支持的语言显式交给评审，不伪报检查通过。
- 自动检查负责格式、非空注释和参数匹配；业务命名、注释真实性、所有权与线程语义由评审确认。
- 违规 MUST 返回非零并定位对象；读取失败不能当作无违规。PR 元数据作为数据传入，不拼接为可执行 shell。
- 检查器先用有效/无效样例验证，再接入现有汇总检查；按 [治理计划](../docs/plans/CodeConventions.md) 分批启用，未实现前不增加悬空 Required Check。

## 2. 回归测试

保留已有业务行为测试。快速流程的“快速”指不启用 Docker 真实依赖和完整 E2E，不省略单元测试；首次冷依赖构建仍可能较慢。
全量流程额外运行 POSIX 生命周期、Linux 同源构建、MySQL/Redis/SMTP、四服务和双客户端双服务完整 E2E。

Windows 静态 job 统一执行一次 `CheckTestStructure`；其成功后，各测试入口使用
`-SkipTestStructureCheck` 复用同一提交的检查结果。该参数仅允许 GitHub CI 的四个测试入口使用，
本地独立运行默认保留检查，`RunAllTests` 在同一进程内只检查一次。测试执行和报告校验不跳过。
Server 由 `RunServerTests` 一次构建生产与测试目标，不先单独调用 `BuildServers`。
develop PR/push 保留所有测试及报告上传，不生成或上传应用 ZIP；master、每周和手动全量保留打包。

- 测输入输出、状态与错误行为，不绑定私有容器、文件排列和内部调用顺序。
- 修复缺陷时补能复现问题的回归；重构不改变行为预期。功能确需改变旧行为，在变更说明中说明并更新相关测试。
- 测试失败、必需报告缺失、超时和清理失败都返回非零；不自动重试业务测试刷绿。
- 基础设施故障允许排查后重跑，保留原始失败记录，不限制首次 attempt。
- 测试代码和 runner 是执行清单；报告数从实际测试生成。已有防漏跑校验保留，不再手工增加一套发布 Test ID 账本。
- 所有临时数据、进程和端口只清理本次创建的资源。日志不输出真实凭据。

### 2.1 比例化执行合同

文档修改检查事实、链接和 diff；行为修改运行所属模块回归；网络或数据修改补相关集成。
跨流程修改验证触发、失败传播和受影响入口；全量运行用于 master、周检和明确的全量验收。
不要求普通修改另写计划、威胁模型、多份总结或重复 mutation。已有业务测试不因流程精简而删除。

## 3. 发布

合入 master 前在 `VERSION` 更新 x.x.x 版本。发布自动串行执行，不要求环境审批或 owner 凭据。
直接组装本次 CI 已测试的 Windows ZIP，不再为发布重新编译。

1. 全量检查成功后，从同一次 run 下载 Server、Qt、Varify 包。
2. 补齐独立运行库、Node、proto、SQL 和配置模板，生成 SHA256SUMS 与源码 SHA 清单。
3. 新 Windows runner 下载并解压候选包，在源码目录之外验证文件、程序加载、启动和清理。
4. 创建或继续同 SHA 的未发布草稿，上传同一个 ZIP，再下载核对字节后公开 Release。

Windows 冒烟不提供真实 MySQL/Redis；完整业务由同 SHA 的 Linux 真实依赖 E2E 验证。
不将包启动冒烟称为 Windows 真实数据库端到端验证。
未发布构建允许重试，不占用版本或源码；已发布版本和文件不覆盖，需要新的 VERSION。
定时、PR、手动全量检查均不发布。N-1 完整矩阵暂缓，已有协议和 schema 迁移回归继续运行。

命令与实际验证边界见 [发布测试入口](release/contracts/README.md)。

## 10. 本地依赖保护

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
