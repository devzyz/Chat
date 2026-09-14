# Release candidate contracts

R-00 的公开入口是 `scripts/release/release.ps1`。Python 标准库负责身份、清单、ZIP 与 GitHub 证据校验；
PowerShell 入口传播非零退出，不额外解释一次“预期失败”为成功。

## 执行

在仓库根目录执行，不恢复或修改本机 vcpkg：

```powershell
powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-00-T1 -Configuration Release
```

`run_contracts.py` 按 [显式登记](../../manifests/release-reports.json) 构造测试集，要求声明与登记完全一致。
报告是 `build/test-results/release_candidate_contracts.xml`，当前实际发出 16 个 testcase。
Domain 为 Architecture，Level 为 Unit/Component；没有应用编译、真实服务或个人凭据。
临时 ZIP/目录归当前 testcase 所有，成功或失败都由 `TemporaryDirectory` 清理；公开入口期限 120 秒。

| Test ID | 实际覆盖 |
| --- | --- |
| R01-BUILD-01 | 仅 x.x.x 版本、完整 SHA、run ID、首次 attempt；拒绝 rc/beta/build 后缀 |
| R01-BUILD-02 | 同源 required checks，最新失败结果，审批/分支/retention；owner 记录身份/篡改/过期/重放拒绝 |
| R01-BUILD-03 | 已用版本或源码 SHA 不可重建，持久化 deployment seam 首次登记/重复拒绝 |
| R01-BUILD-04 | Windows 保留名、相对路径和目录穿越 |
| R01-BUILD-05 | 缺少 current-N 验收、不同 source SHA |
| R01-BUILD-06 | 私密元数据拒绝；合法依赖文件名不会误判为 Token |
| R01-BUILD-07 | 文件修改、额外文件与逐文件摘要 |
| R01-BUILD-08 | ZIP 路径冲突、穿越和大小限额 |
| R01-BUILD-09 | 每个目标一次构建的记录、工具缺失/版本漂移 |
| R01-BUILD-10 | 逐个移除必需 payload 文件、未知角色/路径 |
| R01-BUILD-11 | canonical ZIP、清单回验、拒绝覆盖与摘要漂移 |
| R01-BUILD-12 | staging 额外文件、配置中写入实际值 |
| R01-BUILD-13 | SBOM fallback 真实状态、空 dependency inventory 拒绝 |
| R01-BUILD-14 | 制品缺失、名称冲突、已过期 |
| R01-BUILD-15 | 下载字节/API digest 不符、保留期不足 |
| R01-BUILD-16 | 实际 ZIP 字节封存、attestation 伪报、JUnit 身份/失败/跳过拒绝 |

这些是合同测试，不是 16 次正式 candidate build。Deployment seam 使用确定性内存替身；ZIP 使用合成字节，
不把它们称作可运行 EXE。真正的 candidate 构建、上传和回下载必须由下面的 hosted job 另行证明。

## Hosted candidate

[Release workflow](../../../.github/workflows/release.yml) 默认只运行合同测试。正式候选要求手动 dispatch，
`build_candidate=true`、未使用的 `release_version`、同一 master SHA 的首次成功 Linux `admission_run_id`
及由 `PrepareCandidate` 生成的 `settings_receipt`。版本只接受 `x.x.x`，首次采用 `1.0.0`；
验收和发布状态单独记录，同一包晋升时不改内部版本号和字节。
禁止从 PR merge-test SHA 的验收推定另一个 merge commit 已通过。

