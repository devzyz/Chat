---
document: tests/plans/PHASE-3D-PLAN.md
phase: 3D
title: Two-ChatServer cross-instance business E2E and N/N-1 business compatibility
status: Planned
plan_ids: [3D-00, 3D-01, 3D-02, 3D-03, 3D-04, 3D-05]
plan_count: 6
wave_range: [13, 18]
depends_on:
  - "Phase 3C complete with accepted hosted-Ubuntu, real-Adapter, schema, MessageCommit, four-process, and compatibility/bootstrap evidence"
requirements: [G-016, G-017]
locked_decisions: [DG-09, DG-10, DG-11, DG-12, DG-13, DG-14, DG-15, DG-16, DG-17, DG-18, DG-19, DG-20, DG-21, DG-22, DG-23, DG-24, DG-25]
execution_policy:
  vertical_cycle: "RED -> GREEN -> mutation -> owning public runner"
  full_phase_lane_runs: 1
  final_testcase_total: "derive from actual registration and emitted reports; never predeclare"
  flaky_policy: "Required failures remain blocking; quarantine and retry never substitute for the gate"
autonomous: true
must_haves:
  truths:
    - "Two native ChatServer processes with distinct logical identities are ready at distinct endpoints, and two isolated production-module client processes remain connected to different ChatServers throughout the fixed current-version journey."
    - "A versioned production-shaped synthetic dataset with a fixed seed and fixed logical topology drives verification, registration, login/discovery, Chat-token login, friendship, cross-instance private chat, reconnect, history, ordering, and deduplication."
    - "A committed private message is persisted once in MySQL, routed through Redis to the peer ChatServer, and observed by both clients with the same server message_id for the same authenticated sender/msg_uuid."
    - "Client retry, reconnect, notification/history replay, and pagination preserve one durable row and one stable message_id without claiming network exactly-once."
    - "When an immutable promoted N-1 artifact/schema exists, real N/N-1 clients, services, peer RPC, and migrated-schema business flows run; otherwise every N-1 cell reports the exact non-PASS bootstrap blocker."
    - "The hosted-Ubuntu master/release gate preserves all earlier Required checks, rejects missing reports/timeouts/cleanup failures, and hands the accepted SHA to the separate build-once Release gate."
  artifacts:
    - "planned tests/plans/PHASE-3D-TEST-PLAN.md"
    - "planned versioned Phase 3D dataset, topology schema, scenario Module, production-module client driver, and deterministic fault schedules"
    - "planned cross-instance journey, messaging, recovery/history, and N/N-1 business-flow E2E tests"
    - "planned hosted-Ubuntu Phase 3D report manifest, master/release workflow registration, topology evidence, and teardown ledger"
  key_links:
    - "chat GUI and chat_e2e_client process -> one ClientSessionRuntime Interface -> production GateHttpTransport/ChatTcpTransport/AuthFlowCoordinator/ClientSession/MessageModelStore targets"
    - "client A -> ChatServer A -> Redis route lookup -> ChatServer B peer RPC -> client B"
    - "authenticated sender + msg_uuid -> MessageCommit -> MySqlMessageCommitAdapter -> UNIQUE(send_id, client_msg_uuid) -> one message_id"
    - "immutable N-1 manifest/digests -> actual client/service/schema processes -> business-flow compatibility matrix"
    - "stable planned Test ID -> real registered case -> JUnit/evidence -> scripts/linux-ci.sh Phase 3D selector -> hosted-Ubuntu master/release Required check"
---

# Phase 3D 正式执行计划

状态：**Planned（尚未执行）**

本文件完整展开 3D-00..3D-05。所有标为 planned 的文件、symbol、Test ID range、report family 与 CI check
都不是当前仓库事实；只有真实 source/target/runner/JUnit 注册完成后，才可把实际 testcase/report 数写入 manifest。
当前继承的 12-report/180-testcase baseline（Phase 2.5 的 173 case 加 3A-01 的 7 case）不可减少；Phase 3A、3B、3C 落地后的实际新增数量必须从各自
Summary 和 runner manifest 读取，本计划不猜测最终总数。

## 1. Hard prerequisite 与执行授权

3D-00 开始前必须同时存在并接受以下证据，否则立即停止：

1. Phase 3C completion review 与 `PHASE-3C-SUMMARY.md` 已证明 hosted Ubuntu 同源 build、Redis/MySQL/Mailpit、
   SchemaMigration、MessageCommit、四进程 topology、POSIX ProcessHarness 和完整 identity-scoped cleanup；
2. Gate、Status、Chat、Varify 正式 composition roots 与测试链接同一 production Module/Adapter targets，Linux 未复制
   Windows parser、协议或业务算法（DG-18、DG-19）；
3. current-N `MessageCommit` 已兑现 authenticated sender、whole-batch explicit transaction、
   `UNIQUE(send_id, client_msg_uuid)`、Created/Existing/Conflict 和 peer/history/client stable-ID mapping（DG-24）；
4. Phase 3C report manifest、schema/checksum audit、service-image/action/tool identities与 compatibility report 可审计；
   无 promoted N-1 时，状态必须是精确 `BOOTSTRAP_NO_PROMOTED_N_MINUS_1`，不能是 PASS；
5. 四个既有 Windows `develop` Required check 与 Phase 3C hosted-Ubuntu master check 均保持权威、未被 flaky
   quarantine、retry 或 waiver 取代。

若上游 planned path/symbol 与实际 Summary 不同，3D-00 只记录一一映射并复用真实 Interface；不得为匹配本文件命名
再建同义 facade、第二 parser、第二客户端状态机或 fake server。

