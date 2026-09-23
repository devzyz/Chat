<!-- generated-by: gsd-doc-writer -->
# 测试、CI 与审核规范

## 测试分层

| 类型 | 边界 |
| --- | --- |
| Unit | 单一逻辑单元，无真实网络、Redis、MySQL、SMTP 或跨进程依赖 |
| Component | 组合少量真实模块，通过 fake/interface 隔离外部系统 |
| Integration | 使用真实协议、临时进程或可清理的 Redis/MySQL 等依赖 |
| E2E | 从公开客户端或协议入口验证完整用户流程 |
| Stress | 长时间并发、资源和竞态验证，通常手动或 nightly 执行 |

不得把访问开发者本机 Redis/MySQL 的测试标成 Unit，也不得只运行代码不做断言来制造覆盖率。

测试结构使用两个独立维度：`Foundation / Architecture / Business` 表示被保护的领域，
`Unit / Component / Integration / E2E` 表示执行边界。目录按生产模块组织，层级通过 Test ID、
README、runner 分组和报告名表达；不得建立平行的 `tests/unit`、`tests/component` 目录树。
真实 loopback 协议或子进程属于 Integration，即使运行很快且不访问公网。完整规则见
`tests/README.md` 和 `tests/auto/test-strand.md`。

## 新代码要求

- 新纯逻辑 MUST 有 Unit Test。
- Bug 修复 MUST 先添加能够复现问题的回归测试，除非无法安全复现并记录原因。
- 配置、协议、序列化、长度、端口和状态机变化 MUST 覆盖正常、边界和非法输入。
- 异步和生命周期变化 MUST 覆盖超时、取消、重复关闭和异常清理。
- Redis/MySQL/gRPC 适配器本身的正确性必须由 Integration Test 验证，不能完全 mock 掉被测行为。
- 测试不得发送真实邮件、使用个人凭据或污染共享数据库。

完整的测试设计标准以 `tests/auto/test-strand.md` 为准；该文件与本规范冲突时，以更严格且不伪造成功的一方为准。

## RED-GREEN

- 新测试 MUST 证明能够捕获回归：临时破坏断言、fixture 或受控 test seam 后应观察非零失败。
- 故意失败内容 MUST 在提交前完全恢复，并复跑 GREEN。
- 已有实现与预期不符时，应判断是测试错误还是潜在 Bug；不得为了全绿而弱化已确认契约。
- 测试未实际执行时 MUST 标记为 blocked 或 pending，禁止报告通过。

## 确定性和隔离

- 测试 MUST 独立于执行顺序、本机历史数据和个人绝对路径。
- 测试源码 MUST 按被测生产模块组织在子目录中；每个模块目录 MUST 提供 `README.md`，记录契约、范围、
  依赖与隔离、运行命令、CI 报告和已知缺口。中央 runner/project 可以保留在父目录。
- 测试移动或拆分后 MUST 同步所有构建与运行入口，不得复制源码保留旧路径，也不得减少覆盖或弱化断言。
- 临时文件、实例名、端口、Redis key 和数据库空间 SHOULD 使用测试运行唯一标识。
- 每个可能阻塞的测试 MUST 有硬超时。
- 并发测试使用可控同步原语，不以固定 sleep 作为正确性条件。
- teardown MUST 清理本次创建的进程、文件、端口、Redis key 和数据库记录。
- 失败日志 MUST 足以定位 config、实例、PID、端口、message/chat/user id 或 seed。

## 当前测试入口

从仓库根目录执行：

```powershell
.\scripts\windows-local.ps1 -Task CheckTestStructure
.\scripts\windows-local.ps1 -Task CheckTestReports
.\scripts\windows-local.ps1 -Task RunScriptTests
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
.\scripts\windows-local.ps1 -Task RunVarifyTests
.\scripts\windows-local.ps1 -Task RunAllTests -Configuration Release
```

`CheckTestStructure` verifies that every test source is registered in the real
MSBuild/CMake/npm/PowerShell runner and that every CTest target declares a test
Level. The permanent regression baseline and future-module admission contract
are defined in `tests/REGRESSION.md`.

The branch/release policy is defined in [CI governance](../tests/CI-GOVERNANCE.md).
The [contract matrix](../tests/TEST-CONTRACT-MATRIX.md) explains Test IDs and coverage;
[current status](Status.md) records validated progress and remaining work.

Reports live in `build/test-results`. Exact report groups and expected counts are owned by
`scripts/windows-local.ps1`; do not duplicate changing totals here. Each runner checks its
reports, and `RunAllTests` audits the full set. Missing reports, failed/skipped cases and
nonzero test exits fail the entry point.

Windows CI checks registration once in the static job; local entries retain the default check.
Quick develop runs upload test reports; application ZIPs are generated only in full runs.