`RegisterCandidate` 在编译前验证 GitHub admission artifact 的下载 digest、同源十个检查及首次 attempt、
owner 提交的有效设置记录（master required checks/admin enforcement、两个 protected environments 和 30 天 retention）。
它随后创建永久 deployment 记录。相同版本或 source SHA 的失败、制品过期或删除都不授权重建。
Check Runs 由上述预检验证；Deployment API 的 `required_contexts` 是 legacy commit-status 接口，
创建记录时显式传空数组，避免把 Check Run 名字误当作 commit status。参见
[GitHub deployment API](https://docs.github.com/en/rest/deployments/deployments#create-a-deployment)。
`BuildCandidate` 只允许 GitHub-hosted Windows/master/首次 attempt，复用 `windows-local.ps1` 编译，
所有恢复仅写 hosted `.ci/vcpkg_installed` 和本次 runner 临时目录。

工具锁见 [toolchain-lock.json](../../../scripts/release/toolchain-lock.json)：版本来自已验收 Windows run
34763128051 的工具日志和它引用的固定 runner-image 清单。Node 从上游浮动 `22` 收紧为该 run 实际使用的
`22.23.2`，没有升级依赖；Qt/MinGW/vcpkg 保留同一版本。Hosted image 或工具版本变化会阻断，不自动安装补齐。

每个应用打包自身 DLL；Server 附带 VS redistributable，Qt 附带 MinGW/插件，Varify 附带 node.exe 和
锁定的 production node_modules。配置模板只保留键，INI 值为空、JSON 值为 null。安装时再填写配置；
优先级和取值规则仍以 [Operations](../../../docs/Operations.md) 为准。

唯一 payload 为 `Chat-<version>-windows-x64.zip`。内部 manifest 不包含 upload 后才存在的 ID/digest。
`payload-hashes.json` 与 ZIP 一起上传；下载后才生成 `candidate-evidence.json`，核对 upload action 与 API 的
ID/digest、ZIP/manifest/每个文件的字节。外层 evidence artifact 自身的 digest 由消费者从 API 验证，
不循环写回它自己的内容。`dependency-inventory.json` 是受控 inventory fallback，不是标准 SBOM；
没有批准的 attestation generator 时明确记为 unavailable。

## 校验远端证据

`VerifyCiEvidence` 的必需参数为 `Repository`、`WorkflowFile`、`HeadSha`、`ExpectedWorkflowName`、
`ExpectedJobName`、`ExpectedArtifactName`、`ExpectedJUnitPath`、`ExpectedTestIds`、`EvidenceProfile`、
`DownloadRoot`、`TimeoutSeconds`。`ExpectedTestIds` 可传数组或逗号分隔的登记 ID。
workflow 固定 `Release` / `.github/workflows/release.yml`，job 固定 `Windows release candidate build`。

- `CandidateBuild`：artifact 为 `release-candidate-build-evidence`。
- `CandidateUpload`：主 artifact 为完整候选名称，另必需
  `ExpectedCompanionArtifactName=release-candidate-upload-evidence`。
- 两个 profile 的 JUnit 路径均为 `build/test-results/release_candidate_build.xml`；这是同一份合同测试报告的
  hosted 副本，另用 build/upload JSON 证明真实构建及上传。不能仅凭 JUnit 宣称正式构建通过。

`VerifyPlanTask -PlanTask R-00-T2` / `R-00-T3` 分别要求上述完整 CI 参数和 Build/Upload profile，
缺少 hosted 证据会非零退出。未知/后续计划任务不返回通过。校验器通过唯一同源 deployment 选择实际构建的首次手动运行；
未通过预检、未创建 reservation 的 dispatch 不冒充构建。重试、重复 reservation、
超时、跳过和内容漂移都阻断，只删除自己在 DownloadRoot 下创建的临时子目录。

## 验收边界

**已批准的 D-04 预检方案：** GitHub 的
[branch protection](https://docs.github.com/en/rest/branches/branch-protection#get-branch-protection) 和
[retention settings](https://docs.github.com/en/rest/actions/permissions#get-artifact-and-log-retention-settings-for-a-repository)
读取接口需要 `Administration: read`；它不在
[`GITHUB_TOKEN` 可声明权限](https://docs.github.com/en/actions/reference/workflows-and-actions/workflow-syntax#permissions)
中。用户于 2026-09-14 批准管理员在 CI 外使用已有登录只读核查；自动化不接收个人 Token、App 私钥，
也不扩大默认 workflow 权限。配置请求及批准策略见
[repository-settings.proposed.json](../../../scripts/release/repository-settings.proposed.json)。

管理员把 repository、master 完整 sourceSha、version 和 admissionRunId 写入输入 JSON，例如：

```json
{"repository":"devzyz/Chat","sourceSha":"<40-character-master-SHA>","version":"1.0.0","admissionRunId":123456}
```

在仓库根目录运行（输入中的 SHA/run 必须换成实际已验收身份）：

```powershell
./scripts/release/release.ps1 -Task PrepareCandidate -InputFile candidate-input.json -OutputFile candidate-dispatch.json
Get-Content -Raw -Encoding UTF8 candidate-dispatch.json | gh workflow run release.yml --repo devzyz/Chat --ref master --json
if ($LASTEXITCODE -ne 0) { throw 'Candidate dispatch failed.' }
```

记录包含完整设置快照及其摘要，绑定 owner ID、repository、SHA、版本、随机 challenge 与 900 秒期限。
hosted 准入使用 GitHub run API 的 actor/triggering actor、实际 dispatch event 和首次 attempt 验证来源；
哈希不是签名，信任来自 GitHub 认证的 owner 提交。任一身份漂移、过期、内容摘要不匹配或
已消费 challenge 都阻断。版本并发锁与持久化 deployment 记录避免重复构建。
15 分钟限制准入时间，不限制构建耗时；排队过期必须重新核查，不能编辑旧时间戳继续使用。
核查到准入间设置可能变化，这是该管理员辅助方案的明确边界。

正式 R-00 仍须使包含发布工具的 source SHA 通过上游验收，再执行一次
hosted candidate job 和两个 CI evidence profile。本地合同成功不关闭 G-018。
artifact-only 启动/探测/停止、版本化 UAT checklist 和 promotion 属于后续 R-01/R-02/R-03；
本次只打包可用的 verifier，不提供会返回假 PASS 的 smoke/UAT 占位实现。

API 合同参考 [GitHub environments](https://docs.github.com/en/rest/deployments/environments) 和
[GitHub artifacts](https://docs.github.com/en/rest/actions/artifacts)。运行时未知字段/权限缺失以拒绝为准。