DG-25 继续生效：本机 `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 默认只读，本地任何
preflight/build/test 都必须禁用 manifest 自动安装、使用固定 installed tree 并在缺失/不一致时 fail closed。3D 的
hosted Ubuntu 证据只允许 workflow 在 run-owned 临时 root 中按已批准 locks 恢复；任何本机 package 修改，或本机/CI
install root、manifest、baseline、triplet、tool identity 变化，都须先展示精确命令/目标/影响并取得用户明确批准。

比例化执行以 [`tests/CI-GOVERNANCE.md` section 2.1](../CI-GOVERNANCE.md#21-比例化执行合同) 为唯一过程权威。
Phase entry 一次性读取 Phase 3C Summary、DG-09..DG-25、G-016/G-017、当前 baseline、hosted-Ubuntu 与 dependency/threat
prerequisites；task 的 `read_first` 只保留 edited source、closest analog 和一个必要 authority。3D-00-T1 将 3D task selectors
注册到 Phase 3C 已公开的 `scripts/linux-ci.sh`：`--selector <task-id>` 负责 focused RED/GREEN、meaningful failure/lifecycle/
compatibility mutation、真实 topology Integration 与创建者 cleanup，并以确定性非零传播失败；每 plan 最后一个 task
只运行一次 owning selector。3D-05-T2 独占无 selector 的完整 3D lane 与 aggregate report/secret/residue/diff/remote evidence；
3D-05-T3 只验证同一 SHA 的 release admission，不重复 full lane。

## 2. 目标

- 依据 G-016、DG-12、DG-16，在一个 run-owned topology 中原生启动 Gate、Status、Varify、两个不同 identity 的
  ChatServer 和两个隔离的 production-module client processes，固定完成验证码→注册→Gate 登录/服务发现→
  Chat Token 登录→好友申请/接受→跨实例私聊→断线重登→历史分页/顺序/去重。
- 依据 DG-24，证明同一 authenticated sender 与 `msg_uuid` 在 ACK 丢失、断线、通知/历史重复到达时仍只产生一条
  MySQL row 与同一 `message_id`；客户端保留原 UUID bounded retry，并按 server ID 去重。结论明确是
  **at-least-once delivery attempts + idempotent persistence**，不宣称网络 exactly-once。
- 依据 G-017、DG-13、DG-15、DG-21，在 immutable promoted N-1 artifact/schema 存在时运行真实 N/N-1 client、
  full-service、Chat peer RPC 与 N-1 schema→N 后的业务流；首发没有基线时保留不可伪装的 bootstrap blocker。
- 依据 DG-11、DG-20、DG-23 和治理 D-02/D-06/D-09，把固定双实例 E2E 与适用 compatibility 接入
  GitHub-hosted Ubuntu `master/release` gate；runner/依赖不可用、timeout、report missing、secret finding 或 cleanup
  failure 全部阻断。

## 3. 明确非目标

- 不制作 Gate、Status、Chat、Varify application Docker image，不设计生产容器编排，不宣称 Linux release support
  （DG-10、DG-18）。Docker 只承载 Phase 3C 已锁定的 disposable dependency services。
- 不以单 ChatServer、两个客户端连同一 ChatServer、in-memory/fake dependency、Adapter-only probe、GUI 像素自动化
  或 private handler 调用替代 DG-16 拓扑和公开业务流。
- 不扩展到 N-2，不从旧源码重建 N-1，不用 Actions cache、descriptor/wire fixture 或 mutable artifact 冒充跨版本
  进程证据（DG-13、DG-21）。
- 不把更大 mesh、高并发、随机 fuzz、负载或长 soak 放入 Required lane；它们只进入 scheduled/explicit lane，
  Required 仍覆盖本计划列出的确定性 fault/mutation（DG-16、DG-20）。
- 不借 E2E 重写认证、密码或 TLS 产品合同，不把 loopback/hosted-runner 结果解释为公网 production-security 认证。
- 不在 3D 构建、签署或晋升 Windows release set；统一 artifact smoke、人工 UAT 和 promotion 由 Release gate
  R-00..R-03 独立拥有（DG-17、DG-22）。

## 4. Locked decisions / Gap coverage

| Contract | 3D 不可弱化的执行含义 | Owning plans |
| --- | --- | --- |
| DG-09 | 3D 只拥有双 ChatServer/双客户端业务 E2E；release build/UAT/promotion 不提前 | 3D-00..05；Release route |
| DG-10 / DG-18 / DG-23 | hosted Ubuntu 原生执行、同源 production Modules、无 application images、无 self-hosted/个人环境证据 | 3D-00/05 |
| DG-11 / DG-12 | disposable real dependencies、production-shaped synthetic data、run-id 隔离、deadline、reports 与 cleanup failure gate | 3D-00..05 |
| DG-13 / DG-15 / DG-21 | promoted N-1 存在时真实 client/service/schema matrix；不存在时精确 bootstrap non-PASS | 3D-04/05 |
| DG-14 / DG-19 | 两个 client drivers 和 servers 穿过 3A/3B/3C production Interface/transport/Adapter targets；无 fake EXE/test seam | 3D-00/01/02/03 |
| DG-16 | 两个不同 ChatServer、两个分别连接不同 server 的客户端和完整固定业务旅程 | 3D-00..05 |
| DG-17 / DG-22 | 3D 只输出被 Release gate 消费的 accepted SHA/evidence；build-once、同 artifact UAT 与原样晋升由 R-00..03 完成 | 3D-05；Release route |
| DG-20 | ACK loss、disconnect/restart、deadline、late/duplicate delivery、route recovery、resource release 均确定性且 mutation-proven | 3D-02/03/05 |
| DG-24 | `(sender,msg_uuid)` 唯一、same `message_id`、server-ID order、client dedupe、明确不声称 exactly-once | 3D-02/03 |
| DG-25 | 本机 vcpkg 持久目录不可变；本地 runner fail closed 且无隐式安装；hosted runner 只使用 run-owned 临时 root，变更逐次审批 | 3D-00/05 |
| G-016 | current-N 双实例认证/好友/私聊/重连/历史公开 E2E | 3D-00..03/05 |
| G-017 | N/N-1 clients/services/peer RPC/schema 的实际业务 flow 或真实 bootstrap blocker | 3D-04/05 |

## 5. Production Module / Interface / Adapter seam

以下路径与名称为 planned target state。执行者先用 Phase 3C Summary 映射真实 upstream target；若等价 Interface 已存在，
直接复用并在 `PHASE-3D-TEST-PLAN.md` 记录映射，禁止叠加 wrapper。

| Module | 唯一 Interface | Implementation 隐藏内容 | Adapter / seam |
| --- | --- | --- | --- |
| planned `client::ClientSessionRuntime` | `Start(ClientRuntimeSpec)`、`Execute(ClientCommand, Deadline) -> ClientResult`、`Snapshot(ChatId) -> ConversationSnapshot`、`Stop(Deadline)` | Gate/Auth/Chat flow generation、token/session state、pending UUID retry、reconnect generation、history cursor、model dedupe、Qt object ownership | GUI dialogs/windows 作为 production caller Adapter；planned `JsonLineClientDriverAdapter` 作为 E2E process caller Adapter；两者调用同一 runtime，均不访问 internals |
| planned `e2e::ProductionClientDriver` | `Start(ClientProcessSpec)`、`Execute(Command, Deadline)`、`WaitEvent(EventPredicate, Deadline)`、`Stop(Deadline)` | child process/control-pipe framing、correlation、bounded stdout/stderr、safe event projection | 使用 3C `ProcessHarness`/POSIX Adapter；child `chat_e2e_client` 链接 production client Modules，不复制 HTTP/TCP/proto/model logic |
| planned `e2e::Phase3DScenario` | `Provision`、`StartTopology`、`RunJourney`、`InjectFault`、`CollectEvidence`、`Teardown` | start order、logical identities、correlations、step ledger、fault scheduling、reverse cleanup | 3C RunContext/DependencyCoordinator/SchemaMigration/ProcessHarness 是 production-shaped infrastructure dependencies；没有 fake app Adapter |
| planned `e2e::ExternalStateEvidence` | `ObserveRedisRoute`、`ObserveMessageRow`、`ObserveSchemaVersion`、`ObserveMail` 返回 allowlisted evidence | read-only Redis/MySQL/Mailpit queries、hashing、redaction、connection lifetime | 真实 disposable Redis/MySQL/Mailpit evidence Adapters；只验证外部可见事实，不复制业务决策或写生产状态 |
| 3C `CompatibilityMatrix` | `Resolve/Verify/RunMatrix` 与稳定 cell status | artifact download/extraction、digest、process launch、schema restore | promoted Release asset Adapter；Phase 2.5 static fixture 仍是独立 baseline，不能满足 process cell |
| planned `e2e::FrameFaultRelay` | `Start(upstream, FaultSchedule)`、`Endpoint()`、`Stop()` | loopback forwarding、generation identity、指定 production-frame boundary 的 drop/close、bounded buffers | 正常旅程直连 ChatServer；仅 deterministic ACK-loss/replay cases 使用 relay。relay 复用 production `ChatFrameCodec` target，不复制 parser，不进入正式 EXE |

`ClientSessionRuntime` 是生产 deep Module：GUI 和测试只学习 command/result/snapshot/lifecycle；transport、retry、dedupe 和
状态转换留在 Implementation 内，形成 leverage/locality。`chat_e2e_client` 是测试 composition root，不是第二客户端实现，
不增加 `chat.exe --test-mode`、测试宏、公开 socket/model container 或 private reset hook。

## 6. Fixed dataset、topology、deadline 与 evidence contract

### 6.1 Versioned production-shaped synthetic dataset

planned `tests/integration/fixtures/phase3d-synthetic.json` 与 schema 必须固定：

- dataset identity `phase3d-2026-08-30`、固定 seed `0x3D20260830`、固定 logical actors `alice`/`bob`、
  fixed ChatServer names `chat-e2e-a`/`chat-e2e-b` 和固定 journey step order；
- 运行时仅用 run-id 给 DB/schema、Redis key、Mailpit recipient、ports、temp paths 与 usernames 添加 namespace；
  logical topology、seed、message template、friend graph 与预期顺序不随重跑改变；
- 覆盖 ASCII/Unicode/emoji、最大合法用户名与 message length、重复注册/好友请求、self/unknown/non-friend 关系、
  canonical/invalid UUID、冲突 payload，以及至少 `2 * production_page_size + 1` 条通过公开 send flow 创建的 history，
  从而必然跨越三页和页边界；
- repository 只保存 credential/code 的约束与 runtime reference，不保存 password/token/verification code 值。
  `SyntheticCredentialFactory` 在 run-owned memory 中生成合规值，Mailpit code 只在内存中读取并立即 redaction；
- fixture 禁止真实用户、真实邮箱、复制生产 dump、固定开发 endpoint、个人 credential 或 public SMTP。

### 6.2 Fixed runtime topology

```text
digest-pinned Redis + MySQL + Mailpit (job-owned containers)
                 |
         SchemaMigration current N
                 |
       Varify N + Status N + Gate N
                 |
       +---------+---------+
       |                   |
 ChatServer A N <---peer---> ChatServer B N
   TCP/RPC A                 TCP/RPC B
       |                         |
 chat_e2e_client A N       chat_e2e_client B N
 (production modules)      (production modules)
```

两个 ChatServer 共享本 run-id 的 MySQL database 与 Redis namespace，但必须有不同 logical server identity、TCP/RPC
endpoint、process identity 和 log/evidence path。Status production discovery 按真实 count/routing behavior 让第一个客户端
获得 A、第二个获得 B；测试不得覆盖 discovery result。若任一客户端得到同一 logical server 或通过 direct endpoint 绕过
Gate/Status discovery，topology 失败。

### 6.3 Bounded waits and cleanup

- dependency health 最多 120 秒；SchemaMigration 最多 120 秒；每个 application/client protocol-ready 最多 30 秒；
  每个业务 command/history page 最多 10 秒；route recovery/server restart 最多 30 秒；graceful stop 最多 10 秒，
  然后只对 identity 匹配的 run-owned child escalation；full hosted job 必须有硬 timeout；
- 禁止固定 sleep 充当正确性同步；使用 protocol-ready、event/correlation、future/condition、bounded API polling；
- 失败先保存 primary evidence，再按 client B→client A→Chat B→Chat A→Gate→Status→Varify→run-owned app resources→
  Mailpit messages→Redis prefix→MySQL database/schema→temp root 逆序清理；containers 最后由 job 销毁；
- cleanup 后必须证明 child/thread/QObject/socket/port/temp/mail/key/schema 全部释放，application ports 可重新 bind；
  primary 与 cleanup failure 分列，任一非空均使 gate 非零。

### 6.4 Secret/redaction and evidence

JUnit、stdout/stderr、topology、DB/Redis evidence、manifest 与上传 artifact 只允许 run-id、actor alias、server alias、
correlation ID、stable error、message_id、UUID 的不可逆 evidence hash、relative path、port 与 timing。runtime password、
token、verification code、raw email localpart、connection credential 和 message secret canary 必须在写 evidence 前 exact-value
redact；scan 只输出命中类别/位置，不回显值。证据必须能证明流程经过 Gate/Varify/Status/Chat A/Chat B/Redis/MySQL，
但不能通过记录 secret 来“证明”。

## 7. Planned Test ID / report ownership

| Plan | Planned Test ID range | Planned contract family | Planned report family |
| --- | --- | --- | --- |
| 3D-00 | `E03-CONTRACT-01..12` | dataset/schema, client runtime/driver, topology ownership | `linux_phase3d_contract.xml` |
| 3D-01 | `E03-JOURNEY-01..16` | two-server/two-client ready, verification/register/login/discovery/friend | `linux_phase3d_journey.xml` |
| 3D-02 | `E03-XMSG-01..16` | cross-instance commit/routing/delivery/auth/idempotency | `linux_phase3d_messaging.xml` |
| 3D-03 | `E03-RECOVER-01..18` | ACK loss, reconnect/restart, history paging/order/dedup | `linux_phase3d_recovery.xml` |
| 3D-04 | `E03-COMPAT-01..18` | actual N/N-1 client/service/peer/schema business flow or bootstrap blocker | `linux_phase3d_compatibility.xml` |
| 3D-05 | `E03-CLOSE-01..12` | manifest/workflow/secret/cleanup/flaky/master-release closeout | `linux_phase3d_gate.xml` |

这些 ranges 只是 planned stable namespace，不是 testcase 数或 report count 承诺。只有 non-empty test source、production target
link、real runner registration 与 emitted JUnit 同时存在时，3D-05 才按实际 case 更新 manifest、matrix、README、Summary；
unused IDs 保持 planned，不创建空/always-pass case 填满范围。

## 8. Dependency DAG 与 waves

```text
Phase 3C accepted
  -> 3D-00 fixed dataset + production client/scenario contracts
  -> 3D-01 two-server/two-client identity/auth/friend journey
  -> 3D-02 cross-instance private chat + Redis/MySQL/idempotency
  -> 3D-03 ACK-loss/reconnect/restart/history/order/dedupe
  -> 3D-04 N/N-1 client/service/peer/schema business matrix or bootstrap
  -> 3D-05 hosted-Ubuntu master/release closeout
  -> separate Release gate R-00 build once -> R-01 artifact-only smoke -> R-02 UAT -> R-03 promotion
