---
document: tests/plans/PHASE-RELEASE-PLAN.md
phase: Release gate
title: Build-once Windows x64 artifact, artifact-only smoke, same-digest UAT, and same-bytes promotion
status: Planned
plan_ids: [R-00, R-01, R-02, R-03]
plan_count: 4
wave_range: [19, 22]
waves: 4
depends_on:
  - "Phase 3A complete with accepted production Module and Windows develop-gate evidence"
  - "Phase 3B complete with accepted production-transport/process and Windows develop-gate evidence"
  - "Phase 3C complete with accepted hosted-Ubuntu real-Adapter, schema, four-process, and compatibility/bootstrap evidence"
  - "Phase 3D complete with accepted hosted-Ubuntu two-server business E2E and compatibility/bootstrap evidence"
requirements: [G-017, G-018]
locked_decisions: [DG-09, DG-10, DG-11, DG-12, DG-13, DG-17, DG-18, DG-21, DG-22, DG-25]
autonomous: false
user_setup:
  - service: github-environments
    why: "Human approval must be separate from build and promotion permissions"
    dashboard_config:
      - task: "Create protected release-uat and release-promotion environments with required reviewers and deployment/tag restrictions"
        location: "Repository Settings -> Environments"
      - task: "Create or confirm a protected release-smoke-windows environment only if formal Windows startup requires run-scoped disposable dependency endpoints"
        location: "Repository Settings -> Environments"
  - service: github-rulesets
    why: "Master and release admission must require the exact evidence-producing checks"
    dashboard_config:
      - task: "Require the six inherited master checks and the three release-admission checks listed in section 3 after those checks exist"
        location: "Repository Settings -> Rules -> Rulesets / Branch protection"
  - service: github-actions
    why: "Candidate retention and workflow permissions must cover the full UAT and promotion window"
    dashboard_config:
      - task: "Allow 30-day Actions artifact retention and workflow-created evidence/pointer pull requests; do not grant a personal PAT"
        location: "Repository Settings -> Actions -> General"
must_haves:
  truths:
    - "One accepted Phase 3D SHA is compiled exactly once into one unified versioned Windows x64 candidate payload."
    - "The payload contains Gate, Status, Chat, Qt client, Varify, runtime dependencies, config templates, canonical proto, migrations, manifest, per-file hashes, and auditable dependency inventory/SBOM status."
    - "Every downstream automated or human gate names the same candidate artifact ID, Actions digest, payload SHA-256, and internal manifest SHA-256."
    - "Artifact smoke installs, configures, starts, probes, stops, and cleans only files downloaded from the candidate; a source checkout cannot supplement missing bytes."
    - "A user signs the versioned UAT checklist for that exact digest; any candidate-byte, manifest, checklist-version, or evidence change invalidates the sign-off."
    - "Promotion uploads the original candidate payload bytes to one immutable tag/Release asset without rebuilding, repackaging, or overwriting."
    - "The first successful immutable release creates the complete N-1 pointer; each later promotion advances it to the just-promoted immutable release for the next cycle."
  artifacts:
    - "planned .github/workflows/release.yml"
    - "planned scripts/release/release.ps1 and ReleaseGate.psm1"
    - "planned tests/release schemas, contract tests, smoke tests, UAT checklist/evidence validator, promotion tests, and Module README files"
    - "planned tests/manifests/release-reports.json with actual registrations only"
    - "planned unified Chat-<version>-windows-x64.zip and detached candidate/release evidence"
    - "planned tests/compatibility/baselines/n-minus-1.json metadata pointer"
  key_links:
    - "accepted source SHA + inherited required checks -> build-once candidate deployment record"
    - "candidate artifact ID + Actions digest -> canonical payload SHA-256 -> internal manifest/per-file hashes"
    - "downloaded candidate -> packaged verifier/smoke runner -> JUnit/evidence -> release-admission check"
    - "candidate identity + smoke evidence + signed UAT evidence -> GitHubReleasePromotionAdapter -> exact Release asset"
    - "promoted Release asset digest + schema/migration identity -> N-1 pointer -> Phase 3C/3D compatibility resolver"
---

# Release gate 正式执行计划

状态：**Planned（尚未执行）**

本文件完整展开 R-00..R-03。所有标为 planned 的路径、symbol、Test ID range、report family、workflow check、
artifact identity 与 GitHub environment 都不是当前仓库事实；执行时必须先读取 Phase 3A/3B/3C/3D 的实际 Summary、
runner manifest 和远端 check evidence，再把 planned 名称映射到真实 Interface。不得为匹配本文命名重建同义 Module。

当前继承的 12-report/180-testcase baseline（Phase 2.5 的 173 case 加 3A-01 的 7 case）必须保留。Phase 3A..3D 实施后会产生实际新增报告与 testcase；Release
gate 只从真实 upstream manifest 继承数字，并只从真实 release-test registration 更新自己的数字。本计划不会预填最终
report/testcase 总数，也不会把 planned Test ID range 的大小当作 runner case 数。

## 1. 目标

1. 依据 D-12、DG-09、DG-17 和 G-018，从已经通过 Phase 3D `master/release` admission 的同一 source SHA 在
   GitHub-hosted `windows-2022` 上只编译一次，形成一个统一、版本化、不可覆盖的 Windows x64 candidate payload。
2. 依据 DG-12、DG-17、DG-21，用该 candidate 的 artifact ID、Actions digest、payload SHA-256、internal manifest
   SHA-256 和逐文件 SHA-256 建立可审计身份；smoke、适用 compatibility、UAT 与 promotion 全部引用这组身份。
3. 依据 D-12、DG-22，downstream job 不 checkout source 来重建或补齐被测文件，只下载、校验、安装、配置、启动、
   兼容、停止和清理 R-00 的 candidate；缺文件、不同 bytes、不可用 runner、timeout 或 cleanup failure 均阻断。
4. 用户在自动 smoke 全绿后，对同一 digest 执行仓库版本化 UAT checklist 并签署 evidence；任一失败或 byte/checklist
   漂移立即使签核无效，可自动化缺陷回到最早 owning 3B/3C/3D regression gate。
5. R-03 只把原 candidate payload bytes 晋升为 tag/Release asset，再次下载并复核 digest/evidence chain；第一次成功
   晋升建立完整 N-1 artifact/schema pointer，后续 release 维护该 pointer 供下一周期使用。

## 2. 硬前置与执行授权

R-00 开始前必须同时满足：

- `PHASE-3A-PLAN.md`、`PHASE-3B-PLAN.md`、`PHASE-3C-PLAN.md`、`PHASE-3D-PLAN.md` 均已有接受的 completion
  Summary。仅 Planned 文件、局部 focused run 或 Adapter-only evidence 不满足前置。
- candidate source SHA 与 Phase 3D release-admission record 完全相同；它的 source tree、submodule/lock identity 与
  upstream report hashes 已冻结。任何 SHA 或 lock 变化都产生新 candidate identity，旧 evidence 不继承。
- 同一 SHA 的 section 3.1 六个 master checks 均为 authoritative `success`；`skipped`、`neutral`、`cancelled`、
  unavailable、timeout、missing report、cleanup failure、retry-to-green 或 waiver 都不是 success。
- Phase 3C/3D compatibility 状态要么是实际 supported N/N-1 results，要么是精确非-PASS
  `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`。首发 bootstrap 不阻止 current-N release，但不能被记录为 compatibility PASS，
  也不能提前关闭 G-017。
- GitHub Actions 能兑现 30 天 candidate artifact retention；若 repository policy/plan 上限不足 30 天，R-00 在编译前
  停止，由管理员调整 policy。不得静默缩短 UAT window 或依赖即将过期的 artifact。
- `release-uat` 与 `release-promotion` protected environments、required reviewers、tag restrictions、workflow token
  permissions 与 concurrency policy 已由 read-only preflight 验证。缺权限时停止，不回退到个人 PAT。
- DG-25 已在全部 upstream phase 落实：本机 `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed`
  保持只读，本地构建/测试显式禁用 manifest 自动安装。R-00 只能在 GitHub-hosted runner 的 run-owned 临时 install root
  中按已批准 manifest/baseline/triplet 恢复；任何本机 package 修改或本机/CI root、manifest、baseline、triplet、tool
  identity 变化，都必须先展示精确命令/目标/影响并取得用户明确批准及 D-04 评审。

任何前置缺失都保留可审计 blocker；不得用旧 workflow zip、Actions cache、local build、source rebuild、个人凭据、
共享服务、公共 SMTP、fake EXE mode、测试宏或 private state 替代。

### 2.1 比例化执行与共享 release 前置