## CI 门禁

CI 用于回归保护。develop PR/push 运行现有 Windows 单元、组件、loopback/进程测试和构建；
master PR/push、每周 develop 与手动执行增加 Linux 真实依赖和完整 E2E。
master 合并后实际 SHA 的全量与包启动冒烟成功，自动发布同一个包，不需要人工审批。

Required Checks、运行时机和失败规则统一见 [CI 治理](../tests/CI-GOVERNANCE.md)。
发布包命令与测试边界见 [发布测试入口](../tests/release/contracts/README.md)。
普通修改不要求额外阶段计划或多份证据文档；保留所属测试、失败传播、超时和资源清理。

## 代码审核清单

评审者至少检查：

1. 变更是否符合 [Architecture.md](Architecture.md) 的职责和依赖方向。
2. 所有权、异常、并发和关闭路径是否完整。
3. 配置、日志和错误中是否泄漏敏感信息。
4. 协议、Redis key 或 schema 是否影响其他消费者。
5. 是否增加个人路径、重复实现或无必要依赖。
6. 测试是否验证行为而非实现细节，是否真实执行。
7. 发布目录和未来 Linux Server 是否受影响。
8. 类和函数注释是否齐全且符合实现，名称是否准确；按 [总则](Standards.md#类与函数注释) 检查本次新增/修改范围。

## Git 规范

### 分支与命名

新任务 MUST 先获取远端更新，将本地 `develop` 快进同步到 `origin/develop`，再创建任务分支。
已有修改先保留，分叉时先查明原因，不用 reset 覆盖。任务分支经 PR 合入 `develop`，再由发布 PR 合入 `master`。

| 对象 | 必须格式 | 示例 |
| --- | --- | --- |
| 任务分支 | `<type>/<scope>/<description>` | `fix/client/message-read-state` |
| PR 标题 | `<type>(<scope>): <summary>` | `fix(client): preserve read state after late acknowledgements` |
| Commit 首行 | `<type>(<scope>): <summary>` | `fix(client): prevent late acknowledgements from downgrading read state` |

- `type` 限定为 `feat`、`fix`、`refactor`、`perf`、`docs`、`test`、`build`、`ci`、`style`、`chore`、`revert`。
  `style` 仅表示格式调整，重命名用 `refactor`，纯注释维护用 `docs`；功能连同必要测试/文档按主要目标分类。
- `scope` 限定为 `client`、`gate`、`status`、`chat`、`resource`、`verify`、`proto`、`shared`、`scripts`、`deps`、`repo`。
  `client` 表示 Qt，`chat` 表示 ChatServer，`verify` 表示现有 VarifyServer，跨多个独立模块的整体变更用 `repo`。
- 分支描述为小写英文/数字组成的 kebab-case；分支中仅允许小写英文、数字、连字符及格式中的两个斜杠。
- PR/commit 摘要 MUST 为简洁英文、动词开头、不加句号，完整首行不超过 100 字符；描述具体结果，不使用 `update code` 或 `fix bugs`。
- 不兼容变更在 scope 后加 `!`，例如 `feat(proto)!: ...`，正文提供 `BREAKING CHANGE:` 及迁移说明；分支名不加 `!`。
- 长期分支 `develop`、`master` 保留原名；`develop → master` 的 PR 标题为 `chore(repo): release <version>`，版本与 `VERSION` 一致。
- 自动生成的 merge commit 可保留默认信息；普通提交（含 squash 结果）遵守上述格式。Revert 使用 `revert(scope): ...` 并在正文记录回退 SHA。
- 新建分支、新 PR 和新提交立即执行；已有分支/提交不因本规范重写历史，自动门禁的增量边界见 [CI 治理](../tests/CI-GOVERNANCE.md#12-规范检查合同)。

### 提交与 PR 内容

- 一个提交 MUST 对应一个可说明的逻辑目标；PR 标题概括最终整体结果，commit 描述各自变更，不必与 PR 同名。
- PR 正文说明问题、最终行为、实际验证和剩余限制；按复杂度控制篇幅。复杂 commit 的正文解释原因和兼容影响，简单修改无需套空模板。
- 测试、文档和必要构建配置可与对应行为变更同提交；大规模格式化和无关重构必须拆分。
- 提交前 MUST 检查 `git diff --check` 和 staged 文件范围。
- 禁止提交缓存、构建产物、测试秘密、临时状态和意外生成文件。

规范历史治理按 [批次计划](plans/CodeConventions.md) 执行，日常局部任务无需加载该计划。

## 合并完成定义

只有相关本地测试或等价验证通过、干净 CI 全绿、artifact 正确生成、已知缺口明确记录且无规范阻断时，变更才可视为完成。