```

| Wave | Plan | Depends on | Exclusive ownership |
| ---: | --- | --- | --- |
| 13 | 3D-00 | Phase 3C accepted | dataset, client runtime/driver, scenario/topology contracts |
| 14 | 3D-01 | 3D-00 | identity/auth/friend journey |
| 15 | 3D-02 | 3D-01 | messaging/routing/persistence journey |
| 16 | 3D-03 | 3D-02 | fault relay, reconnect/restart/history journey |
| 17 | 3D-04 | 3D-03 | compatibility business-flow schema and runner |
| 18 | 3D-05 | 3D-04 | public runner, workflow, manifest/governance/Summary closeout |

## 9. Plan 3D-00 — Dataset, production client and topology contracts

Status: **Planned**
Wave: **13**
Depends on: **Phase 3C accepted**
Coverage: **G-016; DG-09, DG-11, DG-12, DG-14, DG-16, DG-18, DG-19**

<objective>
冻结 production-shaped synthetic dataset、双 server/双 client logical topology、production ClientSessionRuntime 和
scenario/evidence Interfaces，并证明所有 caller/tests 链接 Phase 3A/3B/3C production targets。最难约束先完成：若不能在
不复制客户端状态机/parser 的前提下创建两个隔离 client processes，后续 3D 全部停止。
</objective>

<tasks>

<task id="3D-00-T1" type="tdd">
<name>RED：复核 Phase 3C 并冻结 dataset、Test ID 与 topology schema</name>
<read_first>
- `tests/plans/PHASE-3C-PLAN.md`
- `tests/plans/PHASE-3C-SUMMARY.md` (planned upstream completion artifact; execution requires it)
- `tests/plans/PHASE-3B-RELEASE-DECISIONS.md`
</read_first>
<files>
- `tests/plans/PHASE-3D-TEST-PLAN.md` (planned; create)
- `tests/integration/fixtures/phase3d-synthetic.schema.json` (planned; create)
- `tests/integration/fixtures/phase3d-synthetic.json` (planned; create)
- `tests/integration/phase3d/topology.schema.json` (planned; create)
- `tests/build/phase3d_contracts.cmake` (planned; create)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; register planned ownership only)
</files>
<action>
Per DG-12/DG-16, stop unless Phase 3C actual Summary proves current-N production Modules, real dependencies, MessageCommit,
four-process lifecycle and clean hosted-Ubuntu evidence. Record actual upstream target/header/report names in
`PHASE-3D-TEST-PLAN.md` and map them to the Interfaces in section 5 without introducing aliases solely for naming. Freeze the exact
dataset identity, seed `0x3D20260830`, actors, logical server names, journey step order, boundary/Unicode/duplicate/invalid relation
fixtures, deterministic UUID templates and `2 * production_page_size + 1` history requirement. Schema must require credential/code
references rather than values and reject real/public domains, fixed endpoint, missing run-id namespace, mutable seed/topology and
insufficient pagination rows. Freeze `E03-*` ranges as planned-only with Domain/Level/deadline/report/RED/mutation/Gap ownership;
preserve all inherited actual counts and open G-016/G-017. Add RED structure fixtures for missing second server/client, same server
identity/endpoint, same client assignment, fake dependency, copied production parser and secret-valued dataset.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-00-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Actual 3C paths/targets/evidence are mapped and every hard prerequisite is PASS or the plan stops with a stable blocker.
- Dataset and topology are versioned, production-shaped, fixed-seed/fixed-logical-order, run-id isolated and secret-free.
- Every planned ID is unique and excluded from testcase/report totals until real source/registration exists.
- A one-server, one-client, same-server, fake-dependency or public-data topology cannot validate.
</acceptance_criteria>
</task>

<task id="3D-00-T2" type="auto" tdd="true">
<name>GREEN/mutation：建立一个 production ClientSessionRuntime 与两个真实 caller Adapters</name>
<read_first>
- actual Phase 3A `AuthFlowCoordinator` header/Summary path recorded by 3D-00-T1
- actual Phase 3B `GateHttpTransport` and `ChatTcpTransport` headers/targets recorded by 3D-00-T1
- actual Phase 3C client message/session/model targets and MessageCommit mapping recorded by 3D-00-T1
</read_first>
<files>
- `chat/clientsessionruntime.h` (planned; create unless actual upstream equivalent exists)
- `chat/clientsessionruntime.cpp` (planned; create unless actual upstream equivalent exists)
- `chat/mainwindow.h` (existing; modify caller wiring)
- `chat/mainwindow.cpp` (existing; modify caller wiring)
- `chat/logindialog.h` (existing; modify caller wiring)
- `chat/logindialog.cpp` (existing; modify caller wiring)
- `chat/CMakeLists.txt` (existing; register one production target)
- `tests/integration/phase3d/ProductionClientDriver.h` (planned; create)
- `tests/integration/phase3d/ProductionClientDriver.cpp` (planned; create)
- `tests/integration/phase3d/chat_e2e_client.cpp` (planned; create)
- `tests/integration/phase3d/client_runtime_contract_tests.cpp` (planned; create)
- `cmake/tests/phase3d-client-runtime.cmake` (planned; create)
</files>
<action>
RED `E03-CONTRACT-01..07` for two simultaneously alive runtime instances with distinct run/client identities, finite Start/Execute/
Snapshot/Stop, operation correlation, generation-scoped late-result suppression, original `msg_uuid` pending retention, server-ID
model dedupe and complete QObject/socket/timer ownership. Reuse an actual upstream equivalent if it already owns these contracts;
otherwise deepen the existing client cluster into one production `ClientSessionRuntime` used by the GUI and tests. GUI dialogs/windows
adapt UI events/actions to the runtime; planned `JsonLineClientDriverAdapter` in `chat_e2e_client` adapts a run-owned inherited control
pipe to the same command/result Interface. The child must link production GateHttpTransport, ChatTcpTransport,
AuthFlowCoordinator, ClientSession and MessageModelStore targets and use canonical generated protocol; it may project safe public
events/snapshots but may not expose tokens, codes, socket/model containers, inject business results or copy parser/state rules.
Mutation must disconnect one production target, accept an old generation result, clear UUID before terminal acknowledgement, share
state between two client identities or add a fake/test switch; focused/structure tests must fail. Formal `chat.exe` receives no
test mode or control pipe.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-00-T2</automated>
</verify>
<acceptance_criteria>
- GUI and `chat_e2e_client` call one production runtime Interface; tests and executable link the same client production targets.
- Two client processes have independent session/generation/model/pending state and bounded lifecycle.
- No production parser/business/state implementation, public private container, test macro, fake EXE flag or secret-bearing event exists.
- Driver evidence is sufficient for public journey assertions but makes no GUI-pixel claim.
</acceptance_criteria>
</task>

<task id="3D-00-T3" type="auto" tdd="true">
<name>GREEN/mutation：扩展 RunContext 为双 server/双 client topology 与 external-state evidence</name>
<read_first>
- actual Phase 3C `RunContext`, `ProcessHarness`, `PosixProcessAdapter`, `DependencyCoordinator` and four-process scenario paths recorded by 3D-00-T1
- `tests/integration/fixtures/phase3d-synthetic.json` (planned output of 3D-00-T1)
- `tests/integration/phase3d/ProductionClientDriver.h` (planned output of 3D-00-T2)
</read_first>
<files>
- `tests/integration/phase3d/Phase3DScenario.h` (planned; create)
- `tests/integration/phase3d/Phase3DScenario.cpp` (planned; create)
- `tests/integration/phase3d/ExternalStateEvidence.h` (planned; create)
- `tests/integration/phase3d/ExternalStateEvidence.cpp` (planned; create)
- `tests/integration/phase3d/topology_contract_tests.cpp` (planned; create)
- `tests/integration/config/phase3d/chat-a.ini.template` (planned; create)
- `tests/integration/config/phase3d/chat-b.ini.template` (planned; create)
- `cmake/tests/phase3d-topology.cmake` (planned; create)
- `tests/integration/phase3d/README.md` (planned; create)
</files>
<action>
RED `E03-CONTRACT-08..12` for one RunContext owning distinct Chat A/B identities/endpoints/configs/processes, client A/B process
identities/control channels, shared run-id MySQL/Redis/Mailpit namespaces, topology step ledger, deadlines and reverse cleanup. Extend
the existing Phase 3C scenario rather than create a parallel process/dependency coordinator. `ExternalStateEvidence` performs
read-only allowlisted queries for Redis route identity, MySQL message row/schema version and Mailpit correlation; it cannot write
production state, derive business decisions, log credentials or expose connection strings. Protocol ready must establish two legal
Chat TCP handshakes and client-driver control readiness; PID/log/sleep alone is invalid. Mutate Chat B name/port to equal A, map both
clients to one server, omit a child/resource from the teardown ledger, write a secret to evidence or replace a production process
with an in-memory host; focused contracts must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-00-T3</automated>
</verify>
<acceptance_criteria>
- RunContext is the sole owner of every application/client endpoint, process, namespace, evidence path and cleanup action.
- Topology cannot become ready without two distinct production ChatServer identities and two distinct production-module client processes.
- External evidence is read-only, allowlisted and redacted; no private container or copied business query owns behavior.
- Every wait/stop is bounded and every normal/failure resource has identity-scoped cleanup ownership.
</acceptance_criteria>
</task>

</tasks>

## 10. Plan 3D-01 — Two-server identity, authentication and friendship journey

Status: **Planned**
Wave: **14**
Depends on: **3D-00 PASS**
Coverage: **G-016; DG-11, DG-12, DG-16, DG-19**

<objective>
启动固定双 ChatServer/双 production-module client topology，执行两名 synthetic user 的验证码、注册、Gate 登录/
Status 服务发现、Chat Token 登录、好友申请与接受；每一步同时证明跨 layer correlation、不同 server assignment、
外部状态和 secret-safe evidence。
</objective>

<tasks>

<task id="3D-01-T1" type="tdd">
<name>RED/GREEN：启动两个 ChatServer 与两个分别发现不同 server 的客户端</name>
<read_first>
- `tests/integration/phase3d/Phase3DScenario.h` (planned output of 3D-00)
- `tests/integration/phase3d/ProductionClientDriver.h` (planned output of 3D-00)
- `tests/integration/fixtures/phase3d-synthetic.json` (planned output of 3D-00)
</read_first>
<files>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned; create)
- `tests/integration/phase3d/Phase3DScenario.cpp` (planned; modify)
- `tests/integration/phase3d/journey-steps.schema.json` (planned; create)
- `cmake/tests/phase3d-journey.cmake` (planned; create)
</files>
<action>
RED `E03-JOURNEY-01..05` for dependency health/migration, protocol-ready Gate/Status/Varify/Chat A/Chat B, two client processes,
distinct Chat logical identities and dynamic endpoints. Start in the section 6 order. Use actual Status discovery: client A login/
Chat authentication updates production server-count/session state before client B discovery, so the deterministic tie/count policy
assigns A then B; the scenario must assert the returned server identity/endpoint and Redis route rather than override it. Both client
TCP connections must stay attached to their discovered server through the journey. Missing/false ready, duplicate server identity,
same assignment, fixed endpoint, direct Chat endpoint bypass or unavailable dependency is nonzero. Mutation that ignores discovery
and forces an endpoint or marks a PID as ready must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-01-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Gate/Status/Varify/Chat A/Chat B and both client processes have identity/protocol-ready evidence within deadlines.
- Client A and B receive different ChatServer identities from production discovery and connect to those exact endpoints.
- Redis route/session evidence matches each authenticated user/server without secret values or private-state inspection.
- No direct config override, fake server or single-server fallback can pass.
</acceptance_criteria>
</task>

<task id="3D-01-T2" type="auto" tdd="true">
<name>GREEN/mutation：执行验证码、注册、Gate 登录/发现与 Chat Token 登录</name>
<read_first>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned output of 3D-01-T1)
- `tests/integration/phase3d/ExternalStateEvidence.h` (planned output of 3D-00)
- production GateRequest/AuthFlowCoordinator/EmailDelivery/SchemaMigration Interfaces recorded by upstream Summaries
</read_first>
<files>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/Phase3DScenario.cpp` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
Add `E03-JOURNEY-06..11` for both actors. Each client sends the production Gate verification request; evidence waits for exactly
one Mailpit message by run-id/correlation, extracts the code only into protected memory, registers with production Gate/MySQL, logs in
through Gate, consumes the Status assignment/token and authenticates over production Chat TCP. Assert each response once, expected
public error/result, MySQL user existence and Redis session/route through allowlisted readers. Negative mutations cover missing/
expired/wrong code, duplicate registration, wrong password, token mismatch and replaying actor A token from actor B; each must fail
closed before the next state-changing layer. Never persist or print password/code/token/raw email; exact runtime values are registered
with the redactor before any request. Mutation that bypasses Varify/Mailpit, logs a code, trusts another actor token or accepts a
second completion must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-01-T2</automated>
</verify>
<acceptance_criteria>
- Both users traverse Gate→Varify/Mailpit→MySQL→Status/Redis→Chat with step-level correlation and public outcomes.
- Wrong/expired/replayed identity inputs fail closed and do not execute later writes or authenticate a Chat session.
- Client A remains on Chat A and client B on Chat B after Chat-token login.
- Evidence proves participation of every layer without containing runtime secret/email/code/token/password values.
</acceptance_criteria>
</task>

