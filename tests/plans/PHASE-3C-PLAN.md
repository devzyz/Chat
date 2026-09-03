---
document: tests/plans/PHASE-3C-PLAN.md
phase: 3C
title: Linux same-source build, disposable dependencies, persistence, and compatibility
status: Planned
plan_ids: [3C-00, 3C-01, 3C-02, 3C-03, 3C-04, 3C-05, 3C-06, 3C-07, 3C-08, 3C-09]
plan_count: 10
wave_range: [5, 12]
depends_on:
  - "Phase 3B complete with accepted completion evidence"
requirements: [G-012, G-013, G-014, G-015, G-016, G-017]
locked_decisions: [DG-09, DG-10, DG-11, DG-12, DG-13, DG-15, DG-18, DG-19, DG-21, DG-23, DG-24, DG-25]
baseline_contract:
  reports: 12
  testcases: 180
  develop_required_checks:
    - Static configuration checks
    - Server Release build
    - Qt client Release
    - VarifyServer dependency and package check
execution_policy:
  vertical_cycle: "RED -> GREEN -> mutation -> owning public runner"
  full_phase_lane_runs: 1
  final_testcase_total: "derive from actual registration; never predeclare"
autonomous: true
must_haves:
  truths:
    - "Pinned hosted Ubuntu configures, compiles, links, and enters startup paths for the same production sources"
    - "Disposable Redis, MySQL, and Mailpit prove real production Adapter behavior without personal services or secrets"
    - "Fresh schema and applicable N-1 migration are auditable through one SchemaMigration Interface"
    - "A sender/UUID retry returns one durable message ID through an explicit whole-batch transaction"
    - "Four native production processes reach protocol ready, share run-owned state, recover, and clean up"
    - "N/N-1 runs from immutable promoted artifacts or reports a non-PASS bootstrap blocker"
  artifacts:
    - "planned top-level CMake/presets and production target registry"
    - "planned SchemaMigration, MessageCommit, real Redis/MySQL/SMTP Adapters, and integration tests"
    - "planned hosted Ubuntu master workflow, report manifest, compatibility matrix, and teardown evidence"
  key_links:
    - "formal EXEs, IntegrationHost, and tests -> same production library targets"
    - "MessageCommit -> MySqlMessageCommitAdapter -> UNIQUE(send_id, client_msg_uuid)"
    - "RunContext -> PosixProcessAdapter/DependencyCoordinator -> four production processes and disposable services"
---

# Phase 3C 正式执行计划

状态：**Planned（尚未执行）**

本文件是 Phase 3C 的执行合同，不是完成证据。当前 12-report/180-testcase baseline、四个稳定 Windows
`develop` Required check 名称和 G-012..G-017 的当前状态均不因本计划改变。只有实际测试注册、报告和
远端门禁证据全部存在后，实施者才可按真实数字更新 manifest；本文件不会预填未来 testcase/report 总数，
也不会提前把任何 Gap 标为 complete。

## 1. Hard prerequisite 与执行授权

Phase 3C **只可在 Phase 3B 已完成并有可审计 completion evidence 后执行**。开始 3C-00 前必须同时证明：

1. [`PHASE-3B-PLAN.md`](PHASE-3B-PLAN.md) 的 completion criteria 已满足，且 3B 的 RunContext、
   ProcessHarness、IntegrationHost 与生产 transport/Module 接线已实际落地；
2. Phase 3A 的生产 Module 已由正式 caller 与测试通过同一个 Interface 使用；
3. Windows `develop` 的四个稳定 Required check 名称不变且全绿；
4. 当前 12 份报告、180 个 testcase 无删除、静默改名或未评审的合同弱化。

当前任一上游计划仍是 Planned、缺 Summary/evidence 或 Module 尚不可组合时，立即停止，不得通过复制
production parser/业务算法、测试宏、fake-mode EXE、`clearForTest` 或 private container 穿透推进 3C。

DG-25 同时是 hard prerequisite：本机 `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 默认只读，
本地 preflight/build/test 必须关闭 manifest 自动安装、固定 installed tree 并在不一致时 fail closed。3C 不得借 Linux
建设修改、清理或替换这两个本机路径。GitHub-hosted Ubuntu 只能使用 run-owned 临时 vcpkg root/install root；改变
本机或 CI 的 install root、manifest、baseline、triplet 或 tool identity，以及任何本机 package restore/install/remove/
update/upgrade，都必须先向用户展示精确命令/目标/影响并取得明确批准。

3C-00 是全阶段 hard preflight：锁定的 hosted Ubuntu、compiler、CMake、Qt 获取方式、vcpkg baseline/triplet
上，Gate、Status、Chat 三个 C++ 正式目标与 Varify Node 正式入口必须完成 configure/install、compile、link、
loader/startup proof；Qt production transport/session/model targets 也必须 compile/link。任一同源 proof 失败，
则 3C-01..3C-09 全部停止，只保留 blocker evidence，不得把部分成功描述为 Linux-ready。

比例化执行以 [`tests/CI-GOVERNANCE.md` section 2.1](../CI-GOVERNANCE.md#21-比例化执行合同) 为唯一过程权威。
本节的上游、DG/G、baseline、hosted-Ubuntu、tool/dependency 和 threat prerequisites 在 phase entry 读取/只读核验一次；
各 task 的 `read_first` 只保留 edited source、closest analog 与一个必要 authority。每个行为切片由
`scripts/linux-ci.sh --selector <task-id>` 暴露 focused RED/GREEN、meaningful mutation、真实 Integration/cleanup 和确定性非零
合同；同一 plan 代码稳定后由最后一个 task 的 selector 运行一次 owning runner。只有 3C-09-T2 运行无 selector 的完整 3C lane，
并聚合 secret/residue/report/diff/remote evidence。缺失依赖 fail closed，不自动 restore，也不重复 hash/fingerprint 审计。

## 2. 目标

- 在 GitHub-hosted Ubuntu 上建立与 Windows 正式构建共享生产源和 production library targets 的原生
  build/run 路径；Linux CI support 不等于应用容器化，也不等于 Linux release support（DG-10、DG-18、DG-23）。
- 以 disposable Redis、MySQL、Mailpit 验证真实 production Adapters、fresh schema、适用的 N-1 migration、
  四进程依赖编排、失败恢复与有界清理（G-012..G-015；DG-09、DG-11、DG-12、DG-15）。
- 建立应用拥有的 SchemaMigration Module 与 MessageCommit 深 Module；用数据库唯一约束和显式事务兑现
  `(sender,msg_uuid)` 幂等持久化、同一 `message_id` 和零部分提交（G-013、G-016；DG-24）。
- 用不可变 N-1 artifact/schema（存在时）验证 client/service/schema 实际进程矩阵；首发无已晋升基线时输出
  稳定 bootstrap blocker，绝不从旧源码重建或用静态 fixture 冒充程序兼容（G-017；DG-13、DG-21）。
- 将以上证据接入 GitHub-hosted Ubuntu `master` gate，并在 phase 末尾只运行一次完整 3C lane。

## 3. 非目标

- 不制作 Gate、Status、Chat、Varify application Docker image，不设计 Kubernetes 或生产容器编排。
- 不新增 self-hosted Required runner；本地 VM/WSL/MobaXterm 只可辅助开发，不是权威证据。
- 不承诺 Linux 正式发布支持；Windows 仍是正式发布平台。
- 不升级 C++、Qt、Node 或 npm dependencies。若 3C-00 证明当前 lock 不可用，停止并发起独立版本、legitimacy
  与兼容评审；不得在本计划内追随 latest。
- 不使用真实用户数据、真实邮箱、个人 token/password、共享数据库或公共 SMTP。
- 不把随机 fuzz、负载或 soak 放进 PR/master Required lane；这里只保留确定性 mutation/fault cases。
- 不扩大到 N-2；不在首个已晋升 N-1 artifact 缺失时制造“兼容通过”。
- 不借本阶段重写认证、密码或 TLS 产品合同；若既有安全缺口阻断发布，记录并路由独立评审，不宣称公网
  production-ready。

## 4. Locked decision / Gap coverage

| 决策或 Gap | 本阶段的不可弱化合同 | Owning plan |
| --- | --- | --- |
| DG-09 | 真实 Redis/MySQL/SMTP、schema、四进程与兼容只在 3C；3D 业务 E2E 与 Release 晋升不提前 | 3C-02..09 |
| DG-10 / DG-18 / DG-23 | hosted Ubuntu 原生同源 build/run；不构建应用镜像、不使用 self-hosted/个人环境作证据、不宣称 Linux release | 3C-00/01/09 |
| DG-11 / DG-12 | disposable services、run-id namespace、synthetic-only、deadline、`always()` evidence/cleanup；不可用或清理失败即失败 | 3C-02/04/06/07/09 |
| DG-13 / DG-15 / DG-21 | fresh 0→N、适用 N-1→N、checksum/audit/recovery；真实 N/N-1 artifact matrix 或稳定 bootstrap blocker | 3C-03/08/09 |
| DG-19 | 正式 EXE、IntegrationHost 与 tests 链接相同 production Modules；不加 fake EXE mode 或 test-only Interface | 3C-01/07 |
| DG-24 | authenticated sender、`UNIQUE(send_id, client_msg_uuid)`、whole-batch explicit transaction、Created/Existing/Conflict、同 message_id | 3C-03/05/07/08 |
| DG-25 | 本机 vcpkg 持久目录不可变；本地 runner 禁止隐式安装并 fail closed；hosted Ubuntu 只恢复到 run-owned 临时 root，任何 dependency/root identity 变化先审批 | 3C-00/01/09 |
| G-012 | Redis real Adapter 的 atomic TTL、timeout、disconnect/restart、bad-connection eviction、recovery、teardown | 3C-02/04/07/09 |
| G-013 | 可重复 schema/migration、真实 MySQL transaction/unique/race/rollback evidence | 3C-02/03/05/07/09 |
| G-014 | production SMTP Adapter 对 Mailpit 的真实 success/failure/deadline/参数与无公网证据 | 3C-02/06/07/09 |
| G-015 | 同源 Linux build 与 Gate/Status/Chat/Varify 四进程 ready、共享状态、恢复、逆序 teardown | 3C-00/01/02/07/09 |
| G-016 | 本阶段只交付 DG-24 的持久化/协议/客户端基础；双 ChatServer 公开 E2E 仍由 Phase 3D 完成 | 3C-05；route 3D |
| G-017 | 适用时运行实际 N/N-1 client/service/schema matrix；无基线时稳定阻断，Gap 不伪关闭 | 3C-03/08/09 |

DG-14 与 DG-20 已由 Phase 3B 拥有 transport/fault 边界；DG-16 由 Phase 3D 拥有双实例业务流；DG-17、
DG-22 由 Release gate 拥有统一 artifact/UAT/promotion。本阶段不得挪用这些后续 completion claim。

## 5. Module / Interface / seam 合同

以下名称和路径均为 **planned**，不是当前仓库事实。实施时若 Phase 3A/3B 已用等价名称落地，必须复用现有
production Interface 并在 Summary 记录实际符号，禁止再建同义 facade。

| Module | 唯一 Interface（planned target state） | production Adapter / implementation | in-memory/local Adapter | seam 与禁止项 |
| --- | --- | --- | --- | --- |
| Production build ownership | CMake imported targets `chat::gate_server_modules`、`chat::status_server_modules`、`chat::chat_server_modules`、`chat::client_modules` | Windows/Linux executable composition roots 链接相同 target/source owner | IntegrationHost/tests 链接同一 targets | test/EXE 不得各列一份 production `.cpp`；MSBuild parity 由 structure check 审计 |
| ProcessHarness | 复用 3B `ProcessHarness::Start/WaitReady/Stop/CollectEvidence` | `Win32ProcessAdapter` + planned `PosixProcessAdapter` | local controlled child used by lifecycle tests | OS process/signal/path 细节只在 Adapter；不把 PID 或 private registry 暴露给 caller |
| SchemaMigration | planned `Inspect/Plan/Apply/Verify`，结果含 version、migration IDs/checksums、reversibility 与 invariant report | `MySqlSchemaAdapter` 调真实 disposable MySQL | local plan/checksum Adapter 只验证 ordering/corruption | migration 属于应用/发布资产；容器 init SQL 不得成为第二 schema owner |
| MessageCommit | planned `CommitTextBatch(AuthenticatedPrincipal, ChatId, RecipientId, PendingTextMessage[]) -> CommitResult[]` | `MySqlMessageCommitAdapter` | in-memory Adapter 用于业务合同/fault ordering | sender 只来自 session；whole batch 一个显式 transaction；唯一约束最终仲裁并发 |
| Redis-backed business Modules | 复用 Phase 3A Store/Repository Interfaces | Gate/Status/Chat hiredis Adapters 与 Varify ioredis Adapter | 复用 Phase 3A in-memory stores | 3C 测生产 Adapter，不复制 routing/session/verification-code 算法；跨语言只共享行为合同，不造浅胶水层 |
| EmailDelivery | planned `SendVerification(VerificationMail, Deadline) -> DeliveryResult` | `NodemailerSmtpAdapter`，endpoint/security/auth 全由运行时配置 | `InMemoryEmailAdapter` 用于业务 unit contract | Mailpit 是外部可观察 sink，不是 fake Adapter；不得固定 qq.com 或输出 credential |
| DependencyCoordinator | planned `WaitHealthy/Endpoints/CollectEvidence/CleanupOwned` | GitHub service-container metadata Adapter | local manifest Adapter 验证 mapping/redaction | 只拥有本 run-id 的 schema/key/mail/temp；不尝试删除共享或预存资源 |
| CompatibilityMatrix | planned `ArtifactResolver/ManifestVerifier/SchemaFixtureResolver/RunMatrix` | durable promoted Release asset + MySQL restore tools | Phase 2.5 descriptor/wire fixtures 仅作静态 baseline | 静态 fixture 不能报告 program matrix pass；缺 N-1 返回稳定 bootstrap blocker |

## 6. 资源、证据与保密合同

每次执行创建一个不可变 `RunContext`：`run_id`、UTC start、全局 deadline、动态 application port bundle、
service-container mapped ports、MySQL database/schema name、Redis key prefix、Mailpit recipient domain、临时根目录、
child identity registry、bounded log paths 与 teardown ledger。资源 identity 至少为
`{run_id, owner_plan, logical_name, pid/container-id-or-endpoint, creation_nonce}`。

- service health 上限 120 秒；应用协议 ready 上限 30 秒；单请求/命令使用显式 deadline；graceful stop 上限
  10 秒，之后只对 identity 匹配的 owned child 做 kill escalation。job 必须设置硬 timeout。
- primary failure 先保存，再运行逆序 cleanup；primary failure 与 cleanup failure 分列，任一都让 gate 失败。
- 日志、JUnit、migration audit、topology manifest 与上传 artifact 仅含 synthetic identifiers、端口、稳定错误码和
  相对路径。不得含 password、token、验证码、SMTP body secret、connection string credential 或绝对 runner path。
- cleanup 只能 drop 本 run-id database/schema、删除本 prefix Redis keys、删除本 recipient/run-id 邮件、停止本
  identity child、移除本临时目录；GitHub job 最终销毁 containers，但这不能替代应用级残留审计。

## 7. Planned Test ID 与 report ownership

| Plan | Planned Test ID range | Planned behavior/report family | Owning public runner |
| --- | --- | --- | --- |
| 3C-00 | `T10-LNX-01..10` | Linux configure/compile/link/loader/startup proof；planned `linux_build_proof.xml` | pinned Ubuntu CMake/CTest + Node startup selector |
| 3C-01 | `T10-BLD-01..12` | target/source ownership、Windows/Linux parity、POSIX lifecycle；planned `linux_build_ownership.xml` | CTest label `phase3c-build-ownership` |
| 3C-02 | `T10-SVC-01..12` | Redis/MySQL/Mailpit health、mapped port、credential/redaction/cleanup；planned `linux_services.xml` | CTest label `phase3c-services` |
| 3C-03 | `T10-MIG-01..16` | migration plan/checksum/fresh/N-1-or-bootstrap/recovery；planned `linux_migration.xml` | CTest label `phase3c-migration` |
| 3C-04 | `T10-RDS-01..16`, `V08-REDIS-01..06` | C++/Node production Redis Adapter；planned `linux_redis.xml`, `varify_redis.xml` | CTest label + Node real-Redis selector |
| 3C-05 | `T10-MSG-01..20`, `Q05-MSG-01..08` | MessageCommit/MySQL/proto/client stable-ID mapping；planned `linux_message_commit.xml`, `client_message_commit.xml` | CTest labels `phase3c-message-commit`, `phase3c-client-message` |
| 3C-06 | `V09-SMTP-01..12` | SMTP/Mailpit success/failure/deadline/redaction；planned `varify_smtp.xml` | Node Mailpit selector |
| 3C-07 | `T10-4PROC-01..18` | four-process real-dependency lifecycle/flow/recovery；planned `linux_four_process.xml` | CTest label `phase3c-four-process` |
| 3C-08 | `T10-COMPAT-01..18` | descriptor plus actual artifact/client/service/schema matrix or bootstrap blocker；planned `linux_compatibility.xml` | compatibility matrix runner |
| 3C-09 | `T10-CLOSE-01..10` | manifest/report/cleanup/branch-gate structure；planned `linux_phase3c_gate.xml` | `scripts/linux-ci.sh --phase 3C` |

这些 ranges 是稳定合同 namespace，不是 runner testcase 数量。一个 Test ID 可由多个 runner case 证明，一个
runner case 也可覆盖多个相关合同；实现时必须按实际注册数更新 Module README、矩阵、runner manifest 和报告，
不得将 range 上界当作 case count。现有 12 份报告/180 case 始终作为继承 baseline 单独保留。

## 8. Dependency DAG 与 Wave

```text
Phase 3B complete
  -> 3C-00 hard Linux preflight
  -> 3C-01 shared target ownership
  -> 3C-02 disposable service proof
       -> 3C-03 schema/migration -> 3C-05 MessageCommit/MySQL --+
       -> 3C-04 Redis -------------------------------------------+-> 3C-07 four-process
       -> 3C-06 SMTP --------------------------------------------+       -> 3C-08 compatibility
                                                                                -> 3C-09 master closeout