过程分层以 [`tests/CI-GOVERNANCE.md` section 2.1](../CI-GOVERNANCE.md#21-比例化执行合同) 为唯一权威。R-00 phase entry
一次性读取四个 upstream Summary/manifest、DG-09..DG-25、G-018、同一 SHA checks、permissions/retention 与当前 baseline；
后续 task 的 `read_first` 只保留 edited source、closest analog 与一个必要 authority。R-00-T1 创建的公开
`scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask <id>` 封装本计划各 selector 的 hard deadline、focused
RED/GREEN、meaningful artifact/lifecycle/compatibility/gate mutation、远端 run/artifact identity polling、JUnit 与确定性非零
合同；`-ExpectRed` 仅在底层因指定合同稳定失败时把 wrapper 视为成功。每 plan 代码稳定后最后一个 task 运行 owning
selector 一次。secret/cleanup 仅由实际处理 artifact/data/log 或创建 process/port/temp 的 task 执行，R-03-T3 再聚合一次
release report/secret/residue/diff/evidence-chain closeout；不重跑 upstream full lanes，也不把 master/release evidence降为本地声明。

## 3. Required checks、permissions、environment 与 retention

### 3.1 `master` admission：R-00 必须复核的 exact checks

| Check | Owner | Release gate behavior |
| --- | --- | --- |
| `Static configuration checks` | inherited Windows develop gate | exact name and success on candidate SHA required |
| `Server Release build` | inherited Windows develop gate | exact name and success on candidate SHA required |
| `Qt client Release` | inherited Windows develop gate | exact name and success on candidate SHA required |
| `VarifyServer dependency and package check` | inherited Windows develop gate | exact name and success on candidate SHA required |
| `Linux real dependencies and compatibility` | Phase 3C | actual supported result or explicit bootstrap status; check itself must succeed |
| `Linux two-server business E2E` | Phase 3D | current-N two-server journey must succeed; actual N-1 or explicit bootstrap recorded |

### 3.2 Release admission 与 publication checks

| Check | Produced by | Required meaning |
| --- | --- | --- |
| `Windows release candidate build` | R-00 | one successful run attempt, one candidate record, one immutable Actions artifact, complete manifest/provenance/SBOM status |
| `Windows artifact-only smoke and compatibility` | R-01 | exact candidate download, no source supplement, install/config/start/probe/compat/stop/cleanup evidence |
| `Release UAT evidence` | R-02 | protected human approval and validator success for exact candidate/checklist identity |
| `Release promotion integrity` | R-03 | same payload bytes on immutable tag/Release asset, durable evidence, correct N-1 pointer |

前 3 个 release admission checks 必须在 `release-promotion` environment 解锁前成功。`Release promotion integrity`
是 publication 的最终 blocking check；它不能通过覆盖 tag/asset、删除首次失败或 rerun 构建获得绿色。

### 3.3 Least privilege 与 approvals

| Stage | Workflow permissions | Environment / approval |
| --- | --- | --- |
| R-00 | `contents: read`, `actions: read`, `deployments: write`; attestation 子 job 条件启用 `id-token: write`, `attestations: write` | no human approval after six master checks; candidate deployment record is created before compilation |
| R-01 | `contents: read` only for workflow metadata, `actions: read`; no `contents: write`, no deployment secret | protected `release-smoke-windows` only when run-scoped dependency endpoints are required; hosted Windows runner remains authoritative |
| R-02 | `contents: read`, `actions: read`, `pull-requests: read`; evidence submission PR uses normal contributor identity | `release-uat` requires the user/release owner as reviewer; environment stores no application secret |
| R-03 | `contents: write`, `actions: read`, `pull-requests: write`, `deployments: write`; no package/secret administration | `release-promotion` requires a different explicit approval after R-00/R-01/R-02; one concurrency group per version |

`GITHUB_TOKEN` is the only automation credential. If organization policy prevents the required narrow operation, the stage stops for
an administrator-controlled configuration change; it never requests or logs a personal PAT, password, token value or long-lived key.

### 3.4 Retention

- Candidate Actions artifact、R-00 build evidence、R-01 smoke/compat reports 与 R-02 pending evidence：**30 days**。Upload
  必须显式 `retention-days: 30`、`overwrite: false`、`if-no-files-found: error`；repo policy 无法兑现时不构建。
- Candidate deployment/tombstone record：保留在 GitHub deployment history；artifact 过期、失败或删除不授权相同
  `{release_version, source_sha}` 重建。
- Published payload、release manifest、smoke report hashes、UAT evidence、promotion evidence 与 N-1 metadata：随 immutable
  GitHub Release 长期保存，不以 Actions artifact 为唯一副本。
- 普通 workflow log 不得承担唯一 provenance；所有 release-critical identity 必须进入 allowlisted durable evidence。

## 4. 明确非目标

- 不制作 Gate、Status、Chat、Varify application Docker image，不设计 production container orchestration（DG-10）。
- 不发布 Linux binaries；Linux evidence 仍是 Phase 3C/3D Integration/E2E authority，Windows x64 是本 Release payload
  （DG-18）。
- 不新增 self-hosted Required runner，不使用个人 Windows/Linux 机器、本地 VM、MobaXterm 会话或共享依赖作证据。
- 不在 Release gate 修复 Phase 3A..3D behavior。artifact smoke 或 UAT 缺陷回到最早 owning phase，产生新 accepted SHA
  和新 candidate identity；不得直接 patch staged payload。
- 不扩大默认兼容到 N-2，不从旧 source/cache 重建 N-1，不把 descriptor/wire fixture 写成实际 process PASS。
- 不升级 Qt、Node/npm、vcpkg/C++ dependencies，不新增 package-manager dependency；任何 lock/install 变化先完成独立
  legitimacy、compatibility 与 D-04 评审。
- 不宣称公网 production security、完整 ASVS compliance、网络 exactly-once、自动数据 rollback 或操作系统服务注册已
  被 artifact smoke 隐式验证。
- 不把 attestation 可用性当作发布硬依赖；manifest/hash/provenance 是必需合同，attestation 是条件增强且状态必须真实。

## 5. Production artifact 与 promotion Interfaces

### 5.1 CandidateArtifact Interface

执行时优先复用 Phase 3C 实际 `ArtifactResolver`/`ManifestVerifier`；若其 Interface 已覆盖本节，不创建新的 wrapper。
planned `ReleaseGate.psm1` 只组合以下外部 Interface，不复制 archive/hash/parser 逻辑：

```text
RegisterBuildOnce(version, accepted_source_sha, upstream_evidence) -> CandidateRecord
VerifyCandidate(candidate_identity, downloaded_root) -> VerifiedCandidate
RunArtifactSmoke(verified_candidate, run_context) -> SmokeEvidence
ValidateUat(verified_candidate, checklist, signed_evidence) -> UatDecision
PromoteExact(verified_candidate, uat_decision, promotion_adapter) -> PromotionEvidence
```

`CandidateIdentity` 的必需字段：

- release version、accepted source SHA、source tree/lock digest、workflow name、run ID、run attempt、job identity；
- Actions artifact ID、unique artifact name、Actions artifact digest；
- canonical payload relative path、payload byte size、payload SHA-256；
- internal `release-manifest.json` SHA-256 与 manifest schema version；
- upstream Phase 3C/3D report/compatibility hashes及 bootstrap/actual-N-1 status；
- candidate record/deployment ID、created UTC、retention expiry UTC。

artifact ID 等 upload 后才可得的字段只进入 detached `candidate-evidence.json`；不得回写 payload 或 manifest 导致 bytes
变化。Candidate Interface 的调用者只学习 identity、verification result、stable failure 和 evidence path；zip extraction、
path normalization、hash streaming、GitHub response、retry/cleanup 细节属于 Implementation。

### 5.2 Internal release manifest

payload 内 `release-manifest.json` 至少包含：release version、source SHA/tree digest、build run identity、Windows runner
image identity、Visual Studio/MSBuild、CMake/Ninja、Qt/MinGW、Node/npm、vcpkg baseline/triplet、action full SHAs、lockfile
hashes、canonical proto hashes、ordered migration IDs/checksums、config-template list，以及每个 relative file 的
`{role,size,sha256}`。路径必须 normalized relative，不允许 absolute/UNC/drive prefix、`..`、duplicate/case collision、
reparse link 或 manifest 未登记文件。

manifest 与 candidate evidence 采用 allowlist serialization；不得包含 environment dump、password、token、verification
code、email credential、real email、connection string、absolute runner path、temporary directory或日志 body。

### 5.3 SBOM / controlled fallback

- R-00 总是生成可执行 `dependency-inventory.json`：vcpkg manifest/status、npm lock packages、runtime DLL inventory、
  tool/action identities、license/provenance references 与 lock hashes。
- 若仓库已批准并 pin 了可用 SBOM generator，另生成标准 machine-readable `sbom.json`，记录 generator identity/digest
  和 input hashes；工具失败使 R-00 失败，不以空文件通过。
- 若 repository plan/runner 不提供已批准 generator，受控 fallback 只允许
  `sbom_status: CONTROLLED_DEPENDENCY_INVENTORY_FALLBACK`，同时记录 reason、approver policy reference 和完整
  `dependency-inventory.json`。UI/Release notes 不得称其为标准 SBOM；inventory 缺失或把 fallback 标成完整 SBOM 均阻断。
- 不为生成 SBOM 临时 `npm install`、`pip install`、`cargo install` 或下载未审计二进制。

### 5.4 ReleasePromotion Interface

planned `ReleasePromotion` 只有一个外部操作：

```text
PromoteExact(CandidateIdentity, ValidatedUatEvidence, TagSpec) -> PromotionEvidence
```

真实 `GitHubReleasePromotionAdapter` 与 deterministic local filesystem Adapter 构成真实 seam。Interface 隐藏 draft
Release、tag creation、asset upload/download、API pagination 和 rollback metadata；caller 只提供 candidate identity、
validated UAT evidence 与新 tag。Adapter 必须拒绝 existing tag/release/asset、overwrite、different source SHA、different
payload digest、repacked archive、missing evidence 或 second promotion。

## 6. Unified Windows x64 payload contract

R-00 产出一个 canonical `Chat-<version>-windows-x64.zip`。它在一次 build job 内形成，后续不得重新压缩。内部至少：

```text
GateServer/          GateServer.exe + app-local runtime DLLs + config template
StatusServer/        StatusServer.exe + app-local runtime DLLs + config template
ChatServer/          ChatServer.exe + app-local runtime DLLs + multi-instance config templates
chat-client/         chat.exe + Qt/compiler runtime + plugins + static assets + config template
VarifyServer/        production JS/JSON + exact package-lock + restored production node_modules + config template
proto/               canonical varify/status/chat proto files
migrations/          immutable migration manifest + ordered SQL + schema/fixture metadata allowed for release
release-tools/       packaged verifier, safe extractor, artifact-smoke runner, versioned checklist helper
release-manifest.json
dependency-inventory.json
sbom.json            only when approved generator produced it; otherwise manifest records controlled fallback
```

每个 application directory 必须独立满足 runtime dependency inventory；禁止从 runner PATH、source workspace、vcpkg tree、
Qt install、global npm cache 或 sibling package 偷取缺失文件。Config 是无值 template，列出 required key/precedence/range，
不得包含真实 endpoint/secret。Canonical proto 和 migrations 与 accepted source SHA 的 upstream evidence hash 必须一致。

## 7. Planned Test ID 与 report ownership

| Plan | Planned Test ID range | Contract family | Planned report family |
| --- | --- | --- | --- |
| R-00 | `R01-BUILD-01..16` | accepted-SHA admission, build-once record, payload layout, manifest/hash/toolchain/provenance/SBOM status | `release_candidate_build.xml` |
| R-01 | `R01-SMOKE-01..20` | exact artifact download, safe extraction, clean install/config/start/probe/compat/stop/cleanup | `release_artifact_smoke.xml`, `release_artifact_compatibility.xml` |
| R-02 | `R01-UAT-01..14` | checklist/evidence schema, digest binding, signer/approval, failure and byte-change invalidation | `release_uat_validation.xml` |
| R-03 | `R01-PROMOTE-01..16` | exact-byte promotion, tag/asset immutability, evidence durability, rollback metadata, N-1 pointer | `release_promotion.xml` |

Ranges 是 planned stable namespace，不是 runner testcase 或 checklist item 数。一个 ID 可由多个 runner cases 证明，实际
case/report 只有在 non-empty source、real registration 与 emitted JUnit 存在后才进入
`tests/manifests/release-reports.json`。UAT checklist item 使用独立 `UAT-WIN-*` identity，不冒充自动 testcase。

## 8. Dependency DAG 与 waves

```text
Phase 3A complete
  -> Phase 3B complete
    -> Phase 3C complete
      -> Phase 3D accepted SHA + six exact master checks
        -> R-00 build once + immutable candidate identity
          -> R-01 artifact-only smoke + applicable compatibility
            -> R-02 protected same-digest human UAT
              -> R-03 same-bytes tag/Release promotion
                -> first/next immutable N-1 pointer
```

| Wave | Plan | Depends on | Exclusive ownership |
| ---: | --- | --- | --- |
| 19 | R-00 | Phase 3A/3B/3C/3D accepted | build-once registry, unified staging, manifest, payload, candidate upload/evidence |
| 20 | R-01 | R-00 PASS | artifact-only resolver/verifier, install/config/start/compat/stop/cleanup evidence |
| 21 | R-02 | R-01 PASS | versioned UAT checklist, signed evidence, protected human checkpoint |
| 22 | R-03 | R-02 PASS | promotion adapter, tag/Release, durable evidence, N-1 pointer and closeout |

同一 release version 由 workflow concurrency `release-<normalized-version>` 串行；不得让 R-00..03 同时修改 candidate、
tag、Release 或 pointer metadata。

## 9. Plan R-00 — Build once and create one immutable candidate

Status: **Planned**
Wave: **19**
Depends on: **Phase 3D accepted SHA and all upstream evidence**
Coverage: **G-018; DG-09, DG-11, DG-12, DG-17, DG-18**

<objective>
从同一已接受 SHA 在一个 pinned hosted-Windows job 中只构建一次 Gate/Status/Chat/Qt/Varify，形成一个 unified canonical
payload；冻结 manifest、逐文件 hash、toolchain/provenance、dependency inventory/SBOM status，并上传一个不可覆盖的
candidate artifact。任何 build attempt 或 payload bytes 的变化都会产生失败或新的 release identity，不继承旧 evidence。
</objective>

<tasks>

<task id="R-00-T1" type="tdd">
<name>RED：冻结 accepted-SHA、build-once registry、manifest 和 payload contract</name>
<read_first>
- `tests/plans/PHASE-3A-PLAN.md` and accepted Phase 3A Summary/manifest
- `tests/plans/PHASE-3B-PLAN.md` and accepted Phase 3B Summary/manifest
- `tests/plans/PHASE-3C-PLAN.md` and accepted Phase 3C Summary/service/schema/compatibility manifests
</read_first>
<files>
- `scripts/release/ReleaseGate.psm1` (planned; create)
- `scripts/release/release.ps1` (planned; create public runner)
- `tests/release/contracts/release-manifest.schema.json` (planned; create)
- `tests/release/contracts/candidate-evidence.schema.json` (planned; create)
- `tests/release/contracts/release_artifact_contract.tests.ps1` (planned; create, non-empty)
- `tests/release/contracts/README.md` (planned; create)
- `tests/manifests/release-reports.json` (planned; create from real registration, without guessed counts)
- `.github/workflows/release.yml` (planned; create)
</files>
<action>
Per DG-09/DG-17 and D-12, first map actual upstream target, report, manifest, migration, compatibility and accepted-SHA paths.
Define `CandidateIdentity`, normalized manifest path rules, exact unified payload roles from section 6 and detached upload evidence.
Define `RegisterBuildOnce` so the workflow creates a candidate deployment/tombstone record before compilation and rejects
`github.run_attempt != 1`, an existing release-version record, an existing `{version,source_sha}` candidate, a different Phase 3D
accepted SHA, or any missing/non-success master check. A failed build record remains auditable and cannot be erased to authorize a
same-identity rebuild; a fixed release requires a new accepted SHA and candidate record. Add RED `R01-BUILD-01..08` for missing
upstream check/evidence, duplicate candidate, second attempt, invalid version/tag, absolute/traversal/case-colliding path, missing
required payload role, secret-valued manifest field and source/lock mismatch. Freeze the controlled SBOM fallback state exactly as
section 5.3; empty inventory or fallback mislabeled as standard SBOM is RED. The runner `release.ps1` exposes bounded commands
`PreflightCandidate`, `BuildCandidate`, `VerifyCandidate`, `SmokeCandidate`, `ValidateUat`, `PromoteCandidate`; it does not add a
second manifest parser when the actual Phase 3C verifier can be reused.
Implement `VerifyPlanTask` in the public runner in this task. It maps every R-00..R-03 task ID to the exact focused/remote evidence
profile specified by that task, enforces its deadline and expected JUnit IDs, and rejects unknown IDs, ambiguous runs, retries,
digest drift, missing reports or cleanup failure with non-zero exit. This replaces duplicated inline `gh` polling verifiers without
changing hosted-runner authority, build-once/same-bytes, UAT or promotion acceptance behavior.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-00-T1 -Configuration Release -ExpectRed</automated>
</verify>
<acceptance_criteria>
- The accepted SHA and six master checks are exact, queryable prerequisites; no rerun/waiver/skipped status can pass.
- Candidate identity and manifest schemas cover upload-external and payload-internal identities without circularly modifying bytes.
- Build-once remains enforceable after a failed/expired artifact through a durable candidate record.
- Required payload roles, safe paths, redaction and SBOM/fallback claims are machine-validated; actual runner counts remain unknown.
</acceptance_criteria>
</task>

<task id="R-00-T2" type="auto" tdd="true">
<name>GREEN/mutation：在一个 Windows job 内构建和 stage 统一 release set</name>
<read_first>
- `scripts/release/release.ps1` and `ReleaseGate.psm1` (planned output of R-00-T1)
- actual Phase 3A/3B/3C production target registry and Summaries
- `.github/workflows/windows-ci.yml`
</read_first>
<files>
- `scripts/release/release.ps1` (planned; implement/modify)
- `scripts/release/ReleaseGate.psm1` (planned; implement/modify)
- `scripts/release/payload-layout.json` (planned; create)
- `tests/release/build/release_build.tests.ps1` (planned; create, non-empty)
- `tests/release/build/README.md` (planned; create)
- `.github/workflows/release.yml` (planned; modify)
</files>
<action>
Create one `windows-2022` job with a 300-minute hard timeout, full-SHA-pinned actions, the exact accepted checkout SHA and the
same approved Visual Studio, vcpkg baseline/triplet, Qt/MinGW, CMake/Ninja and Node/npm locks used by upstream gates. After the
build-once record exists, compile each Gate/Status/Chat production target, Qt client target and Varify production dependency tree
exactly once in that job; do not consume the separate short-lived ZIPs from the current Windows workflow and do not start a second
build after any failure. Stage one run-owned directory from those outputs: each app-local runtime, no-value configs, static assets,
canonical proto, exact migrations, packaged release verifier/smoke/UAT helper, dependency inventory and allowed SBOM output. Varify
uses `npm ci --ignore-scripts` with the committed lock; any lock change or lifecycle script is a stop. Generate the internal manifest
from an explicit allowlist after staging, hash every non-manifest file, then hash the manifest and create the canonical payload ZIP
once. Do not patch/recompress it later. Add `R01-BUILD-09..13`; mutations remove one runtime DLL/plugin/node dependency/config/proto/
migration, introduce an undeclared file/symlink/reparse point, alter a tool/lock identity, insert a secret canary or invoke a target
twice. Focused tests must fail. `always()` cleans only the run-owned stage/temp paths and reports a separate cleanup result; failed
builds do not upload a candidate payload.

Implement `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with mandatory parameters `-Repository`,
`-WorkflowFile`, `-HeadSha`, `-ExpectedWorkflowName`, `-ExpectedJobName`, `-ExpectedArtifactName`, `-ExpectedJUnitPath`,
`-ExpectedTestIds`, `-EvidenceProfile`, `-DownloadRoot` and `-TimeoutSeconds`. The command uses `gh` to select the first-attempt run
for the exact workflow file and 40-hex head SHA, polls only until the supplied deadline, and requires the named job and artifact to
succeed. It requires an API artifact digest of `sha256:` plus 64 lowercase hex digits, downloads by artifact ID, recomputes the ZIP
SHA-256, rejects a mismatch, and parses the named JUnit file. It exits non-zero for timeout, ambiguity, a retry, a missing or
unexpected test ID, any JUnit failure/error, malformed evidence, or any identity/digest mismatch. The `CandidateBuild` profile
requires exact artifact `release-candidate-build-evidence` containing `candidate-build.json`, `release-manifest.json` and the named
JUnit file, and binds repository, workflow name/file, run ID/attempt, head SHA, artifact name/API digest, canonical payload SHA-256,
manifest SHA-256 and exactly one build invocation; it also rejects secret-like
keys/values and deletes its run-owned download directory without logging environment values.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-00-T2 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- One job and one run attempt compile every release target once from the exact accepted SHA and current locks.
- The canonical ZIP contains every section 6 role and no source-tree/cache/personal/secret residue.
- Manifest/per-file/toolchain/proto/migration/dependency identities reconcile with upstream evidence and the staged bytes.
- Primary and cleanup failures are distinct; any failure prevents candidate upload and same-identity rebuild.
</acceptance_criteria>
</task>

<task id="R-00-T3" type="auto" tdd="true">
<name>GREEN/mutation：上传 immutable candidate 并封存 provenance、digest 与 build check</name>
<read_first>
- canonical payload and internal manifest produced by R-00-T2
- `tests/release/contracts/candidate-evidence.schema.json` (planned)
- `.github/workflows/release.yml` (planned output)
</read_first>
<files>
- `.github/workflows/release.yml` (planned; modify)
- `scripts/release/release.ps1` (planned; modify evidence verifier)
- `scripts/release/ReleaseGate.psm1` (planned; modify)
- `tests/release/build/release_build.tests.ps1` (planned; modify)
- `tests/manifests/release-reports.json` (planned; register actual emitted cases/reports)
- `tests/CI-GOVERNANCE.md` (existing; update planned/actual check contract only after real registration)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; register R-00 ownership without guessed totals)
</files>
<action>
Upload one Actions artifact named `release-candidate-<version>-<full-sha>` with `overwrite:false`, `retention-days:30`,
`if-no-files-found:error` and full-SHA-pinned `upload-artifact`. Its content is the single canonical payload plus detached pre-upload
manifest hash; no independent application ZIPs are release candidates. Capture action output artifact ID/digest/URL and write detached
`candidate-evidence.json` plus deployment status without modifying the payload. Re-download by exact artifact ID in a new step,
validate the Actions digest, payload SHA-256, manifest SHA-256 and every file, then remove the downloaded temp root. If repository
permissions support the already approved attestation path, attest the canonical payload with exact SHA/workflow identity and record
verification; otherwise record the controlled unavailable state without weakening hashes. Add `R01-BUILD-14..16` for artifact-name
collision/overwrite, external digest mismatch, payload mutation, missing provenance, false attestation claim and missing upload/report.
Each mutation is RED, then restored. Publish stable check `Windows release candidate build`; register only actual cases/reports in the
release manifest. Never rerun a failed build or upload a second artifact to replace it.

Extend `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with `-ExpectedCompanionArtifactName` and profile
`CandidateUpload`. This profile requires exactly one candidate artifact matching `release-candidate-<version>-<full-sha>` and one
`release-candidate-upload-evidence` artifact on the same first-attempt run, requires both API digests and downloaded ZIP digests to
be SHA-256 matches, and parses `candidate-evidence.json`, `release-manifest.json` and
`build/test-results/release_candidate_build.xml`. The evidence binds both artifact IDs/names/digests, run ID/attempt/head SHA,
30-day retention, payload SHA-256, manifest SHA-256, verified re-download and attestation state. The command exits non-zero on
timeout, ambiguity, overwrite/collision, missing provenance, digest/payload/manifest drift, a retry, any unexpected JUnit
ID/failure/error, or secret-like evidence, and removes only its run-owned download directory.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-00-T3 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- One immutable candidate identity links accepted SHA, build record, artifact ID/external digest, payload and manifest hashes.
- Re-download proves uploaded bytes match the staged canonical payload and all internal files.
- Attestation state is truthful; manifest/hash/provenance remain complete regardless of optional platform capability.
- The R-00 check is authoritative, secret-free and cannot be made green by overwrite, retry or second build.
</acceptance_criteria>
</task>

</tasks>

## 10. Plan R-01 — Artifact-only install/config/start/compat/stop smoke

Status: **Planned**
Wave: **20**
Depends on: **R-00 PASS and immutable candidate identity**
Coverage: **G-017, G-018; DG-11, DG-12, DG-13, DG-17, DG-21**

<objective>
在新的 GitHub-hosted Windows consumer job 中，只按 R-00 artifact ID 下载并验证 candidate，安装到 run-owned clean root，
从 artifact 内 config templates 生成 synthetic runtime config，启动/探测/兼容/停止 release binaries并证明端口与资源释放。
source checkout、source build output、cache 或 runner-global dependency 不能补齐 candidate 的任何被测文件。
</objective>

<tasks>

<task id="R-01-T1" type="tdd">
<name>RED/GREEN：实现 artifact-only resolver、safe extraction 与 no-source-supplement gate</name>
<read_first>
- R-00 `candidate-evidence.json`, payload and `release-manifest.json`
- actual Phase 3C `ArtifactResolver` and `ManifestVerifier` Interfaces/Summary
- `tests/plans/PHASE-3B-RELEASE-RESEARCH.md`
</read_first>
<files>
- `scripts/release/ReleaseGate.psm1` (planned; extend/reuse actual upstream verifier)
- `tests/release/smoke/artifact_resolution.tests.ps1` (planned; create, non-empty)
- `tests/release/smoke/README.md` (planned; create)
- `.github/workflows/release.yml` (planned; modify)
</files>
<action>
RED `R01-SMOKE-01..07`. The R-01 job must omit `actions/checkout`; its only candidate input is a full-SHA-pinned
`download-artifact` call with exact R-00 artifact ID. Verify action-reported digest, canonical payload name/size/hash and detached
candidate evidence before safe extraction; then validate internal manifest and every file. Safe extraction rejects absolute/UNC/drive,
`..`, duplicate/case-colliding path, reparse/symlink, undeclared file, zip bomb limits and output outside the run-owned root. Add a
structure gate that fails if workflow contains checkout, build/MSBuild/CMake/npm restore for the tested apps, copy from
`github.workspace`, source/vcpkg/Qt/global-node lookup, or any fallback that fills a missing candidate path. `npm` may only execute the
packaged Varify dependency tree; no install/ci runs in R-01. Mutation changes artifact ID/digest/file bytes, removes a runtime file,
adds traversal and enables workspace copy; each is nonzero. GREEN returns `VerifiedCandidate` only after both external and internal
identity pass and cleans extraction root on all failures.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-01-T1 -Configuration Release -ExpectRed</automated>
</verify>
<acceptance_criteria>
- R-01 can identify and extract exactly one R-00 candidate without a source checkout.
- External artifact digest, payload hash, manifest hash and every file hash are required before execution.
- Missing or changed bytes fail; no workspace/cache/global toolchain path can repair the package.
- Unsafe archives never write outside the run-owned root and all partial extraction is cleaned.
</acceptance_criteria>
</task>

<task id="R-01-T2" type="auto" tdd="true">
<name>GREEN/mutation：clean install、config、protocol ready、stop 与 port-release smoke</name>
<read_first>
- `VerifiedCandidate` and packaged `release-tools` from R-01-T1
- actual Phase 3B ProcessHarness/RunContext Interfaces and formal EXE lifecycle evidence
- actual Phase 3C DependencyCoordinator/service/config/migration outputs and Summary
</read_first>
<files>
- `scripts/release/release.ps1` (planned; modify evidence verifier)
- `scripts/release/ReleaseGate.psm1` (planned; implement smoke composition)
- `tests/release/smoke/artifact_lifecycle.tests.ps1` (planned; create, non-empty)
- `tests/release/smoke/artifact-smoke.schema.json` (planned; create)
- `tests/release/smoke/README.md` (planned; modify)
- `.github/workflows/release.yml` (planned; modify)
</files>
<action>
RED `R01-SMOKE-08..15`. Copy only the verified extracted payload into a second clean run-owned install root and prove every installed
file maps to the manifest; no source/build path is in PATH or config. Render config only from payload templates using dynamic loopback
application ports and synthetic run-id identities. Start Varify, Status, Chat, Gate and Qt client/package probe in the documented
dependency order with the production entrypoints. `start` means each formal process loads packaged runtime dependencies/config and
reaches its production protocol-level ready contract; PID/sleep/log text alone is not ready. Where formal Windows ready requires
Redis/MySQL/Mailpit, consume only the approved `release-smoke-windows` environment's run-scoped disposable endpoint Interface already
preflighted against Phase 3C contracts. If such endpoints cannot be provisioned without personal/shared credentials, stop and block
release; do not downgrade to fake/in-memory adapters or call dependency-unavailable exit a successful start. Exercise packaged
migration inspect/plan in read-only or run-owned schema mode, one bounded current-N identity/message smoke through public protocols,
then graceful stop in client→Chat→Gate→Status→Varify order, 10-second per-process stop with identity-matched escalation, port rebind and
empty process/thread/socket/temp/schema/key/mail ledger. Mutation removes a DLL/plugin/node module/config key, injects fixed port,
marks PID ready, points to source, swallows startup failure, kills a stale PID or omits cleanup; focused tests fail. Job timeout 120
minutes; app ready 30 seconds; request 10 seconds; cleanup failure remains blocking and separate from primary failure.

Extend `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with profile `ArtifactLifecycle`. It must select the exact
first-attempt `Release`/`.github/workflows/release.yml` run and job `Windows artifact-only smoke and compatibility`, verify artifact
`release-artifact-lifecycle-evidence` by API and downloaded ZIP SHA-256, and parse
`build/test-results/release_artifact_smoke.xml`, `artifact-smoke.json` and `teardown.json`. Require exact candidate artifact
ID/name/API digest, payload and manifest SHA-256, source SHA and run ID/attempt; require the exact supplied JUnit IDs with zero
failures/errors; and require primary status `PASS`, cleanup status `PASS`, zero tracked processes/threads/sockets/temp roots/schemas/
keys/mail and successful dynamic-port rebind. Timeout, retry, missing/extra identity, source/build-path use, digest drift, false ready,
non-empty teardown, secret-like evidence or cleanup failure exits non-zero and removes only the run-owned download directory.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-01-T2 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- Install, config, start, public probe, stop and cleanup use only candidate application bytes plus approved disposable infrastructure.
- Every formal application proves packaged-runtime and protocol readiness; no source, fake mode, personal/shared endpoint or public SMTP is used.
- Primary failure and cleanup failure remain separately visible and either fails the check.
- No child, thread, socket, port, run-owned config/temp/schema/key/mail resource remains; pre-existing state is untouched.
</acceptance_criteria>
</task>

<task id="R-01-T3" type="auto" tdd="true">
<name>GREEN/mutation：运行适用 N/N-1 artifact compatibility 并收口 public smoke check</name>
<read_first>
- actual Phase 3C/3D compatibility resolvers, matrix schemas and accepted reports
- `tests/compatibility/baselines/n-minus-1.json` (actual pointer or bootstrap record)
- R-01 current candidate identity and lifecycle evidence
</read_first>
<files>
- `tests/release/smoke/artifact_compatibility.tests.ps1` (planned; create, non-empty)
- `tests/release/smoke/README.md` (planned; modify)
- `scripts/release/release.ps1` (planned; modify evidence verifier)
- `scripts/release/ReleaseGate.psm1` (planned; modify)
- `.github/workflows/release.yml` (planned; modify)
- `tests/manifests/release-reports.json` (planned; register actual R-01 cases/reports)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; update actual ownership/status only)
- `tests/REGRESSION.md` (existing; document actual runner/report after evidence)
</files>
<action>
Add `R01-SMOKE-16..20`. Resolve N-1 only through the actual immutable pointer and durable Release asset; verify its release-asset
digest and internal manifest before use. When a real N-1 exists, use the downloaded N and N-1 payloads to run the Windows artifact
smoke cells supported by the accepted Phase 3C/3D matrix: install/upgrade config and schema metadata, N-1 client→N service package
startup/handshake, N client→N-1 supported handshake, supported service/proto pairing and stop/cleanup. Never rebuild either version,
copy a current DLL into N-1 or use static fixtures as process PASS. Unsupported cells fail fast before write with stable diagnostics.
When no promoted N-1 exists, emit exact non-PASS `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`, keep G-017 open and do not label aggregate
compatibility PASS; current-N artifact smoke must still PASS. Mutations use cache/source rebuild, mismatch digest, supplement N-1,
write before unsupported fail-fast, relabel bootstrap, omit report or cleanup; all fail. Register actual reports/cases only and publish
stable check `Windows artifact-only smoke and compatibility` with `always()` uploads and `if-no-files-found:error`; no automatic retry,
quarantine, `continue-on-error` or source checkout.

Extend `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with profile `ArtifactCompatibility`. It must select the
exact first-attempt `Release` workflow run/job, verify artifact `release-artifact-smoke-evidence` by API and downloaded ZIP SHA-256,
and parse `build/test-results/release_artifact_smoke.xml`, `build/test-results/release_artifact_compatibility.xml`,
`artifact-compatibility.json`, `teardown.json` and `redaction.json`. Require exact candidate artifact/run/source identity, payload and
manifest SHA-256; the exact supplied compatibility JUnit IDs with zero failures/errors; current-N smoke `PASS`; either supported
compatibility `PASS` or exact non-PASS `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`; zero secret findings; and primary/cleanup `PASS` with zero
tracked resources. Timeout, retry, cache/source rebuild, supplemented N-1, digest/report/status drift, unexpected JUnit identity,
teardown residue or redaction finding exits non-zero and removes only the run-owned download directory.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-01-T3 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- Current candidate lifecycle smoke is PASS on downloaded bytes.
- Real N-1 cells use immutable downloaded assets or every N-1 cell retains exact bootstrap non-PASS; no fixture/source/cache impersonation occurs.
- Unsupported combinations fail before mutation, supported combinations are bounded, and both versions clean up completely.
- The stable public check is tied to the R-00 artifact ID/digests and actual reports; first failure cannot be retried or waived green.
</acceptance_criteria>
</task>

</tasks>

## 11. Plan R-02 — Same-digest versioned human UAT sign-off

Status: **Planned**
Wave: **21**
Depends on: **R-01 PASS for exact candidate identity**
Coverage: **G-018; DG-12, DG-17, DG-22**

<objective>
建立仓库版本化 UAT checklist、machine-readable signed evidence 与 protected human approval。用户只对 R-01 已验证的 exact
candidate artifact ID/Actions digest/payload SHA-256/manifest SHA-256 执行和签署；任何 candidate byte、checklist、signer、
environment 或 evidence 变化使签核失效，缺陷立即阻断并路由到 owning regression gate。
</objective>

<tasks>

<task id="R-02-T1" type="tdd">
<name>RED/GREEN：冻结 versioned checklist、evidence schema 与 byte-change invalidation</name>
<read_first>
- R-00 candidate identity/manifest/provenance evidence
- R-01 smoke/compatibility reports and teardown evidence
- `tests/CI-GOVERNANCE.md`
</read_first>
<files>
- `tests/release/uat/windows-x64-checklist.md` (planned; create, versioned in content/frontmatter)
- `tests/release/uat/uat-evidence.schema.json` (planned; create)
- `tests/release/uat/uat_evidence.tests.ps1` (planned; create, non-empty)
- `tests/release/uat/README.md` (planned; create)
- `scripts/release/ReleaseGate.psm1` (planned; add validator)
</files>
<action>
Define checklist identity/hash and fixed `UAT-WIN-*` items for: candidate identity verification; clean extraction/install; config
template review without secrets; Gate/Status/Chat/Varify/client start and visible version; verification/register/login/discovery/Chat
login; friend apply/accept; two-server private message; disconnect/relogin; history pagination/order/dedup; applicable N-1 upgrade or
explicit bootstrap acknowledgement; graceful stop/restart/port release; log/redaction review; external OS registration/config migration
deviations. Checklist records expected observation, evidence reference, PASS/FAIL/BLOCKED and comment; every item must be PASS for
sign-off. Evidence schema requires checklist version/hash, exact CandidateIdentity fields, R-00/R-01 check run IDs/report hashes,
UAT environment OS/topology identifiers, signer GitHub login, protected-environment approval/deployment ID, start/end UTC, per-item
results, deviations, defect links and evidence-file SHA-256. Add RED `R01-UAT-01..09` for candidate/artifact/payload/manifest mismatch,
modified payload file, stale checklist, missing/duplicate item, missing signer/approval, non-PASS item, unclosed defect, secret value and
timestamp/environment mismatch. Validator re-downloads the exact R-00 artifact ID and recomputes payload/manifest/file hashes; it does
not trust evidence claims or rebuild. Any byte change, even from the same source SHA, invalidates old UAT.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-02-T1 -Configuration Release -ExpectRed</automated>
</verify>
<acceptance_criteria>
- Checklist is repository-versioned, user-observable and covers install through stop plus the fixed business journey.
- Evidence binds one signer/approval and every checklist item to exact candidate and R-01 report identities.
- Rebuilt, repacked, patched or otherwise byte-different payload cannot reuse sign-off, even when source/version strings match.
- Failed/blocked item, deviation without acceptance, open defect or secret finding is a hard blocker.
</acceptance_criteria>
</task>

<task id="R-02-T2" type="auto" tdd="true">
<name>GREEN/mutation：建立 protected UAT request、evidence submission 与 validation check</name>
<read_first>
- `tests/release/uat/windows-x64-checklist.md` (planned output of R-02-T1)
- `tests/release/uat/uat-evidence.schema.json` (planned output)
- `.github/workflows/release.yml` (planned R-01 output)
</read_first>
<files>
- `.github/workflows/release.yml` (planned; modify)
- `scripts/release/release.ps1` (planned; modify)
- `scripts/release/ReleaseGate.psm1` (planned; modify)
- `tests/release/uat/uat_evidence.tests.ps1` (planned; modify)
- `tests/manifests/release-reports.json` (planned; register actual R-02 cases/report)
- `tests/CI-GOVERNANCE.md` (existing; update UAT evidence gate after real registration)
</files>
<action>
After exact R-01 success, create a UAT request record containing candidate identity, checklist hash, report hashes, retention expiry and
the approved evidence-submission path. The `release-uat` GitHub environment pauses for its required user/release-owner approval and has
no production/application secrets. The human runs only the downloaded candidate's packaged verifier/checklist helper; evidence is
submitted as a dedicated metadata-only PR/file under the versioned UAT evidence path or the repository's approved equivalent, signed
by the submitter's GitHub identity. The validator workflow may checkout only checklist/evidence metadata; it re-downloads candidate by
artifact ID and must never build, repackage, patch or use checked-out files as release content. Add `R01-UAT-10..14` for wrong approver,
approval before R-01, expired/missing artifact, evidence commit changing candidate bytes, second conflicting evidence, report drift and
missing required environment protection. First valid evidence for the identity is immutable; correction after failure requires a new
evidence revision and explicit re-approval, while candidate bytes/checklist changes require a full new UAT. Publish stable check
`Release UAT evidence`, upload validator JUnit with 30-day retention, and keep failure blocking without retry-to-green.

Implement `scripts/release/release.ps1 -Task TestContracts -Filter UatEvidenceValidation` in this task with mandatory parameters
`-WorkflowFile`, `-ExpectedWorkflowName`, `-ExpectedJobName`, `-ExpectedArtifactName`, `-ExpectedJUnitPath`, `-ExpectedTestIds`,
`-FixtureRunId`, `-FixtureHeadSha`, `-FixtureArtifactDigest`, `-JUnitPath`, `-WorkRoot` and `-TimeoutSeconds`. The repository validator
must parse the workflow and prove that the exact first-attempt run/head SHA and protected `release-uat` environment feed stable check
`Release UAT evidence`, exact artifact `release-uat-validation-evidence`, SHA-256 candidate/evidence/report identities and the named
JUnit upload with 30-day retention. It runs separate fixtures for valid evidence plus wrong order/approver, expired/missing artifact,
candidate-byte supplement, conflicting evidence, report drift and absent environment protection. Every invalid fixture must make the
inner validator return non-zero, while the outer contract suite records exact IDs `R01-UAT-10..14` with zero failures/errors. The
command has a hard deadline, performs no build command, logs no environment/secret values and removes only its run-owned work root.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-02-T2 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- UAT cannot begin before exact R-01 PASS and cannot complete without protected human approval.
- Metadata checkout cannot contribute application bytes; candidate is always re-downloaded and rehashed.
- One unambiguous evidence revision, signer and checklist identity are retained; conflicting/stale evidence blocks.
- The public UAT check is stable, report-backed, secret-free and tied to the exact candidate identity.
</acceptance_criteria>
</task>

<task id="R-02-T3" type="checkpoint:human-verify" gate="blocking">
<name>Human checkpoint：用户在 exact candidate digest 上执行并签署 UAT</name>
<read_first>
- R-00 `candidate-evidence.json` and candidate download URL/retention expiry
- R-01 artifact-smoke/compatibility reports and exact check run
- `tests/release/uat/windows-x64-checklist.md` (planned)
</read_first>
<files>
- `tests/release/uat/evidence/<release-version>.json` (planned metadata-only signed evidence; exact final path resolved by R-02-T2)
- UAT screenshots/log excerpts referenced by evidence and stored in the approved non-secret evidence location
</files>
<action>
The user/release owner downloads the candidate by the recorded artifact ID, runs its packaged verifier and confirms the displayed
Actions digest, payload SHA-256 and manifest SHA-256 match the UAT request. On the approved UAT environment, execute every
`UAT-WIN-*` item without replacing files from source/build/cache: install/config/start, visible version, verification/register/login/
discovery/Chat login, friendship, two-server message, reconnect/history/order/dedup, applicable upgrade/bootstrap acknowledgement,
stop/restart/port release and log/redaction review. Record PASS/FAIL/BLOCKED plus evidence reference and deviations for every item;
do not record password/token/code/raw email/credential/message secret. Any non-PASS item, different byte, missing dependency, cleanup
failure or defect immediately stops sign-off. File automatable defects with owning 3B/3C/3D Test ID/behavior, add regression first,
produce a new accepted SHA/candidate and restart R-00; never patch the candidate. Submit the evidence through the protected identity
path and approve `release-uat`; automation then re-downloads and validates it.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task ValidateUat -CandidateEvidence build/evidence/release/candidate.json -SmokeEvidence build/evidence/release/smoke.json -UatEvidence build/evidence/release/uat-submission.json -JUnitPath build/test-results/release_uat_validation.xml</automated>
</verify>
<acceptance_criteria>
- The user has personally completed every versioned checklist item against the exact R-00/R-01 candidate identity.
- Evidence has one verified signer, protected approval, timestamps, environment identity, report hashes and no secret values.
- Any byte/checklist/evidence drift or UAT defect invalidates approval and prevents R-03.
- `Release UAT evidence` is authoritative success; verbal approval, comment-only approval or evidence against another artifact is insufficient.
</acceptance_criteria>
</task>

</tasks>

## 12. Plan R-03 — Same-bytes promotion, immutable Release, and N-1 pointer

Status: **Planned**
Wave: **22**
Depends on: **R-02 exact-digest UAT PASS**
Coverage: **G-017, G-018; DG-13, DG-17, DG-21, DG-22**

<objective>
在 protected promotion approval 后，按 R-00 artifact ID 原样下载 canonical payload，复核 R-01/R-02 evidence，创建新 tag
和 draft Release，上传原 payload bytes并再次下载复核，然后发布 immutable Release。只回滚本 run 尚未公开的 promotion
metadata；公开后不删除/覆盖 tag/asset。建立 durable evidence，并创建或推进 N-1 artifact/schema pointer 供下一周期使用。
</objective>

<tasks>

<task id="R-03-T1" type="tdd">
<name>RED：冻结 promotion Interface、immutable tag/asset 与 metadata-only rollback contract</name>
<read_first>
- R-00 candidate identity/build/provenance evidence
- R-01 smoke/compatibility evidence
- R-02 signed UAT evidence and `Release UAT evidence` check
</read_first>
<files>
- `scripts/release/ReleasePromotion.psm1` (planned; create)
- `tests/release/promotion/promotion-evidence.schema.json` (planned; create)
- `tests/release/promotion/n-minus-1-pointer.schema.json` (planned; create)
- `tests/release/promotion/release_promotion.tests.ps1` (planned; create, non-empty)
- `tests/release/promotion/README.md` (planned; create)
</files>
<action>
Define `PromoteExact` and the real `GitHubReleasePromotionAdapter` plus deterministic local Adapter. Required input is one complete
CandidateIdentity, R-01 report hashes, validated UAT evidence/hash, new semantic release tag, accepted source SHA and current baseline
pointer state. RED `R01-PROMOTE-01..08` for tag/source mismatch, existing/mutable tag, existing release/asset, artifact/payload/manifest
digest mismatch, rebuilt/repacked payload, missing/non-success smoke or UAT check, stale checklist/evidence, overwrite request,
insufficient permissions/environment approval, and pointer referencing cache/source/mutable asset. Promotion state machine is
`Validated -> DraftCreated -> ExactAssetUploaded -> AssetRedownloadVerified -> Published -> PointerRecorded`. Before `Published`,
rollback may delete only draft Release/tag/asset created by the same run after identity verification and must record each action; it
never deletes an existing/public release. After `Published`, rollback is forbidden: pointer/evidence failure leaves an immutable
published release plus blocking repair record, not tag/asset deletion or reuse. No task runs a compiler, package restore, archive
creation or compression command.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-03-T1 -Configuration Release -ExpectRed</automated>
</verify>
<acceptance_criteria>
- Promotion Interface accepts only the exact validated candidate and exposes one immutable result/evidence contract.
- Existing or public tag/Release/asset is never overwritten, deleted or reused.
- Pre-publication rollback is identity-scoped metadata cleanup; post-publication failures are repaired forward.
- Compiler/build/package/compress operations are structurally forbidden from R-03.
</acceptance_criteria>
</task>

<task id="R-03-T2" type="auto" tdd="true">
<name>GREEN/mutation：原样发布 tag/Release asset 并复核 durable evidence chain</name>
<read_first>
- `scripts/release/ReleasePromotion.psm1` (planned output of R-03-T1)
- exact R-00/R-01/R-02 identities and reports
- `.github/workflows/release.yml` (planned)
</read_first>
<files>
- `scripts/release/ReleasePromotion.psm1` (planned; implement/modify)
- `scripts/release/release.ps1` (planned; add promotion command)
- `.github/workflows/release.yml` (planned; modify)
- `tests/release/promotion/release_promotion.tests.ps1` (planned; modify)
- `tests/manifests/release-reports.json` (planned; register actual promotion cases/report)
</files>
<action>
After a separate `release-promotion` required-reviewer approval, query all three release-admission checks on the exact candidate identity
and refuse missing/skipped/retried evidence. Download R-00 by exact artifact ID with the pinned action, verify Actions digest, payload
SHA-256, manifest SHA-256 and every internal file using the packaged/upstream verifier. Do not checkout application source or run any
build/restore/repack command. Create a new annotated tag targeting the accepted application source SHA and a draft GitHub Release;
upload the original canonical payload file without rename-by-recompression or overwrite, plus detached build/smoke/UAT evidence files
as separate assets. Re-download the candidate payload asset from the draft Release and compare byte size/SHA-256 with R-00, then verify
the internal manifest and evidence hashes. Only after exact match publish the Release and set immutable/non-overwrite policy. Add
`R01-PROMOTE-09..13`; mutations substitute same-version rebuilt bytes, change a single byte/name/source SHA, omit evidence, request
overwrite, accept a different approver, skip redownload or swallow cleanup. Each fails. `always()` preserves primary and promotion
cleanup evidence; no retry can replace first result.

Extend `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with profile `Promotion`. It must select the exact
first-attempt `Release`/`.github/workflows/release.yml` run and `Release promotion integrity` job, verify artifact
`release-promotion-evidence` by API and downloaded ZIP SHA-256, and parse `build/test-results/release_promotion.xml`,
`promotion-evidence.json`, the candidate evidence and cleanup evidence. Require exact repository/run/attempt/head SHA, protected
approval identity, candidate artifact ID/API digest, payload and manifest SHA-256, annotated tag target, Release/asset ID/name/URL,
published state and primary/cleanup `PASS`. The validator downloads the named durable Release asset through `gh` by the tag from the
evidence, recomputes byte size/SHA-256 and internal manifest hash, and compares them with the candidate. It exits non-zero for
timeout, retry, missing/ambiguous identity, overwrite, source/tag/asset/evidence/digest drift, unexpected JUnit ID/failure/error,
secret-like evidence or cleanup failure, and removes only its run-owned download directory.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-03-T2 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- Published payload is byte-for-byte the R-00 canonical payload; no rebuild/repack/patch/overwrite occurred.
- Tag targets the accepted source SHA and Release evidence closes the full SHA→artifact→smoke→UAT→promotion chain.
- Draft verification precedes publication; pre-publication failure cleans only current-run metadata and keeps candidate available.
- Published release/tag/assets are immutable and are never deleted to hide a later pointer/evidence failure.
</acceptance_criteria>
</task>

<task id="R-03-T3" type="auto" tdd="true">
<name>GREEN/mutation：建立或推进 immutable N-1 pointer 并完成 release closeout</name>
<read_first>
- published Release/tag/assets and promotion evidence from R-03-T2
- actual Phase 3C `tests/compatibility/baselines/n-minus-1.json` schema/bootstrap state
- actual Phase 3C/3D compatibility reports
</read_first>
<files>
- `tests/compatibility/baselines/n-minus-1.json` (planned upstream; create first pointer or advance existing pointer through metadata-only PR)
- `scripts/release/release.ps1` (planned; modify evidence verifier)
- `tests/release/promotion/release_promotion.tests.ps1` (planned; modify)
- `tests/release/promotion/README.md` (planned; modify)
- `tests/manifests/release-reports.json` (planned; freeze actual release counts/results)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; update evidence-backed G-017/G-018 status only)
- `tests/REGRESSION.md` (existing; record release commands/evidence identities)
- `tests/CI-GOVERNANCE.md` (existing; record actual release check/ruleset contract)
- `tests/plans/PHASE-RELEASE-SUMMARY.md` (planned; create only from accepted execution evidence)
</files>
<action>
Add `R01-PROMOTE-14..16`. Generate a metadata-only baseline pointer containing version, immutable tag, Release/asset IDs/URLs,
asset byte size/SHA-256, internal manifest hash, source SHA, schema version, migration manifest/checksums, protocol hashes, UAT/promotion
evidence hashes and published UTC. For the first successful immutable release, replace the exact bootstrap record with this complete
pointer and record that it becomes the N-1 input for the next development/release cycle; do not retroactively claim a first-cycle N-1
process PASS. For later releases, advance the pointer only to the just-published immutable release after verifying the existing pointer
still resolves and preserving its history in git/Release evidence. Submit the pointer through a dedicated metadata-only PR created by
the workflow; required structure/hash checks must pass, and no application source or artifact byte may enter the PR. If PR creation/
merge is unavailable, published Release remains immutable and `Release promotion integrity` stays failed with a forward-repair record;
do not delete or republish. Mutation points to Actions cache/expired candidate/wrong release, changes digest/schema/proto, loses previous
history, closes G-017 under bootstrap or marks pointer repair success without merge; each is nonzero. Re-resolve pointer via actual
Compatibility resolver, register actual reports/counts, update G-018 only after all release criteria; update G-017 only when its actual
N/N-1 required cells have run, never merely because a pointer now exists. Publish stable `Release promotion integrity` and durable
Summary after pointer verification.

Extend `scripts/release/release.ps1 -Task VerifyCiEvidence` in this task with profile `BaselinePointer`. It must select the exact
first-attempt `Release` workflow run and `Release promotion integrity` job, verify artifact `release-baseline-pointer-evidence` by
API and downloaded ZIP SHA-256, and parse `build/test-results/release_baseline_pointer.xml`,
`baseline-pointer-evidence.json` and the merged `tests/compatibility/baselines/n-minus-1.json`. Require exact repository/run/attempt/
head SHA, pointer merge commit, release tag/ID, asset ID/name/URL, byte size/SHA-256, internal manifest hash, source SHA, schema and
migration/protocol hashes, UAT/promotion evidence hashes and published UTC. Resolve the Release asset with `gh`, download it by exact
tag/name and recompute all bound hashes. Require exact supplied JUnit IDs with zero failures/errors and prove either first-release
bootstrap conversion leaves G-017 open or a later release preserves prior pointer history before advancing. Timeout, retry,
unmerged PR, cache/expired/wrong asset, digest/schema/protocol/history/status drift, unexpected JUnit result, secret-like evidence or
cleanup failure exits non-zero and removes only its run-owned download directory.
</action>
<verify>
<automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/release/release.ps1 -Task VerifyPlanTask -PlanTask R-03-T3 -Configuration Release</automated>
</verify>
<acceptance_criteria>
- A durable, verified pointer names the exact immutable Release asset and schema/protocol identity for the next cycle.
- First promotion creates baseline availability without fabricating retrospective N-1 PASS; G-017 follows actual matrix evidence.
- Published release remains immutable if pointer update fails; forward repair is required and release closeout stays blocked.
- Actual Test IDs/reports/counts, matrix, governance, Regression and Summary reconcile with no secret or owned residue.
</acceptance_criteria>
</task>

</tasks>

## 13. RED → GREEN → mutation → owning runner closure

1. **RED:** 每个 code-producing task 先创建 non-empty focused contract test。RED 必须来自 identity、manifest、artifact-only、
   lifecycle、UAT 或 promotion contract 缺失，而不是 download 抖动、空占位、个人凭据或固定 sleep。
2. **GREEN:** 通过同一 CandidateArtifact/ManifestVerifier/Promotion Interface 实现；workflow、local Adapter 与 GitHub Adapter
   不复制 hash/archive/state machine。所有 release payload consumer 使用相同 packaged verifier。
3. **Mutation:** second build、missing file、one-byte change、path traversal、secret、false SBOM、source supplement、false ready、
   cleanup omission、bootstrap-as-PASS、stale checklist、wrong signer、repack、overwrite、wrong tag/pointer 均必须使 focused
   selector nonzero；恢复后才运行 owning public stage。
4. **Owning runner:** R-00/R-01/R-02/R-03 各只运行自己的 focused/report gate；不得重跑上游完整 3B/3C/3D lane来替代
   已接受 evidence，也不得在 R-03 重跑 build/smoke/UAT。
5. **Evidence reconciliation:** planned ID、actual case、source/runner、JUnit、README、manifest、check run 与 artifact/release
   identity 一一对应；range 大小从不作为 count。
6. **First failure:** Required failure 保持阻断；诊断 rerun 不得产生可晋升的第二 candidate/result。修复必须回到 owner，
   形成新 accepted SHA 和新 build-once chain。

## 14. Timeout、cleanup、failure stop 与 rollback boundary

### 14.1 Bounded operations

| Operation | Hard bound |
| --- | ---: |
| R-00 hosted Windows build job | 300 minutes |
| artifact upload/download/hash verification | 30 minutes per transfer/verification stage |
| R-01 hosted Windows consumer job | 120 minutes |
| application protocol ready | 30 seconds each |
| public smoke request / compatibility operation | 10 seconds each unless upstream contract is stricter |
| graceful process stop | 10 seconds each, then identity-matched escalation |
| UAT candidate availability | within 30-day artifact retention; sign-off after expiry is invalid |
| promotion API operation / asset re-download | 30 minutes each |

网络/Actions API 的有限诊断 retry 可以发生在同一 operation 内，但不得重新 build、重新 upload candidate、覆盖 first test
failure或改变 candidate identity。每个 job 有 workflow hard timeout；timeout 与 runner unavailable 均为 failure。

### 14.2 Cleanup ownership

- R-00：build/stage/temp/install cache only when created by current run; candidate deployment/tombstone and non-sensitive failure
  evidence remain. No pre-existing vcpkg/Qt/user path deletion。
- R-01：stop client→Chat→Gate→Status→Varify; release application ports; delete run-owned config/install/temp/schema/key/mail only;
  preserve bounded sanitized primary evidence before cleanup。
- R-02：does not mutate candidate; failed evidence revision remains auditable and cannot approve promotion。
- R-03 pre-publication：only current-run draft Release/tag/assets may be removed after identity check; candidate remains for diagnosis。
- R-03 post-publication：tag/Release/assets are immutable and never rolled back. Pointer/evidence defects require forward repair and keep
  final check failed。
- Cleanup failure never masks primary failure; reports contain separate `primary_result` and `cleanup_result`, either non-PASS blocks。

### 14.3 Global stop conditions

Stop immediately and retain non-sensitive evidence if any occurs:

1. Phase 3A/3B/3C/3D accepted evidence or one of six master checks is missing/not-success/different SHA.
2. Same candidate version/SHA already has a build record, `run_attempt != 1`, or implementation proposes rebuilding/reuploading after failure.
3. Candidate omits a section 6 role, contains undeclared/unsafe path, secret/real data, tool/lock/proto/migration drift, false SBOM claim,
   missing hash/provenance or shorter-than-required retention.
4. R-01 needs source checkout/build/cache/global dependency to complete install, or cannot reach formal production ready without fake,
   personal/shared dependency or public SMTP.
5. Any runner/dependency unavailable, timeout, report missing/count guessed, first failure retried/quarantined/waived, or cleanup/residue nonempty.
6. N-1 comes from source/cache/mutable artifact, actual digest differs, unsupported combination writes before fail-fast, or bootstrap is PASS.
7. UAT targets different bytes/checklist, lacks protected signer/approval/item evidence, contains a secret, or has any failed/blocked/open defect.
8. Promotion performs build/restore/compress/patch, targets another SHA, overwrites existing tag/Release/asset, skips asset redownload or uses
   broader/personal credential.
9. N-1 pointer does not resolve the immutable asset/schema/proto identity, loses history, or G-017/G-018 status is changed without actual evidence.
10. Same blocking condition repeats three execution turns without new evidence: stop and hand off; do not weaken contract or loop.
11. 本机 vcpkg 固定路径被访问写入、出现隐式 manifest install，或任一 package/root/triplet/baseline/tool identity 变更
    未取得 DG-25 的本次明确批准；不得用本机/全局/替代依赖树补齐 candidate，也不得删除重建。

## 15. Threat model

### 15.1 Trust boundaries

| Boundary | Untrusted input/state | Required control/evidence |
| --- | --- | --- |
| master checks/source ref → R-00 | SHA, check conclusion, workflow/report identity | exact SHA/check API verification, accepted report hashes, build-once deployment record |
| toolchain/workspace → staging | compiler outputs, dependency tree, config/assets, temp paths | pinned identities, explicit allowlist, app-local inventory, no secret/source/temp residue |
| staging → Actions artifact | archive paths/bytes, upload name/digest/retention | safe path schema, canonical payload once, overwrite false, external+internal hashes, re-download verification |
| Actions artifact → R-01 install | artifact ID, archive extraction, missing/replaced files | exact ID/digest, safe extraction, per-file manifest, no checkout/supplement |
| config/environment → processes | endpoint, port, credential reference, dependency readiness | synthetic/run-id values, protected references, range validation, protocol ready, redaction |
| N-1 Release asset → compatibility | tag/asset/hash/schema/support declaration | durable resolver, external/internal digest, no source/cache rebuild, fail-fast-before-write |
| candidate/UAT helper → human signer | displayed digest, checklist, environment, evidence attachments | packaged verifier, checklist hash, protected approval, all-item schema, secret-free evidence |
| UAT evidence → promotion | signer, item result, artifact/report hashes, defects | independent re-download/rehash, validator check, immutable evidence revision |
| workflow token → tag/Release/pointer | permission scope, existing metadata, API response | least privilege, protected environment, new immutable tag/asset, no overwrite, identity-scoped draft cleanup |
| Release asset → future N-1 resolver | durable bytes, pointer metadata, schema/proto identity | asset re-download hash, pointer schema/history, actual compatibility status |

### 15.2 STRIDE register

| Threat ID | Category | Target | Disposition | Required mitigation / evidence |
| --- | --- | --- | --- | --- |
| T-REL-01 | Spoofing | accepted SHA/checks/artifact ID | mitigate | exact API identity, same SHA across six checks, artifact ID+Actions digest+payload/manifest hashes |
| T-REL-02 | Tampering | staged/payload/release bytes | mitigate | explicit allowlist, per-file SHA-256, canonical ZIP once, re-download at R-00/R-01/R-02/R-03, no overwrite/repack |
| T-REL-03 | Tampering | archive extraction/install path | mitigate | normalized relative paths, traversal/collision/reparse/size limits, run-owned root, manifest-only install |
| T-REL-04 | Repudiation | build/smoke/UAT/promotion | mitigate | durable candidate record, stable Test IDs/JUnit, report hashes, signer/approval/timestamps, Release evidence assets |
| T-REL-05 | Information disclosure | manifest/config/log/JUnit/UAT/Release | mitigate | synthetic-only, allowlist serialization, exact-value redaction, no env dump/secret values/absolute paths |
| T-REL-06 | Denial of service | builds, downloads, process ready/stop, UAT retention, API | mitigate | job/operation deadlines, 30-day retention, bounded logs, identity stop/escalation, first failure blocks |
| T-REL-07 | Elevation of privilege | workflow token/environments | mitigate | stage-specific least privilege, required reviewers, tag/ruleset restrictions, no PAT, no untrusted-code secrets |
| T-REL-08 | Tampering / Data loss | unsupported N/N-1 and pointer | mitigate | immutable resolver, fail-fast before writes, bootstrap non-PASS, pointer asset/schema/proto hash verification |
| T-REL-09 | Repudiation / Tampering | build-once invariant | mitigate | candidate tombstone before compile, run-attempt check, duplicate/version registry, failed record cannot be erased into rebuild authority |
| T-REL-10 | Tampering | SBOM/provenance claim | mitigate | pinned generator when present; otherwise explicit controlled inventory fallback; false/empty status fails |
| T-REL-SC | Supply chain | actions/npm/vcpkg/Qt/tools | mitigate | full action SHAs, current locks/toolchain identities, no new install; any lock/package/tool change stops for legitimacy review |

本 threat model 不声称完整 supply-chain/ASVS certification；它保护本阶段 source SHA→candidate→smoke→UAT→Release
identity 与权限链，并明确保留现有 auth/TLS/product-security 限定。

## 16. DG / Gap coverage and release completion criteria

### 16.1 DG / G coverage

| ID | Release-gate ownership | Plan/section | Status |
| --- | --- | --- | --- |
| DG-09 | Release 独立拥有 immutable artifact smoke、UAT、promotion，不重做 3B/3C/3D | sections 1/4; R-00..03 | COVERED |
| DG-10 | 不创建 application Docker image | non-goals; R-00 payload/R-01 env | COVERED |
| DG-11 | hosted runner、不可用/timeout/report/cleanup failure 均阻断；无 self-hosted Required | sections 3/14; R-00/R-01 | COVERED |
| DG-12 | synthetic/run-id isolation、redaction、identity cleanup | sections 3/5/14/15; R-00..02 | COVERED |
| DG-13 | 首次 immutable promotion 创建完整 N-1，之后只维护 N/N-1 | R-01/R-03 | COVERED |
| DG-14 | real transport evidence is inherited prerequisite; Release smoke uses formal production processes | prerequisites; R-01 | COVERED without duplicating 3B |
| DG-15 | migration/schema evidence is packaged and pointer-bound; migration implementation remains 3C | R-00 manifest; R-01 compat; R-03 pointer | COVERED without duplicating 3C |
| DG-16 | two-server current-N E2E is an accepted prerequisite and UAT item | prerequisites; R-02 | COVERED without duplicating 3D |
| DG-17 | unified Windows x64 set, same set for smoke/UAT/promotion, no rebuild | R-00..03 | COVERED |
| DG-18 | Windows toolchain produces release; Linux evidence is inherited, not release bytes | R-00/non-goals | COVERED |
| DG-19 | no fake/test-only EXE; formal processes and packaged verifier only | R-01/stop conditions | COVERED |
| DG-20 | deterministic lifecycle/resource failures and cleanup are inherited/retained | R-01/sections 13-15 | COVERED |
| DG-21 | actual immutable N/N-1 or bootstrap; unsupported fail-fast | R-01/R-03 | COVERED |
| DG-22 | versioned checklist, exact digest sign-off, defect blocking/regression routing | R-02/R-03 | COVERED |
| DG-23 | hosted authoritative evidence; no personal/local runner | prerequisites/R-00/R-01 | COVERED |
| DG-24 | idempotency evidence is inherited and UAT/compat validates observed same-ID behavior | prerequisites/R-01/R-02 | COVERED without reimplementing 3C/3D |
| DG-25 | 本机 vcpkg 持久目录不可变；release build 只使用 hosted run-owned 临时 root；dependency/root identity 变化先审批 | prerequisites/non-goals/R-00/stop conditions | COVERED |
| G-017 | actual immutable N/N-1 compatibility or exact bootstrap; pointer enables next cycle but does not fabricate closure | R-01/R-03 | COVERED, evidence-dependent |
| G-018 | build once, artifact-only smoke, versioned UAT, same-bytes promotion | R-00..03 | COVERED, planned |

### 16.2 Completion criteria

Release gate is complete only when all applicable conditions are true:

- Phase 3A/3B/3C/3D completion evidence is accepted and one candidate SHA has all six exact master checks authoritative success.
- One build-once/tombstone record and one R-00 run attempt produced one unified canonical Windows x64 payload with complete manifest,
  per-file/proto/migration/toolchain/dependency identities, truthful SBOM/fallback status and no secret/unsafe path.
- One immutable Actions artifact has exact ID/name/digest, 30-day retention and verified payload/manifest hashes; re-download is exact.
- R-01 performed clean artifact-only resolution/install/config/start/public probe/stop/port/resource cleanup on hosted Windows; source
  checkout/build/cache did not supplement payload. Actual N-1 cells ran or exact bootstrap remained non-PASS.
- User completed every versioned UAT item on that exact candidate identity; protected signer/approval and machine validator are PASS;
  no byte/checklist drift, secret, deviation without acceptance or open defect exists.
- R-03 published a new tag at the accepted source SHA and exact original payload bytes; draft asset re-download matched before publish;
  durable build/smoke/UAT/promotion evidence is attached and no overwrite/rebuild/repack occurred.
- N-1 pointer resolves the published immutable asset and schema/proto identity. First release makes it available only for the next cycle;
  later releases advance it with history. Pointer failure leaves publication immutable and final check failed until forward repair.
- Every focused mutation was observed nonzero before restoration; actual Test IDs/cases/reports/counts reconcile; no Required failure was
  retried/quarantined/waived; teardown/redaction scans are clean.
- `G-018` changes only from this complete evidence. `G-017` closes only when required actual N/N-1 cells ran; bootstrap/pointer creation
  alone does not close it.

## 17. Artifacts this phase produces

All items remain planned until execution evidence exists:

- `.github/workflows/release.yml` with stable R-00..R-03 check names, full-SHA actions, environments, permissions, concurrency,
  timeouts, required uploads and cleanup.
- `scripts/release/release.ps1`, `ReleaseGate.psm1`, `ReleasePromotion.psm1`, payload layout and packaged release tools.
- Release contract/build/smoke/UAT/promotion schemas, non-empty tests and README ownership under `tests/release/`.
- `tests/manifests/release-reports.json` populated from actual registration/emitted JUnit, never planned ranges.
- One canonical `Chat-<version>-windows-x64.zip`, internal manifest/per-file hashes, dependency inventory/SBOM status, detached candidate
  evidence and optional truthful attestation evidence.
- Artifact-only smoke/compatibility reports, signed versioned UAT evidence, promotion evidence and Phase Release Summary.
- One immutable GitHub tag/Release asset/evidence bundle and verified `tests/compatibility/baselines/n-minus-1.json` pointer/history.

## 18. Multi-source coverage audit

| Source | ID / item | Owning plan / section | Status |
| --- | --- | --- | --- |
| GOAL | build one unified Windows x64 candidate from accepted SHA | R-00 | COVERED |
| GOAL | artifact-only install/config/start/compat/stop smoke | R-01 | COVERED |
| GOAL | versioned human UAT on same digest; byte change invalidates | R-02 | COVERED |
| GOAL | same-bytes tag/Release promotion and N-1 pointer | R-03 | COVERED |
| REQ | G-017 current/previous compatibility | R-01/R-03 | COVERED with actual-or-bootstrap rule; not pre-completed |
| REQ | G-018 artifact/UAT | R-00..03 | COVERED, planned |
| RESEARCH | build-once artifact ID/digest + internal manifest/per-file hashes | R-00; sections 5/6 | COVERED |
| RESEARCH | toolchain/lock/provenance and optional attestation | R-00; sections 3/5/15 | COVERED |
| RESEARCH | dependency cache is not release identity | R-01/R-03 stop conditions | COVERED |
| RESEARCH | artifact-only consumer and safe extraction | R-01 | COVERED |
| RESEARCH | 7-day current retention is insufficient | section 3.4; R-00 | COVERED with 30-day hard preflight |
| RESEARCH | private-plan attestation capability is conditional | section 5.3; R-00 | COVERED without weakening required hashes |
| RESEARCH | no immutable N-1 before first promotion | R-01/R-03 | COVERED without fabricated PASS |
| RESEARCH | UAT evidence chain and defect routing | R-02 | COVERED |
| RESEARCH | secret/PII, path/resource and artifact replacement threats | sections 5/14/15; R-00..03 | COVERED |
| CONTEXT | DG-09..DG-24 release-relevant decisions | section 16.1 | COVERED; upstream-owned implementation remains a prerequisite/non-goal |

Audit result: every in-scope GOAL, G-017/G-018 requirement, release-relevant research constraint and DG decision has an executable
owner or an explicit inherited prerequisite. No Deferred Idea is introduced, no testcase/report total is fabricated, and no source item
is silently omitted.