<task id="3D-01-T3" type="auto" tdd="true">
<name>GREEN/mutation：执行好友申请/接受并运行 journey owning selector</name>
<read_first>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned output of 3D-01-T2)
- actual Phase 3A friend/request business Module Interface recorded by Phase 3C Summary
- production Chat request/notification protocol from canonical generated `chat.proto`
</read_first>
<files>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
- `cmake/tests/phase3d-journey.cmake` (planned; modify)
</files>
<action>
Add `E03-JOURNEY-12..16`: client A applies to B through production Chat A; B receives the cross-server/public notification while
connected to Chat B and accepts; both production client models converge to the same friend relation. Assert public request/accept
results, exactly one logical relationship in MySQL, correct Redis user routes, and both client snapshots. Exercise duplicate apply,
duplicate accept, self-friend, unknown user and unauthorized third-identity forms from the fixed dataset; all return stable errors and
create no extra relationship. No test writes friend rows directly. Mutations route the request locally without peer delivery, allow
self/duplicate rows, trust payload sender or skip one client model update; the focused selector must fail, then restore GREEN and run
the owning `phase3d-journey` label only.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-01-T3</automated>
</verify>
<acceptance_criteria>
- The fixed flow completes verification through mutual friendship with each client still attached to a different ChatServer.
- Friend request/accept crosses production server routing and produces one durable relation visible to both public client snapshots.
- Invalid/duplicate/unauthorized relations are stable failures with no extra DB/client state.
- `linux_phase3d_journey.xml` is emitted only from real registered cases; actual count remains deferred to 3D-05.
</acceptance_criteria>
</task>

</tasks>

## 11. Plan 3D-02 — Cross-instance private chat, Redis routing and MySQL persistence

Status: **Planned**
Wave: **15**
Depends on: **3D-01 PASS**
Coverage: **G-016; DG-12, DG-16, DG-20, DG-24**

<objective>
通过已建立好友关系的 client A→Chat A→Redis route→Chat B peer RPC→client B 路径提交私聊，证明 commit-before-
notify、MySQL durable row、same message_id/UUID mapping、跨实例投递、授权失败和重试幂等。成功含义限定为持久化与
可重试投递，不使用 exactly-once 描述。
</objective>

<tasks>

<task id="3D-02-T1" type="tdd">
<name>RED/GREEN：证明 commit 后的跨实例 Redis routing、peer RPC 与 delivery</name>
<read_first>
- `tests/integration/phase3d/identity_auth_friend_e2e_tests.cpp` (planned 3D-01 output)
- `tests/integration/phase3d/Phase3DScenario.h` (planned 3D-00 output)
- `tests/integration/phase3d/ExternalStateEvidence.h` (planned 3D-00 output)
</read_first>
<files>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned; create)
- `tests/integration/phase3d/Phase3DScenario.cpp` (planned; modify)
- `tests/integration/phase3d/messaging-evidence.schema.json` (planned; create)
- `cmake/tests/phase3d-messaging.cmake` (planned; create)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
RED `E03-XMSG-01..07` using the fixed canonical UUID/content from the dataset. Client A sends through production
ClientSessionRuntime/ChatTcpTransport to Chat A. Require Chat A to authenticate sender from its session, commit through MessageCommit,
read client B's run-scoped Redis route as Chat B, call the production Chat peer RPC, and let Chat B deliver through its production TCP
session to client B. Evidence must correlate the client operation, one MySQL row, one Redis route identity, peer RPC target B, sender
ack, recipient event, client A snapshot, client B snapshot and later public history; all observable copies carry the same server
`message_id` and available client UUID. Notification cannot precede successful commit. Mutation removing DB commit, routing to A,
bypassing peer RPC, notifying before commit, dropping `message_id`/UUID or accepting payload sender must fail. Direct in-process
handler calls, DB row insertion and endpoint overrides are forbidden.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-02-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- One public send crosses client A, Chat A, Redis routing, Chat B peer RPC and client B under one correlation.
- MySQL contains one durable row and every ack/peer/delivery/model/history observation uses its same server `message_id`.
- Client B is reached through Chat B, not a local Chat A session or direct test delivery.
- Failed commit produces no notification; evidence contains no message secret or credential.
</acceptance_criteria>
</task>

<task id="3D-02-T2" type="auto" tdd="true">
<name>GREEN/mutation：验证跨实例 UUID retry、Conflict 与授权失败</name>
<read_first>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned output of 3D-02-T1)
- actual Phase 3C MessageCommit contract/integration tests
- `tests/integration/fixtures/phase3d-synthetic.json` (planned)
</read_first>
<files>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
Add `E03-XMSG-08..13`. Retry the same authenticated sender/UUID and identical chat/recipient/content both serially and behind a
deterministic concurrency barrier; every completion must return the original `message_id`, MySQL must retain one row, and both client
models one logical item. Reuse the same UUID with different content, recipient or chat and require stable Conflict with no changed
row/event. Attempt send/history as a non-friend, unknown recipient, payload `from_uid` different from authenticated session and actor B
replaying actor A's UUID; require access/identity failure before notification or unauthorized history. This E2E repeats the upstream
unit/DB contract deliberately at the public cross-instance seam; it does not replace those Required tests. Mutations create a second
ID, key idempotency only by UUID without sender, ignore payload conflict, send before friend membership or authorize from payload;
focused tests must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-02-T2</automated>
</verify>
<acceptance_criteria>
- Identical retries yield one row and one stable `message_id`; reused UUID with different payload is Conflict.
- Sender identity comes only from authenticated session and is part of the dedupe key.
- Non-friend/unknown/cross-user attempts cannot write, notify or read history.
- No assertion or report uses the phrase or semantics of network exactly-once.
</acceptance_criteria>
</task>