```

| Wave | Plans | Depends on | Parallel file ownership |
| ---: | --- | --- | --- |
| 5 | 3C-00 | Phase 3B complete | hard preflight only |
| 6 | 3C-01 | 3C-00 | shared build ownership |
| 7 | 3C-02 | 3C-01 | service topology owner |
| 8 | 3C-03, 3C-04, 3C-06 | 3C-02 | migration, Redis, SMTP use disjoint production/test/CMake fragments |
| 9 | 3C-05 | 3C-03 | MessageCommit owns idempotency migration/protocol/client mapping |
| 10 | 3C-07 | 3C-04, 3C-05, 3C-06 | four-process composition only |
| 11 | 3C-08 | 3C-07 | compatibility resolver/matrix only |
| 12 | 3C-09 | 3C-08 | workflow/manifest/governance closeout owner |

## 9. Plan 3C-00 — Linux configure/compile/link/start hard preflight

Status: **Planned**
Wave: **5**
Depends on: **Phase 3B complete**
Coverage: **G-015; DG-09, DG-10, DG-18, DG-23**

<objective>
在固定 GitHub-hosted Ubuntu 上证明当前 locks 能原生 configure/install、compile、link 并进入正式 startup
路径，同时冻结 compiler、CMake、Qt acquisition、vcpkg baseline/triplet 与 evidence schema。这个 objective
是 3C-01..09 的硬门：任何正式目标未通过均停止后续 3C，不允许复制实现或删除目标来“通过”。
</objective>

<tasks>

<task id="3C-00-T1" type="tdd">
<name>RED：冻结 Linux toolchain、target inventory 与失败证据合同</name>
<read_first>
- `tests/plans/PHASE-3B-PLAN.md`
- `tests/plans/PHASE-3B-RELEASE-DECISIONS.md`
- `tests/plans/PHASE-3B-RELEASE-RESEARCH.md`
</read_first>
<files>
- `CMakeLists.txt` (planned; create)
- `CMakePresets.json` (planned; create)
- `triplets/x64-linux-chat-release.cmake` (planned; create)
- `cmake/LinuxPreflight.cmake` (planned; create)
- `tests/build/linux_preflight_contract.cmake` (planned; create)
- `scripts/linux-ci.sh` (planned; create)
- `.github/workflows/linux-ci.yml` (planned; create)
</files>
<action>
Per DG-10/DG-18/DG-23, define one `linux-x64-release` configure/build/test preset for an explicitly pinned hosted
Ubuntu label, compiler major, CMake version, Qt version/acquisition channel and the repository vcpkg baseline with
`x64-linux-chat-release`. Record immutable tool identities and expected target inventory in a machine-readable
`linux-preflight.json` schema: Gate, Status and Chat production executables; Varify `npm ci`/production main startup;
existing Qt `chat_network_core`, `chat_message_model`, `chat_session_core`; canonical proto generation. The workflow is
native runner execution and may use Docker only for later dependency services, never for application images. First add
contract checks `T10-LNX-01..10` that fail because the server CMake targets/startup evidence do not yet exist; RED must
name the missing target/tool/loader fact, not a network retry. Pin actions by full commit SHA and require exact image
digests when services are later added; do not invent a digest in the plan. If the repository locks cannot be resolved,
emit `LINUX_PREFLIGHT_BLOCKED` with the exact port/target/tool and stop before any version change.
`tests/build/linux_preflight_contract.cmake` accepts `CHAT_JUNIT_PATH`, `CHAT_EVIDENCE_PATH`, and
`CHAT_EXPECT=RED|GREEN`, writes the exact requested JUnit/evidence paths before returning the selector status, and never
prints environment or credential values. The verifier gives both the configure process and selector process a 600-second hard
timeout, terminates a timed-out process tree, and returns non-zero for timeout or any identity/result mismatch.
Create the public `scripts/linux-ci.sh` owner now, not at closeout. It accepts `--phase`, `--configuration`, `--selector`,
`--expect-red` and `--list-only`; each 3C task ID maps to the focused command/evidence contract in this plan, applies its declared
hard deadline, normalizes expected RED into a zero wrapper result only when the underlying selector fails for the named contract,
and otherwise propagates non-zero. Without `--selector` it runs the full phase lane and aggregate audits exactly once.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-00-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- The preset/toolchain contract names every required target and immutable tool identity without claiming availability.
- RED is reproducible and caused by absent Linux ownership/start proof, not by personal credentials or public SMTP.
- Workflow contains no application image build/push, self-hosted label, floating `ubuntu-latest`, fake EXE mode or secret value.
</acceptance_criteria>
</task>

<task id="3C-00-T2" type="auto" tdd="true">
<name>GREEN/mutation：完成同源 Linux proof，并把失败设为后续计划硬停止</name>
<read_first>
- `CMakeLists.txt` (planned output of 3C-00-T1)
- `CMakePresets.json` (planned output of 3C-00-T1)
- `proto/varify.proto`
</read_first>
<files>
- `CMakeLists.txt` (planned; modify)
- `cmake/LinuxPreflight.cmake` (planned; modify)
- `.github/workflows/linux-ci.yml` (planned; modify)
- `tests/build/linux_preflight_contract.cmake` (planned; modify)
- `tests/build/README.md` (planned; create)
</files>
<action>
Make the production CMake graph configure and link the three server executables from their existing production sources,
compile/link the existing Qt production libraries, run canonical proto generation, and run `npm ci` from the committed
Varify lock. Exercise each linked binary/Node main through a bounded loader/startup probe: CLI/config parsing and expected
dependency-unavailable fail-fast are valid here; protocol-ready with real dependencies is reserved for 3C-07 and must not
be fabricated with a fake mode. GREEN requires no missing shared library, undefined symbol, Windows-only include, detached
startup thread or unbounded process. Mutations must remove one production target, duplicate a production `.cpp` in a test
target, change the vcpkg baseline/triplet, or break one startup invocation; `T10-LNX-*` must fail each mutation. Upload the
redacted preflight manifest/logs even on failure. Set an explicit workflow condition so any non-PASS preflight prevents
3C-01..09 jobs from executing.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-00-T2</automated>
</verify>
<acceptance_criteria>
- All named targets resolve from the same repository production sources and current locks; no Linux copy of parser/business logic exists.
- `linux-preflight.json` records compiler/CMake/Qt/vcpkg/Node identities, target/link/start results and a PASS status without secrets.
- Any failed configure/compile/link/loader/start proof blocks every downstream 3C plan with a stable diagnostic.
- Passing evidence is explicitly limited to CI portability, not application containerization or Linux release support.
</acceptance_criteria>
</task>

</tasks>

## 10. Plan 3C-01 — Shared production target ownership and POSIX Adapter

Status: **Planned**
Wave: **6**
Depends on: **3C-00 PASS**
Coverage: **G-015; DG-10, DG-18, DG-19**

<objective>
让 Windows/Linux 正式 composition roots、3B IntegrationHost 与 tests 链接同一 production library targets，
并把 process/signal/path 差异压入 Win32/POSIX Adapters。Interface 保持平台无关，build structure 自动阻止
重复 production source lists。
</objective>

<tasks>

<task id="3C-01-T1" type="tdd">
<name>RED：建立跨平台 target/source ownership 与 Interface reachability 检查</name>
<read_first>
- `CMakeLists.txt` (planned output of 3C-00)
- `chat/CMakeLists.txt`
- `scripts/windows-local.ps1`
</read_first>
<files>
- `cmake/ProductionTargets.cmake` (planned; create)
- `cmake/ProductionModuleRegistry.cmake` (planned; create)
- `tests/build/production_target_ownership_tests.cmake` (planned; create)
- `scripts/windows-local.ps1` (existing; modify)
</files>
<action>
Per DG-18/DG-19, declare the planned logical imported aliases `chat::gate_server_modules`,
`chat::status_server_modules`, `chat::chat_server_modules`, `chat::client_modules` and map them to the actual Phase 3A/3B
production libraries after upstream completion. If an equivalent upstream target exists, alias/reuse it and record the
actual name; do not create a second facade. Add `T10-BLD-01..06` checks that enumerate every production translation unit
and fail when an EXE/test manually compiles a production `.cpp`, when Windows and Linux target manifests diverge, when a
production caller bypasses its Interface, or when a test references a private implementation. The registry must accept
independent per-Module CMake fragments so Wave 8 migration/Redis/SMTP owners do not edit a shared file concurrently.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-01-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Every Phase 3A/3B Module has one production source owner reachable by Windows/Linux callers and tests.
- The structure check names duplicate/missing source, target and caller; it does not count comments or accept empty placeholders.
- Same-wave feature fragments can register unique targets without touching `ProductionTargets.cmake`.
</acceptance_criteria>
</task>

<task id="3C-01-T2" type="auto" tdd="true">
<name>GREEN/mutation：落地 POSIX process/signal/path Adapter 与双平台 parity</name>
<read_first>
- `tests/integration/process/ProcessHarness.h` (planned Phase 3B output)
- `tests/integration/process/Win32ProcessAdapter.cpp` (planned Phase 3B output)
- `tests/integration/RunContext.h` (planned Phase 3B output)
</read_first>
<files>
- `tests/integration/process/PosixProcessAdapter.h` (planned; create)
- `tests/integration/process/PosixProcessAdapter.cpp` (planned; create)
- `tests/integration/process/process_harness_posix_tests.cpp` (planned; create)
- `cmake/tests/process-harness-posix.cmake` (planned; create)
- `cmake/ProductionTargets.cmake` (planned; modify)
- existing Phase 3A/3B production library project files (modify only as required to link the same sources)
</files>
<action>
Implement `PosixProcessAdapter` behind the unchanged 3B ProcessHarness Interface: create a dedicated process group,
capture bounded stdout/stderr, use protocol probes for ready, send SIGTERM, wait 10 seconds, then SIGKILL only when
`{pid,start-identity,run-id}` still matches; reap every child and record each cleanup action. Move platform path/signal
selection behind compile guards in the Adapter, never in business Modules. Link Windows and Linux composition roots plus
IntegrationHost/tests to the registered production targets. Add `T10-BLD-07..12`; mutations that omit `waitpid`, reuse a
stale PID, bypass graceful stop, or compile a second business source list must fail. Run focused POSIX tests first, then
the owning CTest label; run only affected Windows public runners, not full `RunAllTests`, and keep the four stable check
names unchanged.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-01-T2</automated>
</verify>
<acceptance_criteria>
- Win32/POSIX Adapters satisfy one ProcessHarness Interface and expose no OS details to test scenarios.
- Child processes, descriptors, pipes, groups and temporary paths are released within deadlines and recorded in the ledger.
- Windows and Linux executables/tests use the same production Module implementations; no copied algorithm or test macro exists.
- Baseline reports/cases and all four existing `develop` check names remain intact.
</acceptance_criteria>
</task>

</tasks>

## 11. Plan 3C-02 — Hosted Ubuntu disposable dependency proof

Status: **Planned**
Wave: **7**
Depends on: **3C-01 PASS**
Coverage: **G-012, G-013, G-014, G-015; DG-11, DG-12, DG-23**

<objective>
在固定 GitHub-hosted Ubuntu job 中证明 Redis、MySQL、Mailpit service containers 的 immutable image
identity、随机 host ports、health、CI-only synthetic credential expression、RunContext namespace、失败证据与
`always()` cleanup。此计划只证明 disposable infrastructure seam，不把 Adapter-only probe 称为四进程 Integration。
</objective>

<tasks>

<task id="3C-02-T1" type="tdd">
<name>RED：冻结 service lock、endpoint mapping、redaction 与 cleanup ledger schema</name>
<read_first>
- `.github/workflows/linux-ci.yml` (planned output of 3C-00)
- `tests/CI-GOVERNANCE.md`
- `tests/plans/PHASE-3B-RELEASE-RESEARCH.md`
</read_first>
<files>
- `ci/services.lock.json` (planned; create)
- `tests/integration/dependencies/DependencyCoordinator.h` (planned; create)
- `tests/integration/dependencies/DependencyCoordinator.cpp` (planned; create)
- `tests/integration/dependencies/service_contract_tests.cpp` (planned; create)
- `tests/integration/fixtures/phase3c-synthetic-v1.json` (planned; create)
- `cmake/tests/service-container-contracts.cmake` (planned; create)
</files>
<action>
Define `services.lock.json` entries for `redis`, `mysql`, and `mailpit` with required fields `repository`, exact `tag`,
`digest`, internal ports, health command/interval/timeout/retries and provenance date; implementation must resolve and
record real official-image digests during execution, never invent them. Define DependencyCoordinator results
`HealthyEndpoints`, `HealthTimeout`, `CredentialExpressionUnavailable`, `CleanupFailed` and evidence containing mapped
ports but no credentials. Add `T10-SVC-01..06` RED checks for floating tag/digest, fixed host port, missing health, shared
database/key/email namespace, missing deadline, unredacted credential marker and cleanup without matching run identity.
Freeze the versioned synthetic dataset with Unicode, maximum valid lengths, duplicates, invalid relationships and enough
rows to exercise batch/order behavior; all identities include run-id and contain no real email/token/code.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-02-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Exact image digest and health contracts are machine-readable; no floating tag is accepted.
- Random host-port and run-id namespace contracts cover Redis keys, MySQL database/schema, Mailpit recipients and temp paths.
- Evidence schema separates primary and cleanup failures and rejects secret-like values.
</acceptance_criteria>
</task>

<task id="3C-02-T2" type="auto" tdd="true">
<name>GREEN/mutation：证明三种 disposable services 与 always cleanup</name>
<read_first>
- `ci/services.lock.json` (planned output of 3C-02-T1)
- `tests/integration/dependencies/DependencyCoordinator.h` (planned output of 3C-02-T1)
- `.github/workflows/linux-ci.yml` (planned output of 3C-00)
</read_first>
<files>
- `.github/workflows/linux-ci.yml` (planned; modify)
- `tests/integration/dependencies/DependencyCoordinator.cpp` (planned; modify)
- `tests/integration/dependencies/service_contract_tests.cpp` (planned; modify)
- `tests/integration/dependencies/README.md` (planned; create)
</files>
<action>
Per DG-11/DG-12/DG-23, add Redis, MySQL and Mailpit services to the pinned Ubuntu job using exact tag+digest and random
host mappings for 6379, 3306, 1025 and 8025. Obtain endpoints through `job.services.<id>.ports[...]`; wait for container
health within 120 seconds before exposing them to tests. Prove the selected `services.env` expression can provide a
CI-only synthetic MySQL credential before adopting it; the first step masks the resolved value and subsequent commands
pass it through protected environment/stdin, never argv/log/report. If the expression is unavailable, return
`CredentialExpressionUnavailable` and stop 3C; only after an explicit contract update may implementation use step-managed
disposable containers or a protected non-personal CI-only secret. Implement `T10-SVC-07..12`: health timeout, wrong mapped
port, service termination, redaction scan, run-id cleanup and cleanup-failure propagation. `always()` uploads bounded service
and teardown evidence, then verifies the run-id database/key/mail/temp inventory is empty; job container destruction is
additional defense, not the assertion. Mutations removing health, deadline, masking, random mapping or cleanup must fail.
Keep the workflow name `Linux CI`, the job name `Phase 3C disposable services`, and the artifact name
`phase3c-services-evidence`. That artifact contains `linux_services.xml`, `service-endpoints.json`, and `teardown.json`;
the JSON records workflow run ID, head SHA, exact image digests, primary/cleanup result, and post-cleanup owned-resource
count without credential values.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-02-T2</automated>
</verify>
<acceptance_criteria>
- All three services are disposable, digest-pinned, healthy, reachable only through their mapped localhost ports and bounded by job timeout.
- No personal/shared service, fixed developer endpoint, public SMTP or credential value appears in source, command output or evidence.
- Normal and injected-failure runs leave no run-owned database/key/mail/temp resource; cleanup failure fails the job.
- The result is infrastructure proof only and is not labeled four-process Integration.
</acceptance_criteria>
</task>

</tasks>

## 12. Plan 3C-03 — Application-owned SchemaMigration Module

Status: **Planned**
Wave: **8**
Depends on: **3C-02 PASS**
Coverage: **G-013, G-017; DG-12, DG-13, DG-15, DG-21, DG-24**

<objective>
建立唯一应用拥有的 SchemaMigration Module、immutable migration/checksum ledger、fresh 0→N 路径与适用的
promoted N-1→N 路径。容器 init 只启动空 MySQL；schema、fixture version、审计、备份和恢复都由同一 Module
负责。首发无 N-1 时输出稳定 bootstrap blocker，不伪造 schema pass。
</objective>

<tasks>

<task id="3C-03-T1" type="tdd">
<name>RED：冻结 migration Interface、ledger 与 production-shaped schema invariants</name>
<read_first>
- `tests/plans/PHASE-3B-RELEASE-DECISIONS.md`
- `tests/plans/PHASE-3B-RELEASE-RESEARCH.md`
- `GateServer/GateServer/MysqlDao.cpp`
</read_first>
<files>
- `shared/storage/SchemaMigration.h` (planned; create)
- `database/migrations/manifest.json` (planned; create)
- `database/migrations/0001-initial-schema.sql` (planned; create)
- `database/fixtures/phase3c-fresh-v1.json` (planned; create)
- `database/fixtures/n-minus-1-bootstrap.json` (planned; create)
- `tests/server/storage/schema_migration_contract_tests.cpp` (planned; create)
- `cmake/modules/schema-migration.cmake` (planned; create)
- `cmake/tests/schema-migration-tests.cmake` (planned; create)
</files>
<action>
Define the planned Interface `Inspect(connection) -> MigrationState`, `Plan(from,to) -> MigrationPlan`,
`Apply(plan,RunContext) -> MigrationResult`, and `Verify(expected) -> InvariantReport`. Freeze an immutable manifest whose
entries include ordered migration ID, from/to version, SQL relative path, SHA-256 checksum, reversible flag, backup
requirement, forward-recovery instruction and expected invariants. Model the tables/procedure actually required by the
three current DAOs (`user`, `apply_friend`, `friend`, `chat`, `private_chat`, `chat_message`, `reg_user`) from audited source;
do not invent production data. The `schema_version` ledger records migration ID/checksum/version/application result.
Create `T10-MIG-01..08` RED tests for empty DB, checksum drift, out-of-order/duplicate ID, unknown current version, missing
recovery metadata, and a bootstrap descriptor that tries to impersonate an N-1 schema. `n-minus-1-bootstrap.json` contains
only `BOOTSTRAP_NO_PROMOTED_N_MINUS_1` plus resolver requirements, never a fabricated dump.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-03-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- SchemaMigration is one deep production Interface; tests do not parse SQL or inspect a private migration container.
- Fresh schema is derived by applying ordered migrations from 0, not from a separate final schema/init script.
- Every irreversible migration has backup, forward-recovery and stop metadata; rollback is required only when declared reversible.
- Bootstrap metadata cannot be mistaken for a promoted N-1 fixture or passing compatibility evidence.
</acceptance_criteria>
</task>

<task id="3C-03-T2" type="auto" tdd="true">
<name>GREEN：实现 migration Module/CLI 与 fresh 0→N audit</name>
<read_first>
- `shared/storage/SchemaMigration.h` (planned output of 3C-03-T1)
- `database/migrations/manifest.json` (planned output of 3C-03-T1)
- `tests/integration/dependencies/DependencyCoordinator.h` (planned 3C-02 output)
</read_first>
<files>
- `shared/storage/SchemaMigration.cpp` (planned; create)
- `shared/storage/MySqlSchemaAdapter.h` (planned; create)
- `shared/storage/MySqlSchemaAdapter.cpp` (planned; create)
- `tools/schema-migrate/main.cpp` (planned; create)
- `tests/server/storage/schema_migration_integration_tests.cpp` (planned; create)
- `cmake/modules/schema-migration.cmake` (planned; modify)
- `cmake/tests/schema-migration-tests.cmake` (planned; modify)
</files>
<action>
Implement SchemaMigration against the existing locked MySQL Connector/C++ JDBC target. The CLI accepts endpoint,
credential through protected environment/stdin, run-id database name, target version, manifest path, deadline and audit
output path; it never logs the credential/connection string. For a fresh run-id database, call `Inspect`, verify version 0,
plan and apply every immutable migration, explicitly record each checksum/result, then verify tables, indexes, stored
procedure and production-shaped synthetic invariants. Use prepared statements for fixture data. A failed migration retains
the primary error, marks the ledger failed, runs only declared safe recovery, and prevents application startup. Implement
`T10-MIG-09..12`. GREEN requires repeated fresh creation in distinct run-ids to yield the same schema version/checksums;
mutation of SQL after checksum, missing procedure/index or partial ledger write must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-03-T2</automated>
</verify>
<acceptance_criteria>
- Empty MySQL becomes N only through the production SchemaMigration Interface and exact manifest.
- Audit records run-id, input/target version, migration IDs/checksums, invariants and recovery result without secrets.
- Failed apply never reports target version and blocks dependent app startup; both databases are identity-cleaned.
</acceptance_criteria>
</task>

<task id="3C-03-T3" type="auto" tdd="true">
<name>N-1/恢复 mutation：执行真实升级或稳定 bootstrap blocker</name>
<read_first>
- `database/fixtures/n-minus-1-bootstrap.json` (planned output of 3C-03-T1)
- `shared/storage/SchemaMigration.h` (planned output of 3C-03-T1)
- `tests/server/protocol/fixtures` (existing Phase 2.5 baseline directory)
</read_first>
<files>
- `tests/server/storage/schema_migration_integration_tests.cpp` (planned; modify)
- `tests/server/storage/migration_recovery_tests.cpp` (planned; create)
- `database/fixtures/n-minus-1-bootstrap.json` (planned; modify only when a real promoted baseline exists)
- `tests/server/storage/README.md` (planned; create)
</files>
<action>
Add `T10-MIG-13..16`. If a durable promoted N-1 artifact/schema fixture exists, verify its external digest and internal
manifest, restore it into an isolated database, run N-1→N through the same Module, prove legacy rows and invariants, then
exercise reversible rollback or irreversible backup+forward recovery exactly as declared. If no promoted N-1 exists,
emit `BOOTSTRAP_NO_PROMOTED_N_MINUS_1` as a non-PASS compatibility status with required artifact pointer fields; run the
Phase 2.5 descriptor/wire checks separately and do not call them schema/process evidence. A digest mismatch, undeclared
rollback, schema version drift, silent data loss or restore from mutable cache/old source is a hard stop. Mutations corrupt
fixture digest, migration checksum, legacy invariant and recovery path; each must fail before app startup. Then run the
owning `phase3c-migration` CTest label, not the full phase lane.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-03-T3</automated>
</verify>
<acceptance_criteria>
- Fresh 0→N is PASS and auditable.
- When N-1 exists, actual immutable input is upgraded and validated; when absent, no substitute fixture or pass is generated.
- Reversible/irreversible recovery behavior matches manifest metadata and preserves both primary and cleanup diagnostics.
- G-017 remains open unless actual artifact/schema matrix evidence exists.
</acceptance_criteria>
</task>

</tasks>

## 13. Plan 3C-04 — Redis production Adapter Integration

Status: **Planned**
Wave: **8**
Depends on: **3C-02 PASS**
Coverage: **G-012; DG-09, DG-11, DG-12**

<objective>
通过真实 disposable Redis 验证 Gate/Status/Chat hiredis 与 Varify ioredis production Adapters 的 atomic TTL、
有限 connect/borrow/command deadline、断线/重启、坏连接淘汰、恢复和 pool teardown。业务规则仍由 Phase 3A
Modules 拥有；测试通过同一 Store/Repository Interfaces，不复制 Redis 之上的算法。
</objective>

<tasks>

<task id="3C-04-T1" type="tdd">
<name>RED：冻结跨 C++/Node 的 Redis 可观察合同与 fault schedule</name>
<read_first>
- `GateServer/GateServer/RedisMgr.h`
- `GateServer/GateServer/RedisMgr.cpp`
- `StatusServer/StatusServer/RedisMgr.h`
</read_first>
<files>
- `shared/redis/RedisConnectionPolicy.h` (planned; create)
- `tests/server/data/redis_real_adapter_tests.cpp` (planned; create)
- `VarifyServer/test/integration/redis/redis-real-adapter.test.js` (planned; create)
- `tests/integration/faults/redis-fault-schedule.json` (planned; create)
- `cmake/modules/redis-adapters.cmake` (planned; create)
- `cmake/tests/redis-adapter-tests.cmake` (planned; create)
</files>
<action>
Define the deep connection-policy behavior shared by C++ production Adapters: finite connect, borrow and command
deadlines; `Healthy/Bad/Closed` connection disposition; a failed/timeout connection is never returned healthy; reconnect
creates or validates a usable context; shutdown wakes borrowers and joins heartbeat/lifecycle workers. Do not expose a new
business port: Gate/Status/Chat Modules and tests continue through the Phase 3A Store/Repository Interfaces, with the
production Redis Adapter selected. Define `T10-RDS-01..08` and `V08-REDIS-01..03` RED cases for `SET/GET`, atomic
`SET value EX seconds`, expiry miss, refused connection, command timeout, service disconnect/restart, bad-connection
eviction, bounded pool teardown and run-prefix cleanup. Fault schedule controls the owned disposable service; it contains
no sleeps without deadline and no public endpoint.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-04-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Tests cross production Store/Repository Interfaces or the production Adapter role, never private pool containers.
- Atomic TTL, deadline, connection disposition, recovery and cleanup are defined identically at the behavior level for C++ and Node.
- Faults are deterministic, service-owned and bounded; no personal Redis or fixed host port is referenced.
</acceptance_criteria>
</task>

<task id="3C-04-T2" type="auto" tdd="true">
<name>GREEN：实现 hiredis finite lifecycle 并消除 detached ownership</name>
<read_first>
- `shared/redis/RedisConnectionPolicy.h` (planned output of 3C-04-T1)
- `GateServer/GateServer/RedisMgr.cpp`
- `StatusServer/StatusServer/RedisMgr.cpp`
</read_first>
<files>
- `shared/redis/RedisConnectionPolicy.cpp` (planned; create)
- `GateServer/GateServer/RedisMgr.h` (existing; modify)
- `GateServer/GateServer/RedisMgr.cpp` (existing; modify)
- `StatusServer/StatusServer/RedisMgr.h` (existing; modify)
- `StatusServer/StatusServer/RedisMgr.cpp` (existing; modify)
- `ChatServer/ChatServer/RedisMgr.h` (existing; modify)
- `ChatServer/ChatServer/RedisMgr.cpp` (existing; modify)
- `tests/server/data/redis_real_adapter_tests.cpp` (planned; modify)
</files>
<action>
Use the locked hiredis implementation (`redisConnectWithTimeout`, command timeout and reconnect support) behind each
existing production Adapter; centralize only connection/deadline/disposition policy, not service-specific key/business
logic. All pool waits are finite, bad contexts are evicted, restart recovery uses a newly validated connection, and every
heartbeat/lifecycle thread is joinable and stopped before pool destruction. Add `T10-RDS-09..16`. Mutate each Adapter to
return a failed context, omit a timeout, keep a borrower blocked or skip join; focused tests must fail and show no leftover
thread/socket/key prefix. Do not change external business result semantics without a separate contract review.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-04-T2</automated>
</verify>
<acceptance_criteria>
- Gate/Status/Chat production Redis Adapters use finite connect/borrow/command policy and recover after owned service restart.
- No failed context returns to a healthy pool; all workers join and all sockets/prefixes are released.
- Phase 3A business Modules remain the sole owner of routing/session/verification behavior.
</acceptance_criteria>
</task>

<task id="3C-04-T3" type="auto" tdd="true">
<name>GREEN/mutation：实现 Varify atomic TTL、recovery 与 owning runners</name>
<read_first>
- `VarifyServer/redis.js`
- `VarifyServer/config.js`
- `VarifyServer/package.json`
</read_first>
<files>
- `VarifyServer/redis.js` (existing; modify)
- `VarifyServer/test/integration/redis/redis-real-adapter.test.js` (planned; modify)
- `VarifyServer/test/integration/redis/README.md` (planned; create)
</files>
<action>
Keep the committed ioredis lock and make the production Adapter runtime-composable without creating a client at module
import. Replace two-step SET+EXPIRE with one atomic `SET key value EX seconds`; configure finite connect/command/retry
deadlines, fail closed after exhaustion, reconnect after the owned service returns, and close the client during teardown.
Complete `V08-REDIS-04..06`. Mutation restores two-command TTL, an import-time global client or unbounded retry; Node tests
must fail. Run the focused Node selector, the C++ Redis label, then their owning public runners; do not run the full 3C lane.
Any package-lock change is a hard stop requiring separate package-legitimacy review.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-04-T3</automated>
</verify>
<acceptance_criteria>
- Node and C++ production Adapters satisfy the real Redis behavior contracts with current locks.
- Expired verification/session values miss, disconnects fail within deadline, recovery uses healthy connections and teardown closes all clients.
- `linux_redis.xml` and `varify_redis.xml` contain no secret and actual registered case counts are derived later.
</acceptance_criteria>
</task>

</tasks>

## 14. Plan 3C-05 — MessageCommit and MySQL idempotent persistence

Status: **Planned**
Wave: **9**
Depends on: **3C-03 PASS or auditable bootstrap status**
Coverage: **G-013, G-016; DG-15, DG-24**

<objective>
建立 MessageCommit 深 Module 与 MySQL production Adapter，以 authenticated session sender 为唯一身份来源，
让 whole batch 在一个显式 transaction 中提交。数据库以 `UNIQUE(send_id, client_msg_uuid)` 仲裁并发；同一
sender/UUID/相同 payload 重试返回原 `message_id`，不同 payload 返回稳定 Conflict，任何非幂等错误零部分提交。
</objective>

<tasks>

<task id="3C-05-T1" type="tdd">
<name>RED：冻结 MessageCommit Interface、idempotency migration 与结果语义</name>
<read_first>
- `ChatServer/ChatServer/LogicSystem.cpp`
- `ChatServer/ChatServer/MysqlDao.h`
- `ChatServer/ChatServer/MysqlDao.cpp`
</read_first>
<files>
- `shared/messages/MessageCommit.h` (planned; create)
- `database/migrations/0002-message-idempotency.sql` (planned; create)
- `tests/server/data/message_commit_contract_tests.cpp` (planned; create)
- `tests/server/data/message_commit_integration_tests.cpp` (planned; create)
- `cmake/modules/message-commit.cmake` (planned; create)
- `cmake/tests/message-commit-tests.cmake` (planned; create)
</files>
<action>
Define `PendingTextMessage { client_msg_uuid, content }`, `CommitDisposition { Created, Existing }`,
`CommitItem { client_msg_uuid, message_id, disposition }`, stable `CommitError { UnauthorizedSender, InvalidUuid,
InvalidMembership, Conflict, StorageUnavailable, DeadlineExceeded }`, and
`CommitTextBatch(AuthenticatedPrincipal sender, ChatId, RecipientId, span<PendingTextMessage>, Deadline)`.
Lock whole-batch atomicity: all items validate before commit; a non-idempotent error rolls back the whole batch. Add a
nullable legacy column `client_msg_uuid CHAR(36) CHARACTER SET ascii COLLATE ascii_bin` to `chat_message`, require non-null
canonical UUID for every new MessageCommit write, and create `UNIQUE(send_id, client_msg_uuid)`; legacy rows remain NULL
and must never receive fabricated sender-supplied UUIDs. Add `T10-MSG-01..08` RED tests for sender mismatch, malformed or
duplicate-in-batch UUID, Created, Existing same ID, payload conflict, concurrent duplicate, mid-batch conflict and commit
failure. Tests call the production Interface with in-memory or MySQL Adapter; they do not call private DAO helpers.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-05-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- One MessageCommit Interface owns authentication, validation, idempotency result and transaction semantics.
- The migration is checksum-registered through SchemaMigration and preserves legacy NULL UUID rows without fabricating identity.
- Concurrent uniqueness is database-enforced; no process-local set or Redis-only lock is accepted.
</acceptance_criteria>
</task>

<task id="3C-05-T2" type="auto" tdd="true">
<name>GREEN：实现 explicit transaction、Created/Existing/Conflict 与零部分提交</name>
<read_first>
- `shared/messages/MessageCommit.h` (planned output of 3C-05-T1)
- `database/migrations/0002-message-idempotency.sql` (planned output of 3C-05-T1)
- `ChatServer/ChatServer/MysqlDao.cpp`
</read_first>
<files>
- `shared/messages/MessageCommit.cpp` (planned; create)
- `ChatServer/ChatServer/MySqlMessageCommitAdapter.h` (planned; create)
- `ChatServer/ChatServer/MySqlMessageCommitAdapter.cpp` (planned; create)
- `ChatServer/ChatServer/MysqlDao.h` (existing; modify)
- `ChatServer/ChatServer/MysqlDao.cpp` (existing; modify)
- `ChatServer/ChatServer/LogicSystem.h` (existing; modify)
- `ChatServer/ChatServer/LogicSystem.cpp` (existing; modify)
- `tests/server/data/message_commit_integration_tests.cpp` (planned; modify)
</files>
<action>
Replace `AddChatMessageList` ownership with MessageCommit; retain a compatibility shim only if an existing production
caller still needs it, and make that shim delegate to the Interface rather than duplicate SQL. `LogicSystem` obtains sender
from the authenticated `CSession`/principal and rejects a payload `from_uid` mismatch before storage. The MySQL Adapter
borrows with a finite deadline, starts one explicit transaction, validates membership/recipient, inserts each new row with
prepared statements, and on unique conflict reads the existing row: equal chat/recipient/content returns Existing with the
same server `message_id`; any mismatch returns Conflict and rolls back. Commit exactly once after every item succeeds;
every error path explicitly rolls back before restoring autocommit/returning the connection. A deterministic real-DB
mid-batch case inserts item one then hits conflicting item two and proves item one is absent afterward. Add
`T10-MSG-09..16`; concurrent barriers race the same UUID and prove one Created plus Existing responses with one row/ID.
Mutations removing commit/rollback/unique handling, trusting payload sender or generating a new ID on retry must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-05-T2</automated>
</verify>
<acceptance_criteria>
- Same `(authenticated sender, client_msg_uuid)` and same payload always returns the original `message_id` with one DB row.
- Reused UUID with different chat/recipient/content is a stable Conflict; spoofed sender is rejected before SQL.
- The whole batch commits once or leaves zero new rows; no cleanup operation can implicitly commit a failed batch.
- Connection, transaction and pool worker lifetimes are finite and teardown evidence is clean.
</acceptance_criteria>
</task>

<task id="3C-05-T3" type="auto" tdd="true">
<name>GREEN/mutation：贯通 peer/history/client stable-ID mapping 与 owning runners</name>
<read_first>
- `proto/chat.proto`
- `scripts/protocol-compatibility.js`
- `ChatServer/ChatServer/LogicSystem.cpp`
</read_first>
<files>
- `proto/chat.proto` (existing; modify only through canonical authority)
- `generated/proto/cpp/*` (generated outputs; regenerate, never hand-edit)
- `ChatServer/ChatServer/LogicSystem.cpp` (existing; modify)
- `chat/tcpmgr.h` (existing; modify)
- `chat/tcpmgr.cpp` (existing; modify)
- `chat/messagerecord.h` (existing; modify)
- `chat/messagelistmodel.h` (existing; modify)
- `chat/messagelistmodel.cpp` (existing; modify)
- `chat/tests/message-model/message_commit_model_tests.cpp` (planned; create)
- `chat/CMakeLists.txt` (existing; modify)
</files>
<action>
Preserve all existing protobuf field numbers and add only the required stable client UUID field to historical `ChatMessage`
using the next unused field number; regenerate through the canonical pipeline. Ensure same-instance response, peer
`TextChatData`, history rows and Qt records carry both `client_msg_uuid` where available and server `message_id`; old
messages without UUID remain readable. The client completes a pending item only after Created/Existing maps the original
UUID to the same server ID, de-duplicates notification/history by server ID, and preserves the original UUID for bounded
retry after disconnect. Add `T10-MSG-17..20` and `Q05-MSG-01..08`. Mutations drop UUID/ID from peer or history, accept a
different ID on retry, duplicate a model row, renumber proto fields or hand-edit generated output; focused server/client/
compatibility selectors must fail. Run focused selectors, then owning CTest/Qt public runners, never the full 3C lane.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-05-T3</automated>
</verify>
<acceptance_criteria>
- Server response, peer RPC, history and client model preserve stable server ID and available client UUID without breaking old rows/wire fixtures.
- Retry acknowledgement uses the original `message_id`; notification/history duplicates do not duplicate client rows.
- Canonical proto is the only editable wire authority and generated drift/field renumbering is rejected.
- Actual case/report totals remain deferred to 3C-09 registration audit.
</acceptance_criteria>
</task>

</tasks>

## 15. Plan 3C-06 — Varify SMTP production Adapter with Mailpit

Status: **Planned**
Wave: **8**
Depends on: **3C-02 PASS**
Coverage: **G-014; DG-09, DG-11, DG-12**

<objective>
让 Varify 通过一个生产 EmailDelivery Interface 使用可运行时配置的 Nodemailer SMTP Adapter，并通过 disposable
Mailpit SMTP/API 验证真实 success/failure/deadline/参数与 redaction。生产和测试 caller 共用 Interface；
in-memory 与 SMTP 两个 Adapter 形成真实 internal seam。
</objective>

<tasks>

<task id="3C-06-T1" type="tdd">
<name>RED：冻结 EmailDelivery Interface、runtime config 与 Mailpit observable contract</name>
<read_first>
- `VarifyServer/email.js`
- `VarifyServer/config.js`
- `VarifyServer/config.json`
</read_first>
<files>
- `VarifyServer/adapters/email-delivery.js` (planned; create)
- `VarifyServer/adapters/in-memory-email-adapter.js` (planned; create)
- `VarifyServer/test/integration/smtp/smtp-mailpit.test.js` (planned; create)
- `VarifyServer/test/fixtures/mailpit-message-v1.json` (planned; create)
</files>
<action>
Define `EmailDelivery.SendVerification({ recipient, code, correlationId }, deadline) -> DeliveryResult` with stable results
`Delivered`, `Rejected`, `Unavailable`, `DeadlineExceeded`, `InvalidConfig`; callers cannot access Nodemailer transport.
Freeze runtime keys for host, port, secure mode, optional auth mode/user/password reference and send deadline. `auth=none`
must be valid for Mailpit; production auth values come only from environment/secret provider and are never returned. Add
`V09-SMTP-01..06` RED tests for required/optional config combinations, success envelope, refusal, disconnect, deadline,
duplicate completion, and response/log redaction. Mailpit observation queries by run-id recipient/correlation ID and asserts
headers/body shape using synthetic values; it never makes a public-network call. The in-memory Adapter records only outcome
and safe correlation metadata, not secret bodies.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-06-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- One EmailDelivery Interface has both production SMTP and in-memory Adapters; business caller does not import Nodemailer.
- Runtime config supports Mailpit `secure=false`, `auth=none` and production auth references without values in source/evidence.
- Test fixtures are synthetic/versioned and public SMTP is structurally rejected in CI mode.
</acceptance_criteria>
</task>

<task id="3C-06-T2" type="auto" tdd="true">
<name>GREEN/mutation：实现 Nodemailer Adapter 与 Mailpit API evidence</name>
<read_first>
- `VarifyServer/adapters/email-delivery.js` (planned output of 3C-06-T1)
- `VarifyServer/email.js`
- `VarifyServer/config.js`
</read_first>
<files>
- `VarifyServer/adapters/smtp-email-adapter.js` (planned; create)
- `VarifyServer/email.js` (existing; modify)
- `VarifyServer/config.js` (existing; modify)
- `VarifyServer/config.json` (existing template; modify without values)
- `VarifyServer/server.js` (existing; modify)
- `VarifyServer/test/integration/smtp/smtp-mailpit.test.js` (planned; modify)
- `VarifyServer/test/integration/smtp/README.md` (planned; create)
</files>
<action>
Implement NodemailerSmtpAdapter with runtime endpoint/security/auth and an AbortSignal/deadline; compose it in the real
Varify main and inject InMemoryEmailAdapter only in unit composition, not via an EXE flag. Against mapped Mailpit ports,
send a synthetic verification mail and query API 8025 until the run-id/correlation ID is observed within deadline; assert
recipient, subject/headers and expected synthetic body shape. Inject SMTP refusal, delayed/unavailable sink, invalid secure/
auth combination and late completion; map once to stable results and do not double-complete. Add `V09-SMTP-07..12`.
`always()` evidence stores counts/correlation IDs only and deletes run-owned messages. Mutations restore qq.com, ignore
deadline, require auth for Mailpit, expose credential or treat late success as completion; focused test must fail. Run the
Node focused selector and owning integration runner, not full 3C. Preserve `package-lock.json`; any lock change stops.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-06-T2</automated>
</verify>
<acceptance_criteria>
- Real Varify main uses the production SMTP Adapter through EmailDelivery without fake-mode switches.
- Success/failure/deadline are proven by Mailpit SMTP plus API observation; no mock-only claim or public egress is used.
- Logs/JUnit/evidence contain no email credential, verification code or secret-like marker; cleanup failure fails the runner.
</acceptance_criteria>
</task>

</tasks>

## 16. Plan 3C-07 — Four-process real-dependency Integration

Status: **Planned**
Wave: **10**
Depends on: **3C-04, 3C-05, 3C-06 PASS**
Coverage: **G-012, G-013, G-014, G-015; DG-09, DG-11, DG-12, DG-18, DG-19**

<objective>
在一个 RunContext 中以原生 Linux production composition roots 启动 Gate、Status、Chat、Varify，连接同一个
disposable Redis/MySQL/Mailpit topology，完成协议级 ready、共享状态的固定核心 flow、依赖失败恢复与逆序
teardown。只有此计划通过后才可称四进程真实依赖 Integration。
</objective>

<tasks>

<task id="3C-07-T1" type="tdd">
<name>RED：冻结四进程 topology、ready timeline 与 evidence schema</name>
<read_first>
- `tests/integration/RunContext.h` (planned Phase 3B output)
- `tests/integration/process/ProcessHarness.h` (planned Phase 3B output)
- `tests/integration/process/PosixProcessAdapter.h` (planned 3C-01 output)
</read_first>
<files>
- `tests/integration/four-process/FourProcessScenario.h` (planned; create)
- `tests/integration/four-process/four_process_integration_tests.cpp` (planned; create)
- `tests/integration/four-process/topology-schema-v1.json` (planned; create)
- `tests/integration/config/phase3c/gate.ini.template` (planned; create)
- `tests/integration/config/phase3c/status.ini.template` (planned; create)
- `tests/integration/config/phase3c/chat.ini.template` (planned; create)
- `tests/integration/config/phase3c/varify.json.template` (planned; create)
- `cmake/tests/four-process-tests.cmake` (planned; create)
</files>
<action>
Define the scenario Interface `Provision/Start/WaitReady/RunCoreFlow/InjectFault/CollectEvidence/Teardown`. Topology schema
records run-id, production binary/lock identities, loopback endpoints, service digest/mapped-port identities, config
template hashes, schema/migration version, child process identities, ready timestamps and cleanup results; it rejects
credential values and absolute runner paths. Start order is dependency health → SchemaMigration → Varify → Status → Chat →
Gate. Ready is real Mailpit/Redis/MySQL health plus Varify/Status gRPC deadline probes, Chat legal TCP frame exchange and
Gate HTTP probe; PID/log substring/sleep alone is invalid. Add `T10-4PROC-01..08` RED tests for each missing process,
wrong shared namespace, false ready, fixed port, configuration leakage, deadline absence and teardown order. Tests must fail
if any EXE uses in-memory/fake dependencies.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-07-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Topology proves four production composition roots and real disposable dependencies under one RunContext.
- Every process/dependency has a bounded protocol-level ready result and identity-scoped cleanup owner.
- Templates contain references/placeholders only; generated values are protected and redacted from evidence.
</acceptance_criteria>
</task>

<task id="3C-07-T2" type="auto" tdd="true">
<name>GREEN：执行 shared-state core flow 与 commit-visible evidence</name>
<read_first>
- `tests/integration/four-process/FourProcessScenario.h` (planned output of 3C-07-T1)
- `tests/integration/fixtures/phase3c-synthetic-v1.json` (planned 3C-02 output)
- `shared/messages/MessageCommit.h` (planned 3C-05 output)
</read_first>
<files>
- `tests/integration/four-process/FourProcessScenario.cpp` (planned; create)
- `tests/integration/four-process/four_process_integration_tests.cpp` (planned; modify)
- `tests/integration/four-process/README.md` (planned; create)
- `cmake/tests/four-process-tests.cmake` (planned; modify)
</files>
<action>
Implement `T10-4PROC-09..14`: boot the exact binaries from the preflight build, run migrations, request a verification
code through Gate→Varify→Mailpit observation, register/login through Gate, receive Status assignment/token, connect and
authenticate to Chat, then commit one synthetic message and verify its MySQL `message_id`, UUID mapping and Redis route/
session through public behavior/evidence queries. The test driver uses production wire clients/3B transport helpers, not
private handlers or copied parsers. All application configs point at the same run-id service namespaces and dynamic ports.
No step can pass on Adapter-only state; each correlation must traverse the expected process. Preserve bounded logs before
teardown and scan evidence for secret markers.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-07-T2</automated>
</verify>
<acceptance_criteria>
- Gate, Status, Chat and Varify all reach protocol-ready and participate in one correlated synthetic flow.
- Shared Redis/MySQL/Mailpit outcomes are attributable to the same run-id without direct private-state inspection.
- Committed message evidence includes stable server ID/client UUID mapping and no claim of network exactly-once.
</acceptance_criteria>
</task>

<task id="3C-07-T3" type="auto" tdd="true">
<name>Fault/mutation：验证 dependency recovery、逆序 teardown 与 owning runner</name>
<read_first>
- `tests/integration/four-process/FourProcessScenario.cpp` (planned output of 3C-07-T2)
- `tests/integration/faults/redis-fault-schedule.json` (planned 3C-04 output)
- `tests/integration/process/ProcessHarness.h` (planned Phase 3B output)
</read_first>
<files>
- `tests/integration/four-process/four_process_integration_tests.cpp` (planned; modify)
- `tests/integration/four-process/fault-schedule-v1.json` (planned; create)
- `tests/integration/four-process/README.md` (planned; modify)
</files>
<action>
Add `T10-4PROC-15..18`: refuse one application bind, terminate/restart the owned Redis service, make Mailpit unavailable,
and interrupt an in-flight app operation. Assert bounded fail-closed behavior, no false ready, recovery only after healthy
reconnection, and no duplicate MessageCommit row/ID. Teardown order is client handles → Chat → Gate → Status → Varify →
application temp/config/log resources → run-id MySQL/Redis/Mailpit state; service containers remain job-owned and are
destroyed last by GitHub. Preserve primary and cleanup failures independently. Mutations accept stale route, omit a stop/
join, reuse a port or swallow cleanup failure; focused tests must fail. Run the focused cases, then the owning
`phase3c-four-process` label. Do not run the complete 3C lane here.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-07-T3</automated>
</verify>
<acceptance_criteria>
- Failure recovery is bounded, fail-closed and uses healthy production Adapter connections.
- No fault creates duplicate committed rows or a second message ID for the same sender/UUID.
- Every child/thread/socket/port/temp/database/key/mail resource is released or produces a gate-failing cleanup entry.
- `linux_four_process.xml`, topology and teardown evidence are uploaded without secrets on success and failure.
</acceptance_criteria>
</task>

</tasks>

## 17. Plan 3C-08 — N/N-1 artifact, client, service and schema compatibility

Status: **Planned**
Wave: **11**
Depends on: **3C-07 PASS**
Coverage: **G-017; DG-13, DG-15, DG-21**

<objective>
建立只消费不可变已晋升产物的 ArtifactResolver、ManifestVerifier、SchemaFixtureResolver 与兼容矩阵，覆盖
N-1 client→N server、N client→N-1 server、N/N-1 service RPC 和 N-1 schema/data→N。首个 release 尚无
不可变 N-1 时，稳定输出 bootstrap blocker；descriptor/wire fixtures 继续运行但绝不替代实际进程证据。
</objective>

<tasks>

<task id="3C-08-T1" type="tdd">
<name>RED：冻结 immutable artifact resolver、manifest verifier 与 matrix result Interface</name>
<read_first>
- `tests/plans/PHASE-2.5-07-SUMMARY.md`
- `tests/server/protocol/fixtures` (existing descriptor/wire baseline directory)
- `scripts/protocol-compatibility.js`
</read_first>
<files>
- `tests/compatibility/ArtifactResolver.h` (planned; create)
- `tests/compatibility/ManifestVerifier.h` (planned; create)
- `tests/compatibility/SchemaFixtureResolver.h` (planned; create)
- `tests/compatibility/compatibility-matrix-v1.json` (planned; create)
- `tests/compatibility/baselines/n-minus-1.json` (planned; create)
- `tests/compatibility/compatibility_contract_tests.cpp` (planned; create)
- `cmake/tests/compatibility-tests.cmake` (planned; create)
</files>
<action>
Define resolver Interfaces that accept only a durable promoted Release asset identity `{version, tag, artifact_id_or_asset,
external_digest, internal_manifest_digest}` and return verified relative paths; reject Actions cache, mutable workflow name,
source checkout rebuild, missing digest, overwrite identity, path traversal and manifest/file hash mismatch. Define matrix
cells `n_minus_1_client_to_n_server`, `n_client_to_n_minus_1_server`, `n_to_n_minus_1_service_rpc`,
`n_minus_1_to_n_service_rpc`, `n_minus_1_schema_to_n`. Results are exactly `SUPPORTED_PASS`,
`SUPPORTED_FAIL`, `UNSUPPORTED_FAIL_FAST`, or `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`; neither unsupported nor bootstrap is PASS.
The N client is a planned `chat_compat_client` linked to production Qt transport/session/model Modules, not copied protocol
logic or GUI pixel automation. Add `T10-COMPAT-01..08` RED cases for every invalid artifact identity, matrix omission,
static-fixture substitution and bootstrap-as-pass attempt.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-08-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Resolver cannot obtain N-1 by rebuilding old source or restoring a dependency cache.
- Every required client/service/schema cell has explicit input identities, deadline and one stable non-ambiguous status.
- Static Phase 2.5 descriptor/wire results are separately labeled and cannot satisfy program/schema cells.
</acceptance_criteria>
</task>

<task id="3C-08-T2" type="auto" tdd="true">
<name>GREEN/mutation：运行实际矩阵或产生不可伪装的 bootstrap blocker</name>
<read_first>
- `tests/compatibility/ArtifactResolver.h` (planned output of 3C-08-T1)
- `tests/compatibility/ManifestVerifier.h` (planned output of 3C-08-T1)
- `tests/compatibility/SchemaFixtureResolver.h` (planned output of 3C-08-T1)
</read_first>
<files>
- `tests/compatibility/ArtifactResolver.cpp` (planned; create)
- `tests/compatibility/ManifestVerifier.cpp` (planned; create)
- `tests/compatibility/SchemaFixtureResolver.cpp` (planned; create)
- `tests/compatibility/compatibility_matrix_tests.cpp` (planned; create)
- `tests/compatibility/chat_compat_client.cpp` (planned; create)
- `tests/compatibility/compatibility-matrix-v1.json` (planned; modify)
- `tests/compatibility/README.md` (planned; create)
- `cmake/tests/compatibility-tests.cmake` (planned; modify)
</files>
<action>
Implement `T10-COMPAT-09..18`. Always run canonical descriptor/wire compatibility first. If the baseline pointer contains
a verified promoted N-1 artifact, download without source rebuild, verify external and every internal hash, extract into a
run-owned directory with path checks, restore its versioned synthetic schema/data, and run every matrix cell as actual
processes using dynamic endpoints and protocol-ready probes. Supported cells must preserve documented request/response,
unknown-field/error and data invariants; unsupported combinations must terminate before mutation with stable version
diagnostics and no silent write. Rollback runs only for combinations explicitly marked supported. If no promoted N-1
exists, populate every N-1 cell with `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`, include the required future artifact/schema
identity fields, leave G-017 open, and allow no `pass/skipped-success` aggregate. Mutations substitute a rebuilt binary,
change a digest, silently parse an unsupported message, permit a data write before fail-fast or label bootstrap PASS; the
runner must fail. Then run the owning compatibility runner, not the full 3C lane.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-08-T2</automated>
</verify>
<acceptance_criteria>
- With a baseline, actual immutable N/N-1 client/server/service/schema processes run and each cell has bounded evidence.
- Without a baseline, the report is explicitly bootstrap/non-PASS, G-017 remains open and no old source/static fixture impersonates N-1.
- Unsupported combinations fail before data mutation with stable diagnostics; supported rollback is the only rollback executed.
- Extracted artifacts, processes, ports, schema and temp directories are identity-cleaned and ledgered.
</acceptance_criteria>
</task>

</tasks>

## 18. Plan 3C-09 — Hosted Ubuntu master gate closeout

Status: **Planned**
Wave: **12**
Depends on: **3C-08 applicable matrix PASS or valid bootstrap blocker**
Coverage: **G-012, G-013, G-014, G-015, G-017; DG-09, DG-10, DG-11, DG-18, DG-23**

<objective>
把同源 Linux build、disposable services、migration、真实 Adapters、四进程与适用 compatibility 接入一个
GitHub-hosted Ubuntu `master` Required gate；按实际注册更新 manifest，并只运行一次完整 Phase 3C lane。
现有 Windows `develop` 四个稳定 check 名称与 12-report/180-case baseline 必须完整继承。
</objective>

<tasks>

<task id="3C-09-T1" type="tdd">
<name>RED/GREEN：注册 master workflow、report manifest 与结构门禁</name>
<read_first>
- `.github/workflows/linux-ci.yml` (planned outputs of 3C-00/02)
- `.github/workflows/windows-ci.yml`
- `scripts/windows-local.ps1`
</read_first>
<files>
- `.github/workflows/linux-ci.yml` (planned; modify)
- `scripts/linux-ci.sh` (planned output of 3C-00; modify)
- `tests/manifests/phase3c-reports.json` (planned; create)
- `scripts/windows-local.ps1` (existing; modify)
- `tests/CI-GOVERNANCE.md` (existing; modify after evidence)
- `tests/REGRESSION.md` (existing; modify after evidence)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; modify planned ownership/evidence only)
</files>
<action>
Create the planned stable master check `Linux real dependencies and compatibility` on a pinned GitHub-hosted Ubuntu
label. The job runs application binaries natively, declares only digest-pinned Redis/MySQL/Mailpit containers, executes the
3C-00 hard preflight before any integration selector, and uses one build directory/run-id for the lane. `scripts/linux-ci.sh`
is the public orchestration Interface: `--phase 3C --configuration Release --junit-dir <owned>` performs preflight,
service health, migration, Redis, MessageCommit/MySQL, SMTP, four-process and compatibility/bootstrap selectors in the
dependency order above, with a job hard timeout and no automatic retry. The manifest lists Test IDs, actual registered
runner cases, report paths, level, owner and timeout; values are populated from real registration, not the planned range
sizes. Extend `CheckTestStructure` with `T10-CLOSE-01..06` RED checks for missing/duplicate ID, missing target/report,
production source duplication, wrong lane, floating action/image, `continue-on-error`, absent `always()` upload/cleanup,
secret marker and baseline/check-name drift. Keep the exact four existing Windows check names unchanged and require them on
master in addition to the new Linux check.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release --selector 3C-09-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- One master workflow/check owns the complete 3C lane on hosted Ubuntu; no self-hosted/personal runner or application image exists.
- Manifest derives exact counts from real tests and distinguishes inherited 12/180 from new report families.
- Missing report, unavailable service/runner, timeout, failure or cleanup failure cannot be skipped/retried/continued green.
- Gaps remain planned until actual master evidence is accepted; bootstrap does not close G-017.
</acceptance_criteria>
</task>

<task id="3C-09-T2" type="auto">
<name>Mutation/full-lane：执行唯一一次完整 3C gate 并封存 evidence</name>
<read_first>
- `scripts/linux-ci.sh` (planned output of 3C-09-T1)
- `tests/manifests/phase3c-reports.json` (planned output of 3C-09-T1)
- `.github/workflows/linux-ci.yml` (planned output of 3C-09-T1)
</read_first>
<files>
- `tests/manifests/phase3c-reports.json` (planned; update with actual counts)
- `tests/CI-GOVERNANCE.md` (existing; update evidence/status only)
- `tests/REGRESSION.md` (existing; update actual commands/counts only)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; update actual evidence without pre-closing unsupported gaps)
- `tests/plans/PHASE-3C-SUMMARY.md` (planned; create only after execution evidence)
</files>
<action>
Before the only full lane run, execute focused mutations for: missing Linux target, floating service digest, migration checksum
drift, Redis timeout/eviction, MessageCommit unique/rollback/sender, SMTP public endpoint/deadline, false process ready,
artifact digest/bootstrap-as-pass and teardown failure. Restore each mutation and confirm its owning selector GREEN. Then
invoke the complete 3C public runner exactly once; do not pre-run it as a rehearsal. The job must also observe the existing
four Windows Required checks for the same master candidate SHA. `always()` uploads all declared JUnit/audit/topology/
teardown evidence with `if-no-files-found: error`, scans it for synthetic/real secret markers, and audits resource release.
Update manifest/matrix/governance/Regression/Summary only from the resulting actual report counts and remote check URLs/
identities. Record N-1 cells as actual PASS/FAIL when baseline exists or exact bootstrap blocker when absent. If the full
lane fails, preserve evidence and return to the owning plan; do not rerun to obtain green.
The closeout upload is exactly `phase3c-gate-evidence` and contains `phase3c-reports.json`, its listed `junit/*.xml`,
`teardown.json`, and `redaction.json`; the manifest records workflow name/run ID/attempt/head SHA, the artifact API digest,
every JUnit relative path and actual testcase count, plus the inherited baseline `{reports:12,testcases:180}`.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3C --configuration Release</automated>
</verify>
<acceptance_criteria>
- Exactly one full 3C lane run is the phase closeout evidence; focused runs/mutations are separately identified.
- Same SHA has all four existing Windows checks and the new hosted Ubuntu master check; no missing/unavailable check is waived.
- Actual manifest/report totals reconcile without changing the inherited 12/180 current baseline.
- Summary records all tool/image/action identities, schema/checksums, topology, compatibility/bootstrap status and cleanup evidence without secrets.
</acceptance_criteria>
</task>

</tasks>

## 19. RED → GREEN → mutation → runner 验证闭环

1. **RED：** 每个新增或修改 observable contract 的 task 先创建非空 contract test 并运行最小 selector；纯文档、
   registration 或无行为 plumbing 只做 focused static verification。失败不能来自下载抖动、固定 sleep、个人服务或空占位测试。
2. **GREEN：** 只实现使同一 production Interface 通过所需的 production Adapter/Module 和 composition
   wiring；测试与正式 caller 链接同一 target，不读取 private container。
3. **Mutation：** 只对 action 明确列出的身份、deadline、transaction、unique、checksum、redaction、ready、hash 或
   cleanup 等重要行为执行；必须使 focused selector 非零并在所属 public runner 前恢复。prose、registration 和无行为
   plumbing 不增加 mutation。
4. **Owning runner：** 3C-00..08 只运行其 CTest label/Node selector/compat runner；不在每个 plan 重跑完整 lane。
5. **Phase lane：** 3C-09 先完成所有 focused mutation evidence，再且只再运行一次
   `scripts/linux-ci.sh --phase 3C --configuration Release`。失败不自动重试，回到最早 owning plan 修复。
6. **Evidence reconciliation：** stable Test ID、真实 runner case、JUnit、Module README、manifest 与矩阵逐项对应；
   planned range 大小永不作为实际 count。继承 baseline 单独保持 12 reports/180 testcases。

## 20. Threat model

### Trust boundaries

| Boundary | Untrusted input / asset | Mandatory control and evidence |
| --- | --- | --- |
| GitHub workflow → tool/action/image | action ref、runner image、container image、dependency download | pinned action SHA、Ubuntu label、image tag+digest、vcpkg/npm lock；identity manifest |
| service container → native app | Redis/MySQL/SMTP bytes、health/ports、disconnects | loopback mapped ports、protocol health、deadlines、validation、owned fault schedule |
| config/env → four production processes | endpoint、port、timeout、credential reference | typed range validation before listen、protected env/stdin、redacted templates/logs |
| client/session → MessageCommit | payload sender、UUID、chat/recipient/content、retry | authenticated principal ownership、UUID/content bounds、membership、unique key、explicit transaction |
| migration/fixture → MySQL | SQL/checksum/version、legacy data、rollback instruction | immutable ordered manifest、prepared fixture writes、backup/forward recovery、invariant audit |
| N-1 artifact → matrix runner | archive paths、manifest/hash、binary/schema identity | durable promoted source only、external+internal hashes、safe extraction、fail-fast version checks |
| process/test → report store | stdout/stderr、JUnit、topology、cleanup evidence | bounded collection、allowlisted fields、secret scan、relative paths、primary+cleanup results |

### STRIDE register

| Threat ID | Category | Target | Disposition | Required mitigation / evidence |
| --- | --- | --- | --- | --- |
| T-3C-01 | Spoofing / Elevation | MessageCommit sender | mitigate | sender only from authenticated session; mismatched `from_uid` RED; membership check before SQL |
| T-3C-02 | Tampering / Repudiation | UUID replay/race | mitigate | DB `UNIQUE(send_id, client_msg_uuid)`, equal payload→same ID, mismatch→Conflict, concurrency evidence |
| T-3C-03 | Tampering | migration/fixture | mitigate | ordered SHA-256 ledger, immutable artifact digest, version/invariant audit; drift blocks startup |
| T-3C-04 | Tampering / Data loss | transaction | mitigate | whole-batch explicit begin/commit/rollback; mid-batch conflict proves zero partial rows |
| T-3C-05 | Information disclosure | config/log/report/artifact | mitigate | synthetic-only, protected credential reference, redaction scan, bounded evidence, no absolute paths |
| T-3C-06 | Denial of service | Redis/MySQL/SMTP/process pools | mitigate | finite connect/borrow/command/ready/stop deadlines, bad connection eviction, joined workers |
| T-3C-07 | Spoofing / DoS | stale Redis route/session | mitigate | TTL, fail-closed miss, disconnect/restart recovery and run-prefix cleanup tests |
| T-3C-08 | Tampering / Repudiation | N-1 artifact | mitigate | promoted immutable asset only, external/internal digest, no cache/source rebuild/overwrite |
| T-3C-09 | Tampering / Data loss | unsupported N/N-1 | mitigate | actual process negotiation; unsupported fail-fast before writes; bootstrap cannot pass |
| T-3C-10 | Elevation / Tampering | test seam | mitigate | one production Interface, two justified Adapters, no fake EXE/test macro/private access/copied parser |
| T-3C-11 | Denial of service / Tampering | teardown | mitigate | run identity, reverse ledger, stale-PID protection, primary+cleanup failure propagation |
| T-3C-12 | Supply chain | npm/vcpkg/actions/images | mitigate | retain locks, pinned SHAs/digests; any npm lock/dependency change stops for legitimacy review |

本 threat model 不宣称完整 ASVS compliance，也不将 loopback/plaintext CI 证据解释为公网 TLS/password 安全认证。

## 21. Stop conditions

出现以下任一情况立即停止当前链，保存已生成的非敏感 evidence，并返回最早 owning plan：

1. Phase 3B completion evidence 缺失，或 production caller/test 不能共用同一 Interface。
2. 3C-00 任一 Gate/Status/Chat/Varify configure/install/compile/link/loader/startup proof 失败；Qt production
   Modules 无法用锁定版本获取/链接；任何修复需要复制算法、删除目标或隐式升级 dependency。
3. workflow 需要 self-hosted/个人 VM、个人凭据、固定开发 endpoint、共享数据库、真实用户数据或公共 SMTP。
4. service/action/toolchain exact pin、credential expression、health、random mapping、deadline、`always()` evidence
   或 identity cleanup 无法证明。
5. schema 只能由 container init/ad-hoc SQL 创建，migration checksum/version/invariant 不可审计，或不可逆
   migration 没有 backup/forward recovery/stop metadata。
6. MessageCommit 信任 payload sender、没有数据库唯一约束、依赖 implicit autocommit、允许 partial batch、同一
   UUID 返回不同 ID、Conflict 静默成功，或通知发生在 commit 之前。
7. 实际测试需要 `clearForTest`、测试宏、fake-mode EXE、private container、第二套 parser/业务算法或 test target
   重复编译 production `.cpp`。
8. N-1 artifact/schema 不存在时任何路径报告 compatibility PASS；存在时 digest/manifest/restore/matrix 任一失败。
9. runner 不可用、自动重试刷绿、timeout、报告缺失、实际 count 不一致、secret scan 命中、cleanup failure，或
   原 12/180 baseline/四个 Windows check 名称发生未评审变化。
10. 四进程证据缺任一 production process/real dependency/protocol-ready/shared-state correlation，却被描述为完整
    Integration；或 Linux CI 被描述为 application Docker/Linux release support。
11. 本机 vcpkg 固定路径缺失/不一致、出现 manifest 自动安装，或任何 package/path/triplet/baseline/tool identity 变更
    未取得 DG-25 的本次明确批准；不得切换目录、自动 restore 或删除重建。

## 22. Phase completion criteria

Phase 3C 只有在下列条件全部满足后才可从 Planned 进入完成评审：

- 3C-00 hard preflight 在权威 hosted Ubuntu 上 PASS，且同源 target/toolchain evidence 完整。
- Windows/Linux EXE、IntegrationHost/tests 的 production target/source ownership structure gate PASS；POSIX process
  Adapter 的 start/ready/stop/kill/reap/cleanup 有界。
- Redis/MySQL/Mailpit 全部 digest-pinned、healthy、随机映射、synthetic/run-id 隔离，failure/cleanup evidence 完整。
- SchemaMigration fresh 0→N PASS；若 promoted N-1 存在则 N-1→N/recovery PASS；若不存在则只接受精确
  `BOOTSTRAP_NO_PROMOTED_N_MINUS_1` 非-PASS 状态，且 G-017 保持 open。
- C++/Node Redis production Adapters、MessageCommit/MySQL、SMTP/Mailpit 的 focused/owning runners 与 mutations
  全部有证据；DG-24 同 sender/UUID 仅一行/同一 message ID，whole-batch 零部分提交。
- Gate/Status/Chat/Varify 四个 native production processes 在同一 RunContext 连接真实 dependencies，完成固定
  shared-state flow、dependency recovery 和逆序 cleanup。
- N/N-1 baseline 存在时实际 client/service/schema matrix PASS；不存在时 bootstrap blocker 不被聚合为 PASS，
  Phase 2.5 static fixtures 仍单独 GREEN。
- 同一 master candidate SHA 的四个原 Windows Required checks 与 planned Linux master check 均有权威结果；完整
  3C lane 只运行一次，无 retry/waiver，所有报告、actual counts、secret scan 和 teardown ledger 对齐。
- 本计划本身不自动关闭 G-012..G-017；只有矩阵按实际 evidence 更新后才可改变对应状态。首发 bootstrap 下
  G-017 必须继续保持未完成，并由 3D/Release 路由继承。

## 23. Artifacts this phase produces

下列均为 **planned artifacts/symbols**，只有实现与验证完成后才成为仓库事实：

- 顶层 same-source `CMakeLists.txt`、`CMakePresets.json`、Linux vcpkg triplet、production target registry 与 pinned
  Ubuntu preflight evidence；不含 application Dockerfile/image。
- planned production target aliases `chat::gate_server_modules`, `chat::status_server_modules`,
  `chat::chat_server_modules`, `chat::client_modules`，以及 POSIX ProcessHarness Adapter。
- `ci/services.lock.json`、DependencyCoordinator、versioned synthetic dataset、mapped endpoint/topology/cleanup schemas。
- SchemaMigration Interface/implementation/CLI、versioned migrations/checksum ledger、fresh fixture 与 real-N-1-or-
  bootstrap metadata。
- MessageCommit Interface、MySqlMessageCommitAdapter、`UNIQUE(send_id, client_msg_uuid)` migration、explicit transaction
  semantics、peer/history/client stable-ID mapping。
- finite hiredis/ioredis production Adapter lifecycle、EmailDelivery Interface、NodemailerSmtpAdapter、
  InMemoryEmailAdapter 与 Mailpit observable evidence。
- four-process scenario driver、redacted config templates、protocol-ready/topology/recovery/teardown reports。
- ArtifactResolver、ManifestVerifier、SchemaFixtureResolver、`chat_compat_client`、N/N-1 matrix schema 与 baseline pointer。
- `scripts/linux-ci.sh`、hosted Ubuntu master workflow/check、actual report manifest、Phase 3C Summary/evidence links。

## 24. Multi-source coverage audit

| Source | Item | Owning plans | Status in this plan |
| --- | --- | --- | --- |
| GOAL | same-source hosted Ubuntu native build/run | 3C-00/01/09 | COVERED |
| GOAL | disposable real dependencies and four-process Integration | 3C-02/04/05/06/07/09 | COVERED |
| GOAL | schema/migration/idempotent persistence | 3C-03/05/08 | COVERED |
| GOAL | actual N/N-1 or honest bootstrap | 3C-03/08/09 | COVERED |
| REQ | G-012 Redis Adapters | 3C-02/04/07/09 | COVERED, planned |
| REQ | G-013 MySQL Adapters/schema | 3C-02/03/05/07/09 | COVERED, planned |
| REQ | G-014 SMTP Adapter | 3C-02/06/07/09 | COVERED, planned |
| REQ | G-015 four-process lifecycle | 3C-00/01/02/07/09 | COVERED, planned |
| REQ | G-016 idempotent persistence prerequisite | 3C-05; Phase 3D route | PARTIAL OWNER BY DESIGN, not complete |
| REQ | G-017 compatibility | 3C-03/08/09; Phase 3D/Release route | COVERED with bootstrap rule, not pre-completed |
| CONTEXT | DG-09..DG-24 applicable 3C decisions | sections 1..22 and plan coverage rows | COVERED; non-3C owners explicitly routed |
| RESEARCH | Linux/toolchain uncertainty | 3C-00 hard stop | COVERED |
| RESEARCH | credential expression uncertainty | 3C-02 proof/stop | COVERED |
| RESEARCH | schema absent / transaction defect | 3C-03/05 | COVERED |
| RESEARCH | no immutable N-1 baseline | 3C-03/08 bootstrap blocker | COVERED without fabrication |
| RESEARCH | auth/TLS release claim risk | non-goals, threat model, stop conditions | COVERED without scope expansion |

## 25. 下一阶段路由：Phase 3D

Phase 3D 只可在本节 completion review 接受后执行，并必须读取本阶段 Summary、actual manifest、schema/migration
audit、MessageCommit contract、four-process topology 和 compatibility/bootstrap report。3D 复用同一 production
Modules、real Adapters、RunContext/ProcessHarness 与 hosted Ubuntu dependency topology，扩展为两台 ChatServer、
两个 production-module clients 的跨实例业务 E2E；不得创建平行 client parser、fake server 或静态 fixture
捷径。

若 Phase 3C 在首发 bootstrap 状态完成，3D 只运行 N 版本双实例业务流并继续输出同一
`BOOTSTRAP_NO_PROMOTED_N_MINUS_1`；不得把 G-017 标 complete。真实 N-1 只有 Release gate 首次原样晋升后才
建立，后续周期再启用完整 N/N-1 client/service/schema 矩阵。