<task id="3D-02-T3" type="auto" tdd="true">
<name>Mutation/runner：收口跨实例 messaging evidence 与 resource ownership</name>
<read_first>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned output of 3D-02-T2)
- `tests/integration/phase3d/messaging-evidence.schema.json` (planned output of 3D-02-T1)
- actual Phase 3C topology/teardown evidence schemas
</read_first>
<files>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/messaging-evidence.schema.json` (planned; modify)
- `cmake/tests/phase3d-messaging.cmake` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
Add `E03-XMSG-14..16` for recipient disconnect during delivery, peer RPC deadline/unavailable and bounded client/server completion.
Persisted messages remain discoverable by authorized history and retry returns the same ID; an uncommitted failure has no row/event.
Evidence schema requires server A/B identities, Redis route target, DB row identity, stable ID mapping, primary/cleanup status and
redaction result while excluding secret/message content. Execute and restore mutations for notify-before-commit, wrong Redis target,
missing peer deadline, same-ID break, unauthorized history and omitted cleanup entry. Run focused cases first, then only the owning
`phase3d-messaging` label. No automatic retry is allowed to turn a first failure green.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-02-T3</automated>
</verify>
<acceptance_criteria>
- Success/failure evidence distinguishes durable commit from delivery attempt and supports authorized recovery.
- Every wait and peer/client operation has a deadline; unavailable peer does not hang or fabricate delivery.
- Mutation results prove commit ordering, route target, stable ID, authorization and cleanup assertions are effective.
- `linux_phase3d_messaging.xml` is produced from actual cases without predeclaring its final count.
</acceptance_criteria>
</task>

</tasks>

## 12. Plan 3D-03 — ACK loss, reconnect, server recovery and history semantics

Status: **Planned**
Wave: **16**
Depends on: **3D-02 PASS**
Coverage: **G-016; DG-12, DG-16, DG-20, DG-24**

<objective>
在真实 current-N topology 上确定性制造 commit 后 ACK 丢失、client disconnect、ChatServer restart 和通知/历史重复，
证明客户端用原 UUID bounded retry、route/session recovery、same message_id、history pagination/order/server-ID dedupe，
并完成 fault-path cleanup。
</objective>

<tasks>

<task id="3D-03-T1" type="tdd">
<name>RED/GREEN：用 production codec relay 证明 ACK 丢失后的原 UUID retry</name>
<read_first>
- `tests/integration/phase3d/cross_instance_messaging_e2e_tests.cpp` (planned 3D-02 output)
- production `ChatFrameCodec` target/header recorded by Phase 3B/3C Summary
- production `ClientSessionRuntime` and pending-message Interface from 3D-00
</read_first>
<files>
- `tests/integration/phase3d/FrameFaultRelay.h` (planned; create)
- `tests/integration/phase3d/FrameFaultRelay.cpp` (planned; create)
- `tests/integration/phase3d/fault-schedules/ack-loss.json` (planned; create)
- `tests/integration/phase3d/reconnect_history_e2e_tests.cpp` (planned; create)
- `cmake/tests/phase3d-recovery.cmake` (planned; create)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
RED `E03-RECOVER-01..06`. For this fault-only case route client A through a run-owned loopback FrameFaultRelay whose upstream is
Chat A; relay frame recognition must link the exact production ChatFrameCodec target and cannot copy header/parser constants. Forward
the send request, wait until the committed row/peer delivery is externally observable, then drop the matching server→client ack and
close that connection generation. ClientSessionRuntime must keep the original UUID/payload pending, reconnect/authenticate within
bounded policy and resend the original request; MessageCommit returns Existing with the same `message_id`, MySQL still has one row,
and client A/B snapshots one item. Evidence describes a delivery attempt and idempotent recovery, never exactly-once. Mutations clear
pending UUID on disconnect, allocate a new UUID, accept a different message ID, duplicate the model row, use a copied codec or leave
relay sockets/threads alive; focused tests must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-03-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- A real server commit and peer delivery occur before the selected ack is lost.
- Client retry uses the original UUID and returns the original server ID with one MySQL row.
- Notification/retry duplication produces one logical model item per client.
- Relay is run-owned, bounded, production-codec-linked and fully cleaned; normal journey remains direct-to-server.
</acceptance_criteria>
</task>

<task id="3D-03-T2" type="auto" tdd="true">
<name>GREEN/mutation：验证 client reconnect、ChatServer restart 与 Redis route recovery</name>
<read_first>
- `tests/integration/phase3d/reconnect_history_e2e_tests.cpp` (planned output of 3D-03-T1)
- actual production Redis Adapter/session/routing Interfaces from Phase 3C Summary
- `tests/integration/phase3d/ExternalStateEvidence.h` (planned)
</read_first>
<files>
- `tests/integration/phase3d/reconnect_history_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/fault-schedules/server-route-recovery.json` (planned; create)
- `tests/integration/phase3d/Phase3DScenario.cpp` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
Add `E03-RECOVER-07..12`. Disconnect client B and require the old connection generation/session to stop receiving; restart run-owned
Chat B through ProcessHarness with the same logical server identity but a new process creation identity and a validated endpoint from
its regenerated config. Before healthy re-registration, stale/missing Redis route must fail closed within deadline and must not route
to Chat A or old process identity. After Chat B protocol ready and Redis route refresh, client B performs production reconnect and
Chat-token re-login, then A→B send succeeds through the new Chat B identity. Also restart client A only and prove account/session/model
state restoration follows the documented production contract rather than shared singleton leakage. Mutations retain stale route,
accept PID/log-only ready, skip Chat-token login, deliver to old generation, auto-retry without deadline or fail to reap old process;
focused tests must fail.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-03-T2</automated>
</verify>
<acceptance_criteria>
- Stale/missing route is bounded fail-closed; no message is sent to the wrong or dead server identity.
- Restarted Chat B has a new process identity, reaches protocol ready, re-registers Redis route and accepts client B re-login.
- Reconnected clients suppress old-generation events and preserve only contractually durable state.
- Old processes, connections, timers and routes are released or reported as gate-failing cleanup entries.
</acceptance_criteria>
</task>

<task id="3D-03-T3" type="auto" tdd="true">
<name>GREEN/mutation：验证多页 history、server-ID order 与 notification/history dedupe</name>
<read_first>
- `tests/integration/fixtures/phase3d-synthetic.json` (planned)
- `tests/integration/phase3d/reconnect_history_e2e_tests.cpp` (planned output of 3D-03-T2)
- production history request/response protocol and MessageModelStore Interfaces from upstream Summaries
</read_first>
<files>
- `tests/integration/phase3d/reconnect_history_e2e_tests.cpp` (planned; modify)
- `tests/integration/phase3d/fault-schedules/history-replay.json` (planned; create)
- `cmake/tests/phase3d-recovery.cmake` (planned; modify)
- `tests/integration/phase3d/README.md` (planned; modify)
</files>
<action>
Add `E03-RECOVER-13..18`. Create the dataset's `2 * production_page_size + 1` conversation messages exclusively through the
public client send flow, alternating actors as declared, and record returned server IDs. Reconnect a client with an empty ephemeral
view, request history page-by-page through the production Interface and assert no gap/overlap at cursors, strictly documented
server-`message_id` order, Unicode/boundary content identity by safe hash, and final exhausted cursor. Deterministically replay one
notification and overlap one history item using the production-codec fault/evidence path; MessageModelStore must dedupe by stable
server ID while retaining UUID mapping and order. Unknown/unauthorized chat, zero/over-limit page size and stale cursor return stable
bounded errors. Mutations sort by client time/UUID, duplicate overlap, drop boundary row, accept unauthorized history or retain a stale
loading cursor; focused tests must fail. Restore, run focused cases, then only the owning `phase3d-recovery` label.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-03-T3</automated>
</verify>
<acceptance_criteria>
- Three real pages cover every committed server ID exactly once in documented order after model dedupe.
- Notification/history replay does not duplicate client rows; original UUID↔server-ID mapping remains stable.
- Invalid page/chat/cursor inputs fail within deadlines without leaking or modifying unauthorized data.
- `linux_phase3d_recovery.xml`, fault/evidence and teardown ledgers are secret-free and actual counts remain deferred.
</acceptance_criteria>
</task>

</tasks>

## 13. Plan 3D-04 — N/N-1 client, service, peer and schema business compatibility

Status: **Planned**
Wave: **17**
Depends on: **3D-03 PASS**
Coverage: **G-017; DG-13, DG-15, DG-16, DG-21, DG-24**

<objective>
复用 Phase 3C 的 immutable ArtifactResolver/ManifestVerifier/SchemaFixtureResolver，以同一固定 business journey 运行
N-1 client→N services、N client→N-1 services、N↔N-1 Chat peer RPC 和 N-1 schema/data→N；unsupported 必须在写入前
fail-fast。若没有 promoted N-1，所有 N-1 cell 输出精确 non-PASS bootstrap 状态且 G-017 保持 open。
</objective>

<tasks>

<task id="3D-04-T1" type="tdd">
<name>RED：冻结 business-flow matrix input、cell status 与 write-safety contract</name>
<read_first>
- actual Phase 3C `ArtifactResolver`, `ManifestVerifier`, `SchemaFixtureResolver`, compatibility matrix and report paths
- `tests/compatibility/baselines/n-minus-1.json` (planned Phase 3C output)
- `database/fixtures/n-minus-1-bootstrap.json` (planned Phase 3C output)
</read_first>
<files>
- `tests/compatibility/phase3d-business-matrix.schema.json` (planned; create)
- `tests/compatibility/phase3d-business-matrix.json` (planned; create)
- `tests/compatibility/phase3d_business_compatibility_tests.cpp` (planned; create)
- `cmake/tests/phase3d-compatibility.cmake` (planned; create)
- `tests/compatibility/README.md` (planned; modify)
</files>
<action>
Define `E03-COMPAT-01..08` and required cells: `n_minus_1_client_to_n_services`, `n_client_to_n_minus_1_services`,
`n_chat_a_to_n_minus_1_chat_b_peer`, `n_minus_1_chat_a_to_n_chat_b_peer`, and
`n_minus_1_schema_migrated_to_n_business_flow`. Each cell requires current/promoted artifact identity, external/internal digests,
binary/proto/schema/migration/fixture versions, dynamic endpoints, supported-direction declaration, deadline, pre/post write counters,
fixed journey steps and exact status `SUPPORTED_PASS | SUPPORTED_FAIL | UNSUPPORTED_FAIL_FAST |
BOOTSTRAP_NO_PROMOTED_N_MINUS_1`. Unsupported/bootstrap is never PASS/skipped-success. Resolver must reject old-source rebuild,
Actions cache, mutable artifact, path traversal, missing hash or schema fixture not tied to the artifact. RED mutations omit a cell,
substitute static descriptor fixture, label bootstrap PASS, allow unsupported write, mismatch schema/artifact or change the dataset
seed/topology; all fail before any process launch/write.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-04-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- Every required client/service/peer/schema direction has immutable inputs, fixed business steps, deadlines and one unambiguous status.
- No source rebuild/cache/static fixture can satisfy a process/schema business cell.
- Unsupported combinations prove zero writes before stable fail-fast; bootstrap cannot aggregate as PASS.
- Matrix uses the same production-shaped dataset/topology semantics as current-N rather than a weaker compatibility probe.
</acceptance_criteria>
</task>

<task id="3D-04-T2" type="auto" tdd="true">
<name>GREEN/mutation：运行真实 N/N-1 business matrix 或诚实 bootstrap blocker</name>
<read_first>
- `tests/compatibility/phase3d-business-matrix.json` (planned output of 3D-04-T1)
- actual Phase 3C artifact/schema resolver implementations and compatibility runner
- `tests/integration/phase3d/Phase3DScenario.h` (planned)
</read_first>
<files>
- `tests/compatibility/phase3d_business_compatibility_tests.cpp` (planned; modify)
- `tests/compatibility/Phase3DBusinessMatrixRunner.h` (planned; create)
- `tests/compatibility/Phase3DBusinessMatrixRunner.cpp` (planned; create)
- `tests/compatibility/phase3d-business-matrix.json` (planned; update only with verified identities/results)
- `cmake/tests/phase3d-compatibility.cmake` (planned; modify)
- `tests/compatibility/README.md` (planned; modify)
</files>
<action>
Implement `E03-COMPAT-09..18` by reusing Phase3DScenario and the Phase 3C resolver; do not create another artifact downloader,
protocol client or schema migrator. With a verified promoted N-1 baseline, safely extract exact artifacts, launch actual versioned
processes, and run the fixed supported business slices: verification/register/login/discovery/Chat login, friend relation, private
message with stable ID, authorized history and teardown. The N-1-client→N-services cell uses the N-1 client artifact against N
services/schema; the N-client→N-1-services cell uses the N client against the N-1 service set/schema only when declared supported;
mixed peer cells put client actors on N and N-1 ChatServers and require cross-version RPC routing; the schema cell restores the signed
N-1 fixture, applies the production N migration, then runs N services over preserved legacy user/friend/message plus a new idempotent
send. Unsupported directions must reject before DB/Redis write. If no promoted baseline exists, perform no fake N-1 launch: emit the
exact bootstrap status in every N-1 cell with required future identity fields, keep G-017 open and still run current-N 3D-00..03.
Mutations change artifact/manifest hash, run a rebuilt binary, silently accept unsupported wire, write before fail-fast, lose legacy
row, return a different retry ID or relabel bootstrap; focused runner must fail. Cleanup every extracted tree/process/port/schema/key.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-04-T2</automated>
</verify>
<acceptance_criteria>
- With N-1, actual immutable client/service/peer/schema processes complete supported business flows and each cell has bounded evidence.
- Unsupported combinations fail before writes with stable diagnostics; no silent parse/data mutation is accepted.
- N-1 schema→N preserves declared legacy invariants and supports a new DG-24 idempotent business send.
- Without N-1, no process/schema PASS is fabricated, G-017 remains open, and release routing records first-promotion bootstrap.
</acceptance_criteria>
</task>

</tasks>

## 14. Plan 3D-05 — Hosted-Ubuntu master/release CI closeout

Status: **Planned**
Wave: **18**
Depends on: **3D-04 actual matrix result or exact bootstrap blocker**
Coverage: **G-016, G-017; DG-09, DG-10, DG-11, DG-12, DG-13, DG-16, DG-20, DG-21, DG-23, DG-24**

<objective>
将 fixed current-N two-server E2E 与适用 N/N-1 business matrix 注册到 GitHub-hosted Ubuntu `master/release`
admission，按真实 cases/reports 收敛 manifest，证明 timeout/redaction/cleanup/flaky failure propagation，并在同一 candidate
SHA 上完成一次权威 Phase 3D lane。3D 只把 accepted SHA/evidence 路由给独立 Release gate，不在此构建或晋升产物。
</objective>

<tasks>

<task id="3D-05-T1" type="tdd">
<name>RED/GREEN：注册 Phase 3D public runner、actual report manifest 与稳定 CI check</name>
<read_first>
- `scripts/linux-ci.sh` (planned Phase 3C public runner)
- `.github/workflows/linux-ci.yml` (planned Phase 3C workflow)
- actual Phase 3C report manifest and Summary
</read_first>
<files>
- `scripts/linux-ci.sh` (planned upstream; extend)
- `.github/workflows/linux-ci.yml` (planned upstream; extend)
- `tests/manifests/phase3d-reports.json` (planned; create)
- `tests/build/phase3d_gate_contracts.cmake` (planned; create)
- `tests/CI-GOVERNANCE.md` (existing; update lane contract after real registration)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; update planned/actual ownership without guessed counts)
</files>
<action>
Create one public command Interface `scripts/linux-ci.sh --phase 3D --configuration Release --junit-dir <run-owned>` that first
requires the accepted Phase 3C preflight/services/schema/MessageCommit status, then runs 3D contract, journey, messaging, recovery and
compatibility/bootstrap selectors in DAG order under one RunContext and hard job timeout. Register stable hosted-Ubuntu check name
`Linux two-server business E2E` for `master` candidates and release admission; keep exact prior four Windows check names and Phase 3C
`Linux real dependencies and compatibility` check intact. The manifest maps planned Test ID→actual runner case→report→level→timeout→
owner and is populated from real registrations/emitted JUnit only; do not infer counts from ranges or predeclare aggregate totals.
Add `E03-CLOSE-01..06` RED structure contracts for missing/duplicate ID/source/target/report, same ChatServer/client assignment,
production source duplication, fake mode, floating runner/action/service identity, missing timeout, `continue-on-error`, retry wrapper,
missing `always()` upload/cleanup, secret marker and earlier-check drift. Required flaky cases remain in the owning selector and fail the
check; quarantine metadata may aid diagnosis but cannot skip, downgrade, mark allowed failure or replace a Required Test ID. Actions are
pinned by full SHA and existing service images by verified digest; no package install or application image is added.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-05-T1 --expect-red</automated>
</verify>
<acceptance_criteria>
- One public Phase 3D runner owns all real families in dependency order on pinned hosted Ubuntu.
- Manifest derives actual counts and preserves every inherited report/Test ID/check; planned range sizes never become counts.
- Missing/unavailable/timeout/failure/cleanup/secret/flaky result cannot be continued, retried, quarantined or waived green.
- Workflow contains no self-hosted label, application image, public endpoint, personal credential or renamed earlier Required check.
</acceptance_criteria>
</task>

<task id="3D-05-T2" type="auto">
<name>Mutation/full-lane：执行唯一一次完整 Phase 3D lane 与全资源审计</name>
<read_first>
- `scripts/linux-ci.sh` (planned output of 3D-05-T1)
- `tests/manifests/phase3d-reports.json` (planned output of 3D-05-T1)
- `.github/workflows/linux-ci.yml` (planned output of 3D-05-T1)
</read_first>
<files>
- `tests/manifests/phase3d-reports.json` (planned; update actual counts/results)
- `tests/CI-GOVERNANCE.md` (existing; update actual gate/evidence contract)
- `tests/REGRESSION.md` (existing; update actual Phase 3D command/reports)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; update actual evidence/status only)
- `tests/plans/PHASE-3D-SUMMARY.md` (planned; create only after accepted evidence)
</files>
<action>
Before the phase lane, run and restore focused mutations from every owner: mutable seed/same topology/fake client target; false ready;
verification/token secret leak; friend/sender authorization; commit-before-notify/Redis wrong route; same-ID/Conflict; ACK-loss UUID
retention; stale route/old generation; history gap/order/dedup; N-1 hash/write-safety/bootstrap status; missing report and cleanup
entry. Each mutation must have a captured nonzero focused result and restored GREEN; a random failure is not mutation evidence. Then
invoke the complete Phase 3D public runner exactly once on the master candidate SHA; do not rehearse or automatically retry the full
lane. `always()` preserves bounded primary evidence, then performs reverse identity cleanup and uploads every declared JUnit/topology/
routing/persistence/compatibility/teardown/redaction artifact with `if-no-files-found: error`. Scan evidence against runtime exact
values/canaries without echoing them; audit zero child/thread/QObject/socket/port/temp/mail/key/schema residue and rebind application
ports. Reconcile actual counts from emitted XML into manifest/matrix/governance/Regression/Summary. If any step fails, preserve evidence
and return to the earliest owning plan; do not rerun or move the case to quarantine.
The full-lane upload is exactly `phase3d-gate-evidence` and contains `phase3d-reports.json`, its listed `junit/*.xml`,
`topology.json`, `compatibility.json`, `teardown.json`, and `redaction.json`. The manifest records workflow/run/attempt/head
SHA, artifact API digest, actual per-report testcase counts, and the inherited `{reports:12,testcases:180}` baseline.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release</automated>
</verify>
<acceptance_criteria>
- Exactly one complete Phase 3D lane is the closeout evidence; focused runs/mutations are separately identified.
- Current-N journey/messaging/recovery is PASS; N-1 cells are actual supported results or exact non-PASS bootstrap, never fabricated PASS.
- Every report and evidence artifact exists, actual counts reconcile, and no secret or owned resource remains.
- First failure remains visible and blocking; no retry, quarantine, waiver or cleanup masking changes its result.
</acceptance_criteria>
</task>

<task id="3D-05-T3" type="auto">
<name>Master/release closeout：验证同一 SHA checks 并路由 immutable Release gate</name>
<read_first>
- `tests/plans/PHASE-3D-SUMMARY.md` (planned draft from 3D-05-T2)
- `.github/workflows/linux-ci.yml` (planned)
- `.github/workflows/windows-ci.yml`
</read_first>
<files>
- `tests/plans/PHASE-3D-SUMMARY.md` (planned; finalize from remote evidence)
- `tests/TEST-CONTRACT-MATRIX.md` (existing; close only evidence-backed ownership)
- `tests/CI-GOVERNANCE.md` (existing; record accepted check/evidence route)
- `tests/manifests/phase3d-reports.json` (planned; freeze actual closeout identity)
</files>
<action>
Submit only the intended Phase 3D production/test/build/runner/docs changes through the repository's clean PR route. Require the same
master candidate SHA to have success for the four unchanged Windows checks, Phase 3C `Linux real dependencies and compatibility`, and
Phase 3D `Linux two-server business E2E`; runner/dependency unavailable, skipped, cancelled, timeout, missing report or cleanup failure
is not success. A release-admission invocation must verify and reference this exact accepted SHA, workflow run IDs, report hashes,
topology/schema/dataset identities and compatibility/bootstrap status; it must not rebuild apps, rerun to erase a failure or accept a
different SHA. Update Summary/matrix only after authoritative remote results: close G-016 only if current-N two-server flow is fully
proven; close G-017 only if the actual required N/N-1 cells ran, otherwise retain G-017 open with
`BOOTSTRAP_NO_PROMOTED_N_MINUS_1`. Route the accepted SHA to separate R-00, which builds the unified Windows set exactly once; R-01
artifact-only smoke, R-02 signed UAT and R-03 same-bytes promotion remain outside 3D. The first R-03 promotion creates the complete
N-1 artifact/schema pointer for the next cycle. Do not stage/commit protected user paths, build trees, credentials or unrelated files.
Publish exactly one `phase3d-release-admission` artifact from the accepted run; it contains `release-admission.json` and
`linux_phase3d_gate.xml`. The admission JSON records the accepted SHA, workflow/run/attempt, Phase 3C/3D manifest hashes,
compatibility status, six exact check URLs, and `next_route: R-00`, without tokens or other secret values.
</action>
<verify>
<automated>bash scripts/linux-ci.sh --phase 3D --configuration Release --selector 3D-05-T3</automated>
</verify>
<acceptance_criteria>
- Master/release admission is tied to one accepted SHA and complete evidence chain; no check is missing, renamed, skipped or waived.
- G-016/G-017 statuses reflect actual evidence and bootstrap never closes G-017.
- Phase 3D does not build/rebuild/promote a Windows artifact or perform human UAT.
- Release route is unambiguous: R-00 build once → R-01 artifact-only smoke/compat → R-02 same-digest UAT → R-03 same-bytes promotion/baseline.
</acceptance_criteria>
</task>

</tasks>

## 15. RED → GREEN → mutation → owning runner closure

1. **RED:** each code-producing task first creates a non-empty focused contract test using its planned Test ID. RED must be caused by
   the absent/incorrect Interface or behavior, not download instability, personal service, fixed sleep, missing secret or empty no-op case.
2. **GREEN:** implementation crosses the same production Interface/target used by formal GUI/server callers. Test composition may own
   processes, commands and faults; it may not inject business outcomes, expose private state or copy parser/algorithm.
3. **Mutation:** identity, topology, sender, route, transaction/ID, generation, order/dedupe, artifact hash, write-safety, redaction,
   report or cleanup mutations must make the focused selector nonzero. Restore before the owning runner.
4. **Owning runner:** 3D-00..04 run only their focused CTest label/compatibility selector after mutation restoration. They do not run
   the full Phase 3D lane.
5. **Phase lane:** 3D-05 runs the complete public Phase 3D lane once on the master candidate. Failure remains blocking and routes to
   the earliest owner; no automatic retry or quarantine substitutes another result.
6. **Evidence reconciliation:** stable planned ID, actual runner case, production source/target, JUnit, README, manifest and matrix
   must correspond. Counts come only from emitted reports; historical 12/180 remains a separately preserved baseline.

## 16. Threat model

### Trust boundaries

| Boundary | Untrusted input / state | Required controls |
| --- | --- | --- |
| synthetic dataset/control pipe → client runtime | command, actor, UUID/content, deadlines, control-frame injection | schema validation, run-id/nonce, allowlisted command/result, production Interface only, redaction |
| client A/B → Gate/Chat A/B | HTTP/TCP/protobuf fields, sender claim, UUID, pagination, disconnect/replay timing | production bounds/parser, authenticated principal, membership, deadlines, generation identity |
| Chat A → Redis → Chat B peer RPC | route/server identity, stale keys, peer payload, deadline | run-prefix route, server identity validation, finite peer call, fail-closed stale/missing route |
| Chat/MessageCommit → MySQL | transaction, unique key, payload conflict, schema version | explicit transaction, `UNIQUE(send_id, client_msg_uuid)`, prepared statements, migration ledger |
| Mailpit → registration driver | verification message/code and correlation | run-id mailbox, in-memory extraction, exact-value redaction, no public SMTP |
| FrameFaultRelay → client/Chat | dropped/replayed/coalesced frames and close timing | production codec target, bounded buffer/deadline, run-owned endpoints, fault allowlist |
| promoted N-1 artifact/schema → matrix | archive paths, binaries, manifest/hash, legacy data, support declaration | durable promoted source, external/internal digest, safe extraction, fail-fast-before-write |
| workflow/processes → report store | stdout/stderr, JUnit, topology, DB/Redis evidence, cleanup result | bounded allowlist, relative paths, secret scan, primary+cleanup failures, required report manifest |

### STRIDE register

| Threat ID | Category | Target | Disposition | Required mitigation / evidence |
| --- | --- | --- | --- | --- |
| T-3D-01 | Spoofing / Elevation | sender/client control | mitigate | run-id+nonce control identity; sender only from authenticated Chat session; cross-actor token/from_uid mutations |
| T-3D-02 | Spoofing / DoS | Redis route/server identity | mitigate | distinct server identity/endpoint, route-to-process evidence, stale/missing route fail-closed, restart re-registration |
| T-3D-03 | Tampering / Repudiation | UUID replay/payload conflict | mitigate | DB unique key, same payload→same ID, mismatched payload→Conflict, serial/concurrent/public E2E evidence |
| T-3D-04 | Tampering / Data loss | commit/delivery order | mitigate | explicit upstream transaction, commit-before-notify mutation, one durable row, peer/history same-ID correlation |
| T-3D-05 | Tampering / Data loss | history order/dedup | mitigate | server `message_id` order, multi-page gap/overlap assertions, notification/history replay dedupe |
| T-3D-06 | Information disclosure | dataset, control/log/JUnit/evidence | mitigate | synthetic references, runtime exact-value redactor, allowlisted evidence, no raw credential/code/token/email/content |
| T-3D-07 | Denial of service | clients/servers/relay/waits | mitigate | max bounds, finite queues/operations, protocol-ready deadlines, bounded retry/reconnect/stop, hard job timeout |
| T-3D-08 | Tampering / Repudiation | N-1 artifact/schema | mitigate | promoted immutable input only, external/internal hashes, safe extraction, bootstrap cannot pass |
| T-3D-09 | Tampering / Data loss | unsupported mixed version | mitigate | stable negotiation result, fail-fast before write, pre/post external-state counters |
| T-3D-10 | Elevation / Tampering | test seam | mitigate | one production client/runtime/codec/business target; no fake EXE, test macro, private access or copied parser |
| T-3D-11 | Repudiation / DoS | flaky/report/cleanup | mitigate | first Required failure blocks, no retry/quarantine waiver, every report required, primary+cleanup failures retained |
| T-3D-SC | Supply chain | npm/vcpkg/actions/service images | mitigate | 3D adds no package install; retain locks and verified action/image identities; any dependency/lock change stops for legitimacy review |

本 threat model 不声称完整 ASVS compliance，不把 hosted-loopback plaintext 结果解释为公网 TLS/password 安全认证，
也不把幂等持久化误写成网络 exactly-once。

## 17. Planning constraint and pre-mortem gates

| Likely failure | Earliest detection | Required response |
| --- | --- | --- |
| existing client singleton/state cannot support two isolated clients without copied implementation | 3D-00 client-runtime two-process RED/structure mutation | deepen/reuse one production ClientSessionRuntime; stop all downstream 3D until GUI/driver share it |
| ACK-loss test is race/flaky and retry hides the defect | 3D-03 production-codec FrameFaultRelay contract | deterministic frame/correlation fault, fixed deadline, first failure blocking; no retry/quarantine |
| topology appears green while both clients use one server or peer delivery is local | 3D-01 assignment + 3D-02 Redis/peer evidence | reject same identity/endpoint/route; require Chat A→Redis→Chat B process correlation |
| first release has no real N-1 but reports compatibility success | 3D-04 schema/status mutation | exact non-PASS bootstrap status, G-017 open, first R-03 promotion creates next-cycle baseline |
| failure evidence leaks code/token or cleanup removes shared/pre-existing state | 3D-00 schema + 3D-05 redaction/identity cleanup mutations | exact-value redaction; only run-owned identity cleanup; both leak and cleanup failure block |

## 18. Stop conditions

Stop the current chain, preserve non-sensitive primary/cleanup evidence and return to the earliest owning plan if any occurs:

1. Phase 3C accepted Summary/evidence is missing, or current-N production Interface/real Adapter/schema/MessageCommit/four-process
   prerequisites are incomplete.
2. Two distinct ChatServer production processes and two isolated production-module client processes cannot be constructed without
   copied parser/business/state, fake EXE mode, test macro, private container, direct handler or endpoint bypass.
3. Both clients discover/connect the same ChatServer, server identities/endpoints collide, a single-server fallback passes, or public
   friend/message delivery does not prove Redis route plus peer ChatServer process.
4. Dataset seed/logical topology is mutable, lacks production-shape Unicode/boundary/duplicates/pagination scale, contains real data/
   endpoint/credential, or runtime secret/code/token/message appears in source/evidence.
5. Any readiness, request, history page, retry, reconnect, peer RPC, server restart, stop or cleanup wait lacks a finite deadline or
   relies on fixed sleep/PID/log text alone.
6. `(sender,msg_uuid)` can create multiple rows/IDs, different payload is not Conflict, sender comes from payload, notification occurs
   before commit, client loses the original UUID, or documentation/test claims network exactly-once.
7. Notification/history replay duplicates client rows, pagination has gap/overlap/non-server-ID ordering, stale route/generation is
   accepted, or restart leaks old process/session/socket/port.
8. N-1 is rebuilt from source/cache/mutable artifact, digest/schema identity fails, unsupported combination writes before fail-fast,
   static fixture substitutes a process, or bootstrap is aggregated as PASS.
9. Hosted runner/dependency/check is unavailable/skipped/cancelled, an action/image/tool identity floats, report/count is missing or
   guessed, redaction finds a secret, cleanup ledger/residue is nonempty, or any Required failure is retried/quarantined/waived green.
10. Earlier 12/180 baseline, Phase 3A/3B/3C actual reports/Test IDs, four Windows check names, Phase 3C Linux check, protected user paths
    or staged scope is reduced/changed without D-04 review.
11. The same blocker repeats three consecutive execution turns without new evidence: stop, retain logs/ledger and create a handoff;
    do not loop or weaken the contract.
12. 本机 vcpkg 固定路径缺失/不一致、出现 manifest 自动安装，或任何 package/root/triplet/baseline/tool identity 变更
    未取得 DG-25 的本次明确批准；不得自动 restore、换目录或删除重建。

## 19. Phase completion criteria

Phase 3D may enter completion review only when all applicable conditions are true:

- Fixed dataset/schema/topology, seed, run-id isolation, ClientSessionRuntime/production driver and scenario/evidence Modules are real,
  registered and structure-gated; GUI/client driver/server/tests share production targets.
- Current-N Gate/Status/Varify/Chat A/Chat B and two distinct clients reach protocol ready; production discovery keeps client A on A
  and B on B through verification, registration, login, Chat login and mutual friendship.
- A→B and B→A private messages cross Redis routing and peer ChatServer RPC after MySQL commit; ack/peer/recipient/history/client models
  preserve the same `message_id` and one authenticated sender/UUID row.
- ACK loss, disconnect, client reconnect, ChatServer restart, route re-registration, original-UUID retry and same-ID Existing response
  are deterministic; no network exactly-once claim appears.
- At least three history pages exercise boundaries, server-ID order and notification/history overlap; client dedupe leaves each
  committed ID once and access-control/error paths are stable.
- If promoted N-1 exists, required actual client/service/peer/schema business cells have supported PASS/fail-fast evidence; if absent,
  exact bootstrap non-PASS is retained and G-017 stays open.
- Every focused mutation produced nonzero before restoration; owning selectors are GREEN. One and only one full Phase 3D lane on the
  accepted master candidate is complete with actual report reconciliation, secret-safe evidence and empty teardown/residue audit.
- The same SHA has the four unchanged Windows checks, Phase 3C Linux check and `Linux two-server business E2E`; no retry, quarantine,
  waiver, missing report or skipped/unavailable runner substitutes success.
- G-016 closes only from current-N evidence; G-017 closes only from actual N/N-1 evidence. Bootstrap status routes unchanged to the
  first Release promotion and next compatibility cycle.

## 20. Artifacts this phase produces

All items remain planned until execution evidence exists:

- `tests/plans/PHASE-3D-TEST-PLAN.md`, actual `PHASE-3D-SUMMARY.md`, updated matrix/governance/Regression and
  `tests/manifests/phase3d-reports.json` with real counts.
- versioned `phase3d-synthetic.schema.json`/`phase3d-synthetic.json`, topology/journey/evidence schemas, dynamic config templates and
  deterministic fault schedules.
- production `ClientSessionRuntime` (or mapped upstream equivalent), GUI wiring, `chat_e2e_client`, ProductionClientDriver,
  Phase3DScenario and read-only ExternalStateEvidence.
- current-N identity/auth/friend, cross-instance messaging, ACK-loss/reconnect/restart/history E2E test Modules and production-codec
  FrameFaultRelay.
- Phase 3D N/N-1 business matrix schema/runner/results tied to promoted immutable artifact/schema or exact bootstrap status.
- extended `scripts/linux-ci.sh`, pinned hosted-Ubuntu workflow/check, actual JUnit/topology/routing/persistence/compatibility/
  redaction/teardown evidence, accepted master/release-admission SHA chain.

## 21. Multi-source coverage audit

| Source | ID / item | Owning plans / section | Status |
| --- | --- | --- | --- |
| GOAL | fixed two-ChatServer/two-client cross-instance business E2E | 3D-00..03/05 | COVERED |
| GOAL | current/previous client-service-schema business compatibility | 3D-04/05 | COVERED with honest bootstrap rule |
| REQ | G-016 two-ChatServer public business flow | 3D-00..03/05 | COVERED, planned |
| REQ | G-017 N/N-1 compatibility | 3D-04/05 | COVERED, actual-or-bootstrap; not pre-completed |
| RESEARCH | production-module client driver rather than GUI pixels/copied client | 3D-00 Interface/Adapter seam | COVERED |
| RESEARCH | versioned production-shaped dataset, fixed seed/topology, run-id isolation | sections 6/9; 3D-00 | COVERED |
| RESEARCH | verification→register→login/discovery→Chat login→friend journey | 3D-01 | COVERED |
| RESEARCH | Chat A→Redis route→Chat B peer delivery + MySQL durability | 3D-02 | COVERED |
| RESEARCH | ACK loss, original UUID retry, same ID, reconnect/restart/history/order/dedup | 3D-03 | COVERED |
| RESEARCH | immutable artifact/schema matrix or bootstrap | 3D-04 | COVERED |
| RESEARCH | hosted master/release evidence, deadlines, redaction, cleanup, no retry-to-green | 3D-05; sections 6/15..19 | COVERED |
| CONTEXT | DG-09 phase/release ownership | objectives/non-goals, 3D-05 Release route | COVERED |
| CONTEXT | DG-10 no application Docker images | non-goals, 3D-05 workflow contract | COVERED |
| CONTEXT | DG-11 hosted Ubuntu/disposable/failure gate | 3D-00/05 | COVERED |
| CONTEXT | DG-12 synthetic data/run-id isolation | section 6, 3D-00..05 | COVERED |
| CONTEXT | DG-13 first promoted artifact becomes N-1 | 3D-04/05, Release route | COVERED |
| CONTEXT | DG-14 real production transports | Interface seam, 3D-00..03 | COVERED through inherited production targets |
| CONTEXT | DG-15 schema migration evidence | 3D-04 schema business cell | COVERED through SchemaMigration |
| CONTEXT | DG-16 exact topology/journey | 3D-00..05 | COVERED |
| CONTEXT | DG-17 unified artifact belongs Release gate | non-goals, 3D-05/section 22 | COVERED by explicit route, not duplicated |
| CONTEXT | DG-18 same-source Linux Modules | prerequisites, 3D-00/05 structure gate | COVERED |
| CONTEXT | DG-19 no fake/test-only EXE seam | section 5, 3D-00/05 | COVERED |
| CONTEXT | DG-20 deterministic faults/resources; stress separate | 3D-02/03/05, non-goals | COVERED |
| CONTEXT | DG-21 real N/N-1 process combinations | 3D-04/05 | COVERED |
| CONTEXT | DG-22 same-artifact UAT/promotion belongs Release gate | 3D-05/section 22 | COVERED by explicit route |
| CONTEXT | DG-23 hosted Ubuntu authority | prerequisites, 3D-05 | COVERED |
| CONTEXT | DG-24 retry/idempotency/no exactly-once | 3D-02/03, truth/completion/threat contracts | COVERED |

Audit result: all in-scope GOAL/REQ/RESEARCH/CONTEXT items are planned. DG-17/DG-22 implementation is intentionally owned by the
separate Release gate per locked DG-09 and is not a missing Phase 3D item.

## 22. Release route

Only after section 19 is accepted, pass the exact Phase 3D master candidate SHA and evidence hashes to the separate Release plans:

1. **R-00 build once:** build one unified versioned Windows x64 set from the accepted SHA, including Gate/Status/Chat EXEs, Varify
   package, config templates, canonical proto, migrations, manifest and per-file hashes. No Phase 3D binary is reused as release proof.
2. **R-01 artifact-only automation:** downstream downloads/verifies the immutable R-00 set and runs install/config/start/upgrade/
   stop/port-release smoke plus applicable compatibility; it cannot checkout and rebuild tested executables.
3. **R-02 UAT:** after automated smoke is green, the user executes/signs the versioned checklist against the same artifact digest;
   any defect blocks and an automatable defect returns to its owning 3B/3C/3D regression gate.
4. **R-03 promotion/baseline:** promote the same bytes to durable tag/Release asset, reverify digest/evidence chain and register it as
   the next complete N-1 artifact/schema baseline. The first successful promotion converts bootstrap into a real next-cycle input.

No 3D task may rebuild, sign, UAT or promote the release set; no Release task may replace missing Phase 3D evidence with artifact
smoke alone.
