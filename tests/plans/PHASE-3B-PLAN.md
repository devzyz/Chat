---
phase: "3B"
title: "Deterministic production-transport and process Integration"
status: "Planned"
depends_on:
  - "Phase 3A complete, including 3A-01..3A-06 production Module and runner evidence"
plans: 6
waves: 4
autonomous: true
requirements: [G-008, G-009, G-010, G-011, G-015]
decisions: [DG-09, DG-10, DG-11, DG-12, DG-14, DG-19, DG-20, DG-25]
baseline:
  reports: 12
  runner_testcases: 232
  stable_develop_checks:
    - "Static configuration checks"
    - "Server Release build"
    - "Qt client Release"
    - "VarifyServer dependency and package check"
must_haves:
  truths:
    - "Gate HTTP, Status gRPC, Chat TCP, Qt QNetworkAccessManager and Qt QTcpSocket cross real production transport code on deterministic loopback."
    - "Every host uses Phase 3A production business Modules with in-memory Adapters and never reaches real Redis, MySQL, SMTP or the public network."
    - "Fragmentation/coalescing, size boundaries, malformed input, interruption, refusal, deadlines, late completion, port conflict and owned-resource cleanup have deterministic evidence."
    - "The existing 232 testcase / 12 report baseline remains intact; new actual counts are registered only after real tests exist."
    - "The four existing develop check names remain exact and the new deterministic suites are Required through their owning public runners."
  artifacts:
    - "planned tests/plans/PHASE-3B-TEST-PLAN.md"
    - "planned tests/support/RunContext.h/.cpp and ProcessHarness.h/.cpp"
    - "planned tests/server/integration-host production-transport Integration sources"
    - "planned Gate/Status/Chat shared production transport library targets"
    - "planned chat GateHttpTransport and ChatTcpTransport production Modules"
  key_links:
    - "formal EXE and IntegrationHost -> same production transport library target"
    - "production transport handler -> Phase 3A Module Interface -> in-memory Adapter in test composition"
    - "stable Test ID -> real source registration -> JUnit report -> owning public runner -> unchanged develop check"
---

# Phase 3B 正式计划：确定性 production transport / process Integration

状态：**Planned；Phase 3A 完成前禁止执行**

本文件是 3B-00..3B-05 的执行合同。所有以“planned”标识的新文件、target、Interface、symbol、Test ID
和 report 都尚未实现；它们不得被当作当前仓库事实或当前 232-case baseline 的一部分。

## 1. 目标

在不访问真实 Redis、MySQL、SMTP 或公网的前提下，把 Phase 3A 已完成的生产业务 Module 接到真实
Gate HTTP、Status gRPC、Chat TCP、Qt `QNetworkAccessManager` 与 `QTcpSocket` transport 上，通过
deterministic loopback 和受控进程证明 framing、deadline、取消、连接生命周期、完成去重以及资源释放。

3B 结束时，`develop` 的四个既有 Required Check 名称保持不变；它们在保留全部原 232 个 testcase 和
12 份报告的同时，执行新增的真实 transport/process contracts。新增总数只能由落地后的真实 runner
registration 与 JUnit manifest 得出，本计划不预填未来总数。

## 2. 硬前置与非范围

### 2.1 硬前置

- `tests/plans/PHASE-3A-PLAN.md` 必须明确 Phase 3A complete，3A-01..3A-06 均有完成 Summary；仅
  3A-00 complete 不满足前置。
- `LogicDispatcher`、`StatusRouting`、`ChatSessionState`、`GateRequest`、`AuthFlowCoordinator` 的实际
  production Interface 和 shared production target 必须存在，正式 caller 与 Phase 3A tests 已链接同一实现。
- Phase 3A 收口后的实际 report manifest、Test ID 与当前 baseline 必须一致且全绿。若上游更改了 planned
  symbol 名，以实际 Summary 为准记录映射，但不得复制实现或另建近义 Module。
- 任何前置缺失均停止 3B；不得用空 target、占位 source、fake transport 或 test-only public Interface 绕过。
- DG-25 的本机持久路径必须只读：`D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed` 不得被 3B
  构建/测试隐式修改。所有本地 MSBuild 调用显式关闭 manifest install 并使用固定 installed tree；只读 preflight
  不一致时立即停止。任何 restore/install/remove/update/upgrade、路径/triplet/baseline/tool identity 变化必须先展示
  精确命令、目标和影响并取得用户明确批准。hosted CI 只使用 run-owned 临时 install root。

### 2.1a 比例化执行与共享 phase 前置

过程分层以 [`tests/CI-GOVERNANCE.md` section 2.1](../CI-GOVERNANCE.md#21-比例化执行合同) 为唯一权威。Phase entry
一次性读取上述 Phase 3A Summary、DG-09..DG-25、G-008..G-015 ownership、当前 12-report/232-case baseline，并执行
一次 DG-25 read-only dependency preflight；task 不重复治理栈、tool hash、package identity 或 vcpkg fingerprint。
每个 task 的 `read_first` 仅保留 edited source、closest analog 与一个必要 authority。行为切片先 focused RED/GREEN，
只对可观察 transport/failure/lifecycle/gate enforcement 做 meaningful mutation；process/port/temp cleanup 只由实际创建者
负责。同一 plan 代码稳定后由最后一个 task 运行 owning public runner 一次；3B-05-T3 独占一次 `RunAllTests`，并聚合
report/secret/residue/diff/remote CI evidence。纯 registration/prose/static links 不做 mutation；结构注册只在 closeout 执行
一次隔离 negative probe。

### 2.2 明确非范围

- 真实 Redis/MySQL/SMTP Adapter、schema/migration、Linux native build/run 和真实四进程依赖：Phase 3C。
- 双 ChatServer/双客户端好友、私聊、重连和历史业务旅程：Phase 3D。
- N/N-1 artifact 程序矩阵、统一 Windows x64 release set、UAT 与 promotion：3C/3D/Release gate。
- 应用 Docker 镜像、容器编排、production Docker 部署或 Linux 正式发布支持（DG-10）。
- 随机 fuzz、负载和长时间 soak；它们只进入 scheduled/explicit lane（DG-20）。
- 新协议、认证/密码/TLS 产品范围，以及对互联网 production security 的声明。

## 3. Locked decisions 与 Gap ownership

| Contract | 本阶段不可协商的执行含义 |
| --- | --- |
| DG-09 | 只做无真实 Redis/MySQL/SMTP 的 transport/process Integration；四进程真实依赖不得提前进入 3B。 |
| DG-10 | 不创建 Gate/Status/Chat/Varify 应用 Docker 镜像。 |
| DG-11 | 新 3B suite 进入 GitHub hosted Windows `develop` gate；不新增 self-hosted Required runner；不可用、超时、报告或 cleanup 缺失均失败。 |
| DG-12 | 数据全部 synthetic；每次 RunContext 生成唯一 run-id，并隔离端口、临时目录、身份与日志。 |
| DG-14 | Gate HTTP、Status gRPC、Chat TCP、Qt QNAM/QTcpSocket 都必须穿过真实 production transport；内部使用 3A Module + in-memory Adapter。 |
| DG-19 | IntegrationHost 是测试 composition root，不是第二套 server；正式 EXE 无 fake-dependency flag、测试宏或 test-only Interface；composition wiring 由结构门禁保护。 |
| DG-20 | Required faults 覆盖 fragmentation/coalescing、max boundary、malformed、interrupt、refused、deadline、duplicate/late completion、port conflict 与完整资源释放。 |
| DG-25 | 本机 vcpkg 工具与 package install tree 默认只读；本地 runner fail closed，禁用 manifest 自动安装；任何 package/path 变更逐次审批。hosted CI 仅使用隔离临时 root。 |

Gap 状态在本阶段开始和执行期间保持 open/planned：G-008 的真实 Chat session/TCP、G-009 的 Gate HTTP、
G-010 的 Status gRPC transport extension、G-011 的 Qt HTTP/TCP、G-015 的无真实依赖 process/composition
部分由 3B 负责。只有 3B-05 的全部 completion criteria 通过后，执行者才可按矩阵规则更新这些 Gap 的
“3B 部分”状态；不得提前写 complete，也不得关闭 G-012..G-018。

## 4. Module / Interface / seam 合同

### 4.1 共同原则

- 每个 caller 与 test 共用一个 production Interface。测试通过 Interface 的输入、结果、错误、顺序、timeout
  与 lifecycle 观察行为，不读取 private queue/map/socket flag。
- Phase 3A business Module 保持算法唯一 owner；3B transport 只负责 bytes/messages、deadline、取消和连接
  lifecycle。IntegrationHost 只负责 composition，不复制 parser、serializer、route、selection 或业务判断。
- 只有 production Adapter 与 in-memory/local Adapter 两个真实用途同时存在时才建立 internal port。纯
  in-process 行为直接调用 Module Interface。
- 正式 EXE 与 Integration tests 必须 `ProjectReference`/`target_link_libraries` 同一 production library target；
  禁止 test project 再列一份 production `.cpp` 或用不同 compile definitions 编译第二份实现。

### 4.2 Planned Interfaces（不是当前事实）

| Planned Module / Interface | Caller 与 test 可知内容 | 隐藏的 Implementation | seam / Adapter |
| --- | --- | --- | --- |
| `integration::RunContext` | immutable run-id、loopback endpoint bundle、temp root、deadline、owned-resource registration、teardown result | port reservation、path ownership、ledger ordering、evidence paths | In-process test infrastructure；不暴露 OS handles |
| `integration::ProcessHarness` | `Start(ProcessSpec)`、`WaitReady(Probe, deadline)`、`Stop(deadline)`、`CollectEvidence()` | process group、pipe drain、graceful signal、bounded escalation、identity check | internal `ProcessAdapter`; 3B `Win32ProcessAdapter`，3C planned POSIX Adapter |
| `integration::IntegrationHostFactory` | create Gate/Status/Chat host from loopback config + concrete Phase 3A dependencies，return endpoint/ready/stop | service construction order、thread/io_context ownership、reverse teardown | test composition only；production transports + 3A Modules + existing in-memory Adapters |
| Gate existing `CServer` deepened Interface | `Start()`、actual bound endpoint、`Stop()`；real HTTP request reaches `GateRequest::Handle` | Beast accept/read/write、body bound、connection cancel | GateRequest dependency ports use Phase 3A in-memory Adapters in IntegrationHost；正式 EXE uses production Adapters |
| planned `status::StatusGrpcServer` | construct with `StatusRouting` dependency、start/bound endpoint/stop；generated stub remains protocol Interface | `ServerBuilder`、completion/lifecycle、deadline/cancel | same production service object；in-memory Status store only behind Phase 3A internal seam |
| existing Chat `CServer` deepened Interface | start/bound endpoint/stop；bytes traverse production acceptor/session/frame/dispatcher | accept loop、session ownership、partial reads/writes、send queue | Phase 3A `LogicDispatcher`/`ChatSessionState` + in-memory writer/presence/business dependencies |
| planned Qt `GateHttpTransport` | submit request with flow/request identity、finite deadline、one terminal result、cancel/reset | QNAM/QNetworkReply ownership、late completion suppression | loopback HTTP peer is remote protocol endpoint；GUI and tests link same Module |
| planned Qt `ChatTcpTransport` | connect/send/close/reset、frames/results、finite connect/write deadline、generation-scoped completion | QTcpSocket、TcpFrameDecoder、partial write queue、late signal suppression | loopback TCP peer is remote protocol endpoint；`TcpMgr` delegates instead of exposing socket/container |

`GateHttpTransport` 和 `ChatTcpTransport` 是 planned production deep Modules，不是为测试公开 `HttpMgr::_manager`、
`TcpMgr::_socket` 或 `_handlers`。落地时 GUI manager/dialog 适配它们，测试也只调用相同 Interface。

## 5. Planned Test ID / report namespace

Test ID 是 planned behavior contract，不等于 runner testcase。只有测试 source、target 和实际 JUnit case 均存在后，
3B-05 才更新 runner manifest 的真实每报告数量与 aggregate。现有 232 个 testcase 和 12 个报告不得减少、改名
或被新测试替代。

| Plan | Planned Test ID range | Contract family | Planned owning report |
| --- | --- | --- | --- |
| 3B-00 | `T09-HOST-01..06` | shared target / IntegrationHost composition contract | existing `server_integration.xml` |
| 3B-01 | `T09-PROC-01..12` | RunContext, process ready/stop/evidence/cleanup | existing `server_integration.xml` |
| 3B-02 | `T09-GHTTP-01..12` | production Gate HTTP loopback/faults | existing `server_integration.xml` |
| 3B-02 | `Q04-HTTP-01..10` | production Qt QNAM loopback/faults | planned `client_integration.xml` |
| 3B-03 | `T09-SGRPC-01..12` | production Status gRPC loopback/faults | existing `server_integration.xml` |
| 3B-04 | `T09-CTCP-01..16` | production Chat TCP loopback/faults | existing `server_integration.xml` |
| 3B-04 | `Q04-TCP-01..12` | production Qt QTcpSocket loopback/faults | planned `client_integration.xml` |
| 3B-05 | `T09-COMP-01..08` | formal composition/lifecycle/registration integrity | existing `server_integration.xml` + structure gate |

新增 `client_integration.xml` 只在真实 Qt Integration CTest 已注册并产生报告时加入 manifest；其引入不允许删除
或合并旧 12 reports。若 executor 发现现有 runner 已建立等价 Integration report，必须遵守 D-04 做去重映射，
不能创建同义报告或擅自改名。

## 6. Wave / dependency graph

| Wave | Plan | Objective | Depends on |
| ---: | --- | --- | --- |
| 1 | 3B-00 | 冻结 shared target、IntegrationHost 与 Test ID/report 合同 | Phase 3A complete |
| 2 | 3B-01 | RunContext、Win32 ProcessHarness、ready/evidence/cleanup | 3B-00 |
| 3 | 3B-02 | Gate HTTP + Qt QNAM | 3B-01 |
| 3 | 3B-03 | Status gRPC | 3B-01 |
| 3 | 3B-04 | Chat TCP + Qt QTcpSocket | 3B-01 |
| 4 | 3B-05 | production composition、manifest、develop CI closeout | 3B-02, 3B-03, 3B-04 |

3B-02/03/04 可逻辑并行，但共享工作树执行时不得并发修改 `Chat.sln`、
`tests/server/ServerIntegrationTests.vcxproj`、`chat/CMakeLists.txt`、runner、matrix 或 workflow；执行器按文件
ownership 串行合并这些共享注册文件。

## 7. Plan 3B-00 — Contract / shared-target preflight

状态：**Complete（2026-09-04）**。完成证据见
[`PHASE-3B-00-SUMMARY.md`](PHASE-3B-00-SUMMARY.md)；下一项为 **Plan 3B-01**。

<objective>
确认 Phase 3A 真实 production Modules 已完成并冻结 IntegrationHost 的最小 Interface、planned Test ID、report
与 target ownership，使后续 transport plan 无需探索或复制上游实现。

Purpose: DG-19 的最难约束是 production EXE 和 IntegrationHost 必须复用同一 transport/business targets；先让
结构 contract 可失败，再允许任何 transport case 落地。

Output: planned `PHASE-3B-TEST-PLAN.md`、非空 integration-host test Module、shared composition contract 与
T09-HOST registration。
</objective>

<tasks>

<task type="auto">
  <name>Task 3B-00-1: Verify Phase 3A completion and freeze the 3B test contract</name>
  <read_first>
    tests/plans/PHASE-3A-PLAN.md
    tests/plans/PHASE-3A-TEST-PLAN.md
    tests/plans/PHASE-3A-06-SUMMARY.md (planned upstream completion artifact; must exist at execution)
  </read_first>
  <files>
    tests/plans/PHASE-3B-TEST-PLAN.md (planned new)
    tests/TEST-CONTRACT-MATRIX.md
  </files>
  <action>
Stop immediately unless Phase 3A is explicitly complete and the five upstream production Interfaces are compiled by shared
production targets used by both their formal callers and tests. Record the actual upstream target names and header paths in
`PHASE-3B-TEST-PLAN.md`; map them to `LogicDispatcher`, `StatusRouting`, `ChatSessionState`, `GateRequest`, and
`AuthFlowCoordinator` without renaming or wrapping them for cosmetic consistency. Register the exact planned ID ranges from
section 5 as planned-only, with Domain/Level, timeout, report family, RED trigger, mutation and remaining 3C/3D Gap. Preserve
the current matrix counts and open Gap states; do not create empty future test directories.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task CheckTestStructure</automated>
  </verify>
  <acceptance_criteria>
Phase 3A complete evidence and actual target/header mappings are recorded; every 3B Test ID is unique and explicitly planned;
232/12 and G-008..G-018 current states are unchanged; no empty directory, source stub or placeholder test exists.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-00-2: Establish the IntegrationHost composition contract through one shared target path</name>
  <read_first>
    tests/plans/PHASE-3B-TEST-PLAN.md (planned output of Task 3B-00-1)
    GateServer/GateServer/GateServer.vcxproj
    StatusServer/StatusServer/StatusServer.vcxproj
  </read_first>
  <files>
    tests/support/IntegrationHostFactory.h (planned new)
    tests/support/IntegrationHostFactory.cpp (planned new)
    tests/server/integration-host/integration_host_contract_tests.cpp (planned new, non-empty)
    tests/server/integration-host/README.md (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED first with T09-HOST-01..06: require a host spec to accept only loopback endpoints, concrete Phase 3A dependency objects and
an owned deadline; require returned host handles to expose actual bound endpoint, protocol-ready probe and idempotent stop;
require destruction to report cleanup rather than silently detach. Then implement planned `integration::IntegrationHostFactory`
as a test composition root that constructs the production transport objects and upstream production Modules with their existing
in-memory Adapters. It may own construction and teardown only; it must not parse payloads, select servers, map business errors,
expose private containers or add fake behavior to formal EXEs. Register the real source in `ServerIntegrationTests.vcxproj` only
after the RED compiles against the intended Interface. Mutation: replace one production target reference with a duplicate source
compile or accept a non-loopback endpoint; the focused contract/structure gate must fail, then restore GREEN.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_HOST_* --gtest_output=xml:build/test-results/3b00_host_focused.xml</automated>
  </verify>
  <acceptance_criteria>
T09-HOST focused cases pass through a test-only composition root using the actual Phase 3A production Interfaces; no production
algorithm is duplicated; non-loopback configuration, detached teardown and duplicate source ownership are rejected.
  </acceptance_criteria>
</task>

<task type="auto">
  <name>Task 3B-00-3: Wire contract registration to the owning Server runner</name>
  <read_first>
    tests/server/integration-host/README.md (planned output)
    tests/server/ServerIntegrationTests.vcxproj
    scripts/windows-local.ps1
  </read_first>
  <files>
    scripts/windows-local.ps1
    tests/TEST-CONTRACT-MATRIX.md
    tests/server/integration-host/README.md
  </files>
  <action>
Add the actual T09-HOST runner cases to the existing `server_integration.xml` ownership. Update only the actual observed report
count after the executable emits JUnit; do not fill unused planned IDs or infer total cases from the range. Extend
`CheckTestStructure` so `ServerIntegrationTests` must reference the same upstream production libraries as the formal EXEs and
must not compile their `.cpp` files directly. Run the focused case first, then the owning public `RunServerTests`; do not run
`RunAllTests` in this plan.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
The focused report and owning Server runner are green; the static structure rule is registered for the 3B-05 closeout probe;
all pre-existing Server cases remain registered and no other public runner is invoked here.
  </acceptance_criteria>
</task>

</tasks>

## 8. Plan 3B-01 — RunContext and Windows ProcessHarness

<objective>
Create the deterministic runtime owner used by every later 3B test: unique resources, protocol-ready process control, bounded
evidence and identity-scoped cleanup.

Purpose: Make timeouts and teardown observable contracts rather than per-test best effort, covering the lifecycle portion of
DG-12/DG-19/DG-20 before transport suites multiply processes and sockets.

Output: planned RunContext/ProcessHarness/Win32ProcessAdapter production-quality test infrastructure and T09-PROC evidence.
</objective>

<tasks>

<task type="auto" tdd="true">
  <name>Task 3B-01-1: Own every run resource through RunContext</name>
  <read_first>
    tests/plans/PHASE-3B-TEST-PLAN.md (planned)
    tests/server/startup/startup_config_tests.cpp
    tests/server/startup/gate_status_startup_tests.cpp
  </read_first>
  <files>
    tests/support/RunContext.h (planned new)
    tests/support/RunContext.cpp (planned new)
    tests/server/process-harness/run_context_tests.cpp (planned new)
    tests/server/process-harness/README.md (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED T09-PROC-01..05 for an immutable random run-id, unique loopback port bundle, run-owned temp root, synthetic identity namespace,
absolute deadline, process/socket registration and reverse-order teardown ledger. `RunContext` must distinguish pre-existing state
from resources it created, refuse paths outside its owned temp root, preserve the primary failure separately from cleanup failures,
and never delete an unregistered path or kill an unregistered identity. GREEN with event/future synchronization and no fixed sleep.
Mutation: reuse a run-id, register the same port twice or adopt a pre-existing directory; focused tests must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_PROC_RunContext* --gtest_output=xml:build/test-results/3b01_run_context_focused.xml</automated>
  </verify>
  <acceptance_criteria>
Every owned resource has run-id, identity and deadline; teardown ledger is complete and reverse ordered; pre-existing/user state is
never adopted or removed; all waits are bounded.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-01-2: Start, probe and stop child processes with the Win32 Adapter</name>
  <read_first>
    tests/support/RunContext.h (planned output)
    tests/server/startup/startup_config_tests.cpp
    tests/server/startup/gate_status_startup_tests.cpp
  </read_first>
  <files>
    tests/support/ProcessHarness.h (planned new)
    tests/support/ProcessHarness.cpp (planned new)
    tests/support/Win32ProcessAdapter.h (planned new)
    tests/support/Win32ProcessAdapter.cpp (planned new)
    tests/server/process-harness/process_harness_tests.cpp (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED T09-PROC-06..10 around `Start(ProcessSpec)`, `WaitReady(Probe, deadline)`, `Stop(deadline)` and `CollectEvidence()`: startup exit
must retain bounded stdout/stderr, PID plus creation-time identity and exit code; readiness must be a protocol probe rather than PID,
sleep or log text; stop must send the documented graceful signal, wait to deadline, then terminate only the still-matching owned
identity and report escalation. GREEN using `Win32ProcessAdapter`; keep OS handles internal so 3C can add a POSIX Adapter without
changing ProcessHarness Interface. Mutation: treat PID-alive as ready or kill a PID with mismatched creation time; tests must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_PROC_ProcessHarness* --gtest_output=xml:build/test-results/3b01_process_focused.xml</automated>
  </verify>
  <acceptance_criteria>
Startup, ready, graceful stop, bounded escalation and evidence are independently observable; stale PID identity is never killed;
all pipe reader threads and handles are joined/closed by the deadline.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-01-3: Prove deadline, port-conflict and cleanup failure propagation</name>
  <read_first>
    tests/support/ProcessHarness.h (planned output)
    tests/support/RunContext.h (planned output)
    tests/CI-GOVERNANCE.md
  </read_first>
  <files>
    tests/server/process-harness/process_harness_fault_tests.cpp (planned new)
    tests/server/process-harness/README.md
    scripts/windows-local.ps1
    tests/TEST-CONTRACT-MATRIX.md
  </files>
  <action>
RED T09-PROC-11..12 plus focused cases for refused readiness, expired deadline, deliberately occupied port, child ignoring graceful
stop, late pipe output and teardown failure. The original failure and cleanup failure must both be present in sanitized evidence;
cleanup failure returns nonzero. Prove a released port can be rebound and no child process, reader thread, socket, handle or run temp
directory remains. Mutate cleanup to ignore one ledger entry; focused test and residue guard must fail. Restore, run focused first,
then the owning public Server runner; do not use automatic retry to turn RED green.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
All T09-PROC contracts pass; failure evidence is bounded and secret-free; cleanup failures gate the runner; owned ports/processes/
threads/temp paths are released and pre-existing state is untouched.
  </acceptance_criteria>
</task>

</tasks>

## 9. Plan 3B-02 — Gate HTTP and Qt QNetworkAccessManager

<objective>
Drive real HTTP bytes through the production Gate Beast transport into Phase 3A `GateRequest`, and drive the production Qt HTTP
Module through real QNetworkAccessManager loopback, with deterministic HTTP faults and cleanup.

Purpose: Close the Gate and client-HTTP transport portions of G-009/G-011 without touching real dependency Adapters.

Output: shared Gate transport target, planned Qt GateHttpTransport, Integration tests T09-GHTTP/Q04-HTTP and runner evidence.
</objective>

<tasks>

<task type="auto" tdd="true">
  <name>Task 3B-02-1: Make Gate HTTP transport a shared production target and test its real loopback path</name>
  <read_first>
    GateServer/GateServer/CServer.h
    GateServer/GateServer/CServer.cpp
    GateServer/GateServer/HttpConnection.h
  </read_first>
  <files>
    GateServer/GateServer/GateTransport.vcxproj (planned new shared production target)
    GateServer/GateServer/CServer.h
    GateServer/GateServer/CServer.cpp
    GateServer/GateServer/HttpConnection.h
    GateServer/GateServer/HttpConnection.cpp
    GateServer/GateServer/LogicSystem.cpp
    GateServer/GateServer/GateServer.vcxproj
    Chat.sln
    tests/server/integration-host/gate_http_transport_tests.cpp (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED T09-GHTTP-01..07 with real loopback HTTP for protocol ready, each of the four production POST routes reaching the same
`GateRequest::Handle`, fragmented request bytes, maximum accepted body, one-byte-over/malformed request rejection and client
disconnect during request/response. Refactor ownership so existing Beast server/connection sources are compiled once by planned
`GateTransport.vcxproj`; formal `GateServer.vcxproj` and `ServerIntegrationTests.vcxproj` reference it. Deepen existing `CServer`
rather than wrap it: expose start, actual bound loopback endpoint and idempotent stop while keeping accept/session containers private.
IntegrationHost passes Phase 3A in-memory verification/code/user/status Adapters; route and `GateResponse` remain production owners.
Mutation: bypass `GateRequest`, duplicate JSON parsing or compile a transport `.cpp` in the test project; behavioral/structure tests
must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_GHTTP_Core* --gtest_output=xml:build/test-results/3b02_gate_http_focused.xml</automated>
  </verify>
  <acceptance_criteria>
Real HTTP bytes cross production Beast code and Phase 3A GateRequest; max/malformed/interrupted paths terminate once with stable
sanitized errors; formal EXE and test share one production target; no Redis/MySQL/gRPC/public endpoint is contacted.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-02-2: Deepen Qt HTTP into GateHttpTransport and test QNAM loopback</name>
  <read_first>
    chat/httpmgr.h
    chat/httpmgr.cpp
    chat/CMakeLists.txt
  </read_first>
  <files>
    chat/gatehttptransport.h (planned new)
    chat/gatehttptransport.cpp (planned new)
    chat/httpmgr.h
    chat/httpmgr.cpp
    chat/CMakeLists.txt
    chat/tests/http-transport/http_transport_tests.cpp (planned new)
    chat/tests/http-transport/README.md (planned new)
  </files>
  <action>
RED Q04-HTTP-01..10 for QNAM success, refused connection, finite deadline, malformed response, maximum response bound, peer closing
mid-response, explicit cancel, duplicate finished signal defense, late reply from an old flow and QObject/reply cleanup. Create the
planned production `GateHttpTransport` library Interface with request/flow identity, finite deadline, cancel/reset and exactly one
terminal outcome. Move QNAM/QNetworkReply ownership from `HttpMgr` behind that Interface; `HttpMgr` and tests link the same library
and feed outcomes to Phase 3A AuthFlowCoordinator. Do not expose QNetworkReply, `_manager`, handler maps or a test reset hook.
Mutation: remove generation check or emit a second completion; focused tests must fail.
  </action>
  <verify>
    <automated>ctest --test-dir build/windows-client/Release -R "http_transport" --output-on-failure</automated>
  </verify>
  <acceptance_criteria>
Q04-HTTP crosses real QNetworkAccessManager loopback; refused/deadline/cancel/late completion are stable single outcomes; all replies,
timers and loopback sockets are released; GUI caller and tests share `GateHttpTransport`.
  </acceptance_criteria>
</task>

<task type="auto">
  <name>Task 3B-02-3: Register HTTP reports, mutations and owning runners</name>
  <read_first>
    tests/server/integration-host/gate_http_transport_tests.cpp (planned)
    chat/tests/http-transport/http_transport_tests.cpp (planned)
    scripts/windows-local.ps1
  </read_first>
  <files>
    scripts/windows-local.ps1
    tests/TEST-CONTRACT-MATRIX.md
    tests/server/integration-host/README.md
    chat/tests/http-transport/README.md
  </files>
  <action>
Record actual T09-GHTTP and Q04-HTTP runner cases and observed JUnit counts. Add real Qt Integration CTest to planned
`client_integration.xml` only now that cases exist; preserve every old report/count entry. Run explicit mutations for body bound,
GateRequest bypass and late QNAM completion; each focused selector must return nonzero before restoration. After both focused suites,
run `RunServerTests` as this plan's owning public runner. The Qt focused CTest is sufficient until the 3B-05 full lane; do not run
aggregate `RunAllTests` or rename the existing Server/Qt check jobs.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
Both focused suites and the owning Server public runner are green; actual counts, IDs, README, matrix and reports agree; old 232 cases
and 12 reports remain present; HTTP logs/reports contain no synthetic code/token/password/email marker.
  </acceptance_criteria>
</task>

</tasks>

## 10. Plan 3B-03 — Status gRPC

<objective>
Run the production Status gRPC service and generated stub over real loopback while delegating selection/token behavior to Phase 3A
`StatusRouting` with its in-memory store Adapter.

Purpose: Cover the gRPC transport, completion and lifecycle extension of G-010/G-015 without re-testing or copying routing logic.

Output: shared Status transport target, planned StatusGrpcServer Interface, T09-SGRPC tests and owning Server evidence.
</objective>

<tasks>

<task type="auto" tdd="true">
  <name>Task 3B-03-1: Extract one production Status gRPC server target and prove the success path</name>
  <read_first>
    StatusServer/StatusServer/StatusServer.cpp
    StatusServer/StatusServer/StatusServiceImpl.h
    StatusServer/StatusServer/StatusServiceImpl.cpp
  </read_first>
  <files>
    StatusServer/StatusServer/StatusGrpcServer.h (planned new)
    StatusServer/StatusServer/StatusGrpcServer.cpp (planned new)
    StatusServer/StatusServer/StatusTransport.vcxproj (planned new shared production target)
    StatusServer/StatusServer/StatusServer.cpp
    StatusServer/StatusServer/StatusServiceImpl.h
    StatusServer/StatusServer/StatusServiceImpl.cpp
    StatusServer/StatusServer/StatusServer.vcxproj
    Chat.sln
    tests/server/integration-host/status_grpc_transport_tests.cpp (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED T09-SGRPC-01..05 for protocol ready, real generated `GetChatServer`/`Login` stub success, empty-list fail-closed, maximum valid
request field boundary and malformed/invalid business input mapping without exception text. Create planned `status::StatusGrpcServer`
with construction from a concrete Phase 3A `StatusRouting` dependency and start/actual endpoint/idempotent stop. Move server/service
sources into one `StatusTransport.vcxproj`; formal EXE and Integration tests reference it. `StatusServiceImpl` becomes a thin gRPC
Adapter that delegates to the same StatusRouting Interface; it must not retain `begin()->second` or token-store business logic.
Mutation: restore direct selection or ignore routing failure; focused tests must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_SGRPC_Core* --gtest_output=xml:build/test-results/3b03_status_core.xml</automated>
  </verify>
  <acceptance_criteria>
Generated stubs cross real gRPC transport into production StatusRouting; business errors remain Phase 3A-owned; one production target
serves EXE and tests; no real Redis is created or contacted.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-03-2: Cover gRPC deadline, cancel, refusal, late completion and port ownership</name>
  <read_first>
    StatusServer/StatusServer/StatusGrpcServer.h (planned)
    common/grpc/GrpcClientRuntime.h
    tests/support/RunContext.h (planned)
  </read_first>
  <files>
    tests/server/integration-host/status_grpc_transport_tests.cpp
    tests/server/integration-host/README.md
  </files>
  <action>
RED T09-SGRPC-06..12 for deadline expiry, explicit cancellation, refused connection, server shutdown during call, duplicate/late
completion after host generation changes, occupied port startup failure and complete port/thread/completion-queue release. Use real
generated stub and production service; control delay only through the existing Phase 3A in-memory Adapter/fault schedule, never by
adding a test RPC or sleep. Each ClientContext has a hard deadline; late callbacks cannot mutate stopped/new host state. Mutation:
remove deadline, reuse a stopped completion or suppress bind failure; focused cases must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_SGRPC_Fault* --gtest_output=xml:build/test-results/3b03_status_faults.xml</automated>
  </verify>
  <acceptance_criteria>
Every fault completes within its deadline with stable sanitized status; duplicate/late completion has no second effect; stopped hosts
leave no thread, completion queue, socket or port and an occupied port remains owned by the original process only.
  </acceptance_criteria>
</task>

<task type="auto">
  <name>Task 3B-03-3: Register Status transport contracts and run the owning Server lane</name>
  <read_first>
    tests/server/integration-host/status_grpc_transport_tests.cpp (planned)
    tests/server/ServerIntegrationTests.vcxproj
    scripts/windows-local.ps1
  </read_first>
  <files>
    scripts/windows-local.ps1
    tests/TEST-CONTRACT-MATRIX.md
    tests/server/integration-host/README.md
  </files>
  <action>
Register only actual T09-SGRPC cases in `server_integration.xml` and update its observed count. Add structure assertions that formal
StatusServer and tests reference `StatusTransport.vcxproj`, while tests do not compile Status production `.cpp` directly. Record
and restore the required mutations, run focused core/fault selectors, then public `RunServerTests`. Preserve proto, public error
numbers, prior gRPC client/pool tests and all four check names; do not run the full aggregate.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
T09-SGRPC IDs, source, target, JUnit and manifest agree; owning runner is green; existing Status/gRPC contracts do not regress;
structure mutation disconnecting the shared target fails.
  </acceptance_criteria>
</task>

</tasks>

## 11. Plan 3B-04 — Chat TCP and Qt QTcpSocket

<objective>
Drive real TCP byte streams through the production Chat acceptor/session/frame/dispatcher path and the production Qt TCP Module,
covering DG-20 stream faults, backpressure, connection generations and cleanup.

Purpose: Close the transport portions of G-008/G-011/G-015 while keeping Redis/MySQL/peer RPC behind Phase 3A in-memory Adapters.

Output: shared Chat transport target, planned ChatTcpTransport, T09-CTCP/Q04-TCP tests and owning runner evidence.
</objective>

<tasks>

<task type="auto" tdd="true">
  <name>Task 3B-04-1: Share the production Chat TCP path and verify stream/framing lifecycle</name>
  <read_first>
    ChatServer/ChatServer/CServer.h
    ChatServer/ChatServer/CServer.cpp
    ChatServer/ChatServer/CSession.h
  </read_first>
  <files>
    ChatServer/ChatServer/ChatTransport.vcxproj (planned new shared production target)
    ChatServer/ChatServer/CServer.h
    ChatServer/ChatServer/CServer.cpp
    ChatServer/ChatServer/CSession.h
    ChatServer/ChatServer/CSession.cpp
    ChatServer/ChatServer/ChatServer.vcxproj
    Chat.sln
    tests/server/integration-host/chat_tcp_transport_tests.cpp (planned new)
    tests/server/ServerIntegrationTests.vcxproj
  </files>
  <action>
RED T09-CTCP-01..10 using raw loopback bytes for protocol ready, split header, split body, coalesced adjacent frames, zero body,
maximum legal body, one-byte-over/malformed header, read interruption, write interruption/backpressure and exact frame order. Move
production acceptor/session/frame sources into one planned `ChatTransport.vcxproj`; formal ChatServer and tests reference it. Deepen
existing `CServer`/`CSession` Interfaces for start/actual endpoint/idempotent stop and observable accepted/full/closed send/submit
outcomes established in Phase 3A; do not wrap them or expose socket/map/queue. IntegrationHost composes Phase 3A LogicDispatcher and
ChatSessionState with their in-memory dependencies; do not invoke real Redis/MySQL/Status/peer RPC. Mutation: restore send-capacity
off-by-one, continue accept after stop or bypass validated frame decode; focused tests must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_CTCP_Stream* --gtest_output=xml:build/test-results/3b04_chat_stream.xml</automated>
  </verify>
  <acceptance_criteria>
Raw bytes traverse the exact production acceptor/session/frame/dispatcher code; frames are ordered once; max/malformed/interrupted
paths close deterministically; no external dependency manager creates a network connection.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-04-2: Deepen Qt TCP into ChatTcpTransport and verify generations/retry-safe completion</name>
  <read_first>
    chat/tcpmgr.h
    chat/tcpmgr.cpp
    chat/tcpframedecoder.h
  </read_first>
  <files>
    chat/chattcptransport.h (planned new)
    chat/chattcptransport.cpp (planned new)
    chat/tcpmgr.h
    chat/tcpmgr.cpp
    chat/CMakeLists.txt
    chat/tests/tcp-transport/tcp_transport_tests.cpp (planned new)
    chat/tests/tcp-transport/README.md (planned new)
  </files>
  <action>
RED Q04-TCP-01..12 for real QTcpSocket connect/send/read, fragmented/coalesced frames, maximum/malformed frame, refused connect,
finite connect/write deadline, peer close mid-write, connection reset discarding a half frame, late completion from old generation,
exactly one terminal outcome and QObject/socket/timer cleanup. Create planned production `ChatTcpTransport` with connect/send/close/
reset and generation-scoped outcomes; use the existing production `TcpFrameDecoder` rather than copy it. Refactor `TcpMgr` to
delegate socket/write queue ownership while retaining higher business handlers until their owning Module moves; make `_socket`,
`_handlers` and pending queues private rather than test surfaces. Feed auth outcomes to Phase 3A AuthFlowCoordinator. Mutation:
accept a late old-generation frame or clear retry identity before terminal outcome; focused test must fail.
  </action>
  <verify>
    <automated>ctest --test-dir build/windows-client/Release -R "tcp_transport" --output-on-failure</automated>
  </verify>
  <acceptance_criteria>
Q04-TCP uses real QTcpSocket and production decoder/write path; refusal/deadline/reset/late completion are bounded and generation
safe; no public internal container/test reset exists; all owned QObjects, sockets and timers are released.
  </acceptance_criteria>
</task>

<task type="auto" tdd="true">
  <name>Task 3B-04-3: Complete Chat/Qt fault matrix, mutations and runner ownership</name>
  <read_first>
    tests/server/integration-host/chat_tcp_transport_tests.cpp (planned)
    chat/tests/tcp-transport/tcp_transport_tests.cpp (planned)
    scripts/windows-local.ps1
  </read_first>
  <files>
    tests/server/integration-host/chat_tcp_transport_tests.cpp
    tests/server/integration-host/README.md
    chat/tests/tcp-transport/tcp_transport_tests.cpp
    chat/tests/tcp-transport/README.md
    scripts/windows-local.ps1
    tests/TEST-CONTRACT-MATRIX.md
  </files>
  <action>
RED remaining T09-CTCP-11..16 for refused connection, deadline, duplicate/late write completion, occupied port, stop during accept and
complete release of acceptor/session/io threads/sockets/port/temp root. Register actual server cases in `server_integration.xml` and
actual Qt cases in `client_integration.xml`; update counts from emitted JUnit only. Execute and restore mutations for max length,
partial write, old-generation completion and stop-with-pending-accept. Run focused suites first, then `RunServerTests` as the plan's
owning public runner; the Qt focused CTest is sufficient until 3B-05. Never auto-retry and do not run `RunAllTests` yet.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunServerTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
The complete Chat/Qt DG-20 matrix is green through owning runners; every mutation is proven RED before restoration; ports and all
owned runtime objects are released; prior frame/session/model contracts remain unchanged and green.
  </acceptance_criteria>
</task>

</tasks>

## 12. Plan 3B-05 — Composition, report and develop CI closeout

<objective>
Protect formal production composition roots, converge real Test ID/report counts, run the phase's only complete local lane and make
the unchanged four Windows check names authoritative for the new deterministic transport suites.

Purpose: Prevent a green test host from drifting away from the production EXEs and close the evidence chain from Interface to
Required `develop` result.

Output: T09-COMP evidence, exact post-3B manifest, structure/secret/cleanup gates, clean PR and post-merge develop proof.
</objective>

<tasks>

<task type="auto" tdd="true">
  <name>Task 3B-05-1: Black-box formal composition roots without fake dependency modes</name>
  <read_first>
    GateServer/GateServer/GateServer.cpp
    StatusServer/StatusServer/StatusServer.cpp
    ChatServer/ChatServer/ChatServer.cpp
  </read_first>
  <files>
    tests/server/integration-host/production_composition_tests.cpp (planned new)
    tests/server/integration-host/README.md
    tests/server/ServerIntegrationTests.vcxproj
    scripts/windows-local.ps1
  </files>
  <action>
RED T09-COMP-01..06 for the three formal C++ EXEs: real config precedence and validation, protocol-level ready where the executable
can reach ready without a live external service, documented graceful signal, bounded stop and port release; dependency refusal must
be stable and must not contact public endpoints. Preserve existing startup tests and extend rather than duplicate them. Do not add
`--fake-dependencies`, Adapter selector flags, test compile macros or test-only public Interfaces. Add structure checks that each
formal EXE references the same Gate/Status/Chat transport target and Phase 3A business target used by IntegrationHost, and that
production composition chooses real Adapters. For any formal ready path that intrinsically requires a disposable real service,
record an explicit G-015/3C stop result instead of introducing a fake; do not claim that path complete in 3B. Mutation: disconnect
one formal target reference or insert a fake-mode token; structure gate must fail.
  </action>
  <verify>
    <automated>build\windows-tests\Release\server_integration_tests.exe --gtest_filter=T09_COMP_* --gtest_output=xml:build/test-results/3b05_composition.xml</automated>
  </verify>
  <acceptance_criteria>
Formal composition roots are black-box protected to the no-real-dependency boundary; production/test target wiring is identical;
fake modes/macros are absent; any real-dependency-only lifecycle remains explicitly routed to 3C and is not mislabeled green.
  </acceptance_criteria>
</task>

<task type="auto">
  <name>Task 3B-05-2: Converge actual manifest, reports, cleanup and secret-safe evidence</name>
  <read_first>
    scripts/windows-local.ps1
    tests/CI-GOVERNANCE.md
    tests/REGRESSION.md
  </read_first>
  <files>
    scripts/windows-local.ps1
    tests/CI-GOVERNANCE.md
    tests/REGRESSION.md
    tests/TEST-CONTRACT-MATRIX.md
    .github/workflows/windows-ci.yml
    tests/plans/PHASE-3B-SUMMARY.md (planned new after execution)
  </files>
  <action>
Enumerate actual registered 3B runner testcases from real sources/reports and update every affected per-report expected count plus
aggregate; retain all original 232 cases and 12 reports, add `client_integration.xml` only if actual Qt Integration cases emit it,
and never infer count from ID range. Extend residue/teardown checks for all 3B run-id prefixes and fail on missing report, failure,
error, timeout, unavailable runner or cleanup ledger entry. Scan XML/stdout/stderr and added diff lines for synthetic password/token/
verification-code/email markers without reading or echoing real secret values. Keep exact job/check names and `if: always()` plus
`if-no-files-found: error`; forbid `continue-on-error`. Run `CheckTestStructure` and `CheckTestReports` once. Perform one reversible
closeout probe: remove one newly required report registration when 3B adds a report, otherwise disconnect one production-library
registration; require nonzero, restore it exactly, and leave all public-runner execution to 3B-05-T3.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task CheckTestReports</automated>
  </verify>
  <acceptance_criteria>
Manifest, Test IDs, Module READMEs, matrix, JUnit and Summary agree on actual counts; every original baseline case/report remains;
missing report/link/cleanup produces nonzero; no report/log/diff contains secret markers; four check names are byte-for-byte unchanged.
  </acceptance_criteria>
</task>

<task type="auto">
  <name>Task 3B-05-3: Run the single complete 3B lane and close clean develop CI</name>
  <read_first>
    tests/plans/PHASE-3B-SUMMARY.md (planned draft from Task 3B-05-2)
    scripts/windows-local.ps1
    .github/workflows/windows-ci.yml
  </read_first>
  <files>
    tests/plans/PHASE-3B-SUMMARY.md
    tests/TEST-CONTRACT-MATRIX.md
  </files>
  <action>
Run the phase's only complete local `RunAllTests -Configuration Release` after all focused and owning-runner evidence is green.
Audit every registered report for exact actual count, zero failure/error, no secret marker and complete teardown ledger. Run
actionlint when workflow changed, `git diff --check`, protected-path/scope audit and staged-index audit before submission. Submit only
the intended production/test/build/runner/docs files on a topic branch; do not include `tests/auto/`, proxy, quarantine, build,
vcpkg tree, node_modules, credentials or old planning handoff. Require all four unchanged GitHub hosted Windows checks to be success
on the PR SHA and again on the merged `develop` SHA. Do not retry a failed Required run to overwrite its result; diagnose/fix through
the owning plan. Only after post-merge success update the Summary and matrix to close the 3B portions of G-008..G-011/G-015.
  </action>
  <verify>
    <automated>powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File scripts/windows-local.ps1 -Task RunAllTests -Configuration Release</automated>
  </verify>
  <acceptance_criteria>
The one complete local lane is green with exact actual manifest and no residue; clean PR and post-merge develop runs show the four
unchanged checks successful on the intended SHA; protected/user files and secrets are untouched; 3C-only gaps remain open.
  </acceptance_criteria>
</task>

</tasks>

## 13. DG-20 fault coverage matrix

| Required deterministic fault | Owning plan / planned IDs | Required observable result |
| --- | --- | --- |
| Fragmentation / coalescing | 3B-02 T09-GHTTP; 3B-04 T09-CTCP/Q04-TCP | Exactly one correctly ordered request/frame per complete input; incomplete bytes retained only within current connection generation. |
| Maximum length / one-byte-over | 3B-02 T09-GHTTP/Q04-HTTP; 3B-04 T09-CTCP/Q04-TCP | Maximum accepted; over-limit rejected before unbounded allocation or business dispatch. |
| Malformed input | 3B-02, 3B-03, 3B-04 | Stable sanitized error/close; no parser copy, crash or later business call. |
| Request/read/write interruption | 3B-02, 3B-04 | One terminal outcome; pending state closes or remains retry-safe per production Interface. |
| Refused connection | 3B-01..04 | Bounded unavailable/refused result; no automatic retry to green. |
| Deadline / cancel | 3B-01..04 | Completion before hard test deadline; cancelled/expired operation cannot mutate new state. |
| Duplicate / late completion | 3B-02..04 | Generation/flow identity suppresses second or old completion without exposing internals. |
| Port conflict | 3B-01..04 | Startup fails without stealing ownership; original listener survives; released owned port can rebind. |
| Process/thread/socket/port/temp release | all, final 3B-05 audit | Identity-scoped teardown ledger complete; cleanup failure independently gates runner. |

## 14. Verification closure

1. Every observable contract change begins with a focused RED tied to its planned Test ID, then GREEN through the single production
   Interface. A mutation is required only for meaningful transport, failure, lifecycle, compatibility or gate behavior named by the
   task; prose, registration and harmless plumbing do not receive standalone mutations.
2. After focused GREEN, the task runs only its owning public runner: Server and/or Qt. It does not run aggregate full regression.
3. 3B-05 runs `CheckTestStructure` once with one reversible closeout probe for source/target/report registration and production target
   sharing. Counts are updated from actual emitted JUnit only.
4. 3B-05 runs exactly one complete local `RunAllTests`; its manifest includes every old baseline case plus real new cases and no
   missing/failure/error report.
5. Clean GitHub hosted Windows PR and post-merge `develop` runs are authoritative. Local success alone does not complete 3B.
6. No build/test/CI evidence may contain real or synthetic secret values. Only names of required config keys and sanitized identities
   may appear.

## 15. Threat model

### Trust boundaries

| Boundary | Untrusted input crossing it |
| --- | --- |
| loopback client → Gate Beast | HTTP method/path/headers/body, fragmentation, disconnect timing |
| generated stub → Status gRPC service | protobuf fields, deadline, cancellation, completion timing |
| raw/Qt TCP client → Chat acceptor/session | frame header/body length, stream chunking, close/write timing |
| test coordinator → child process | executable/config path, args, env, PID identity, stdout/stderr |
| production transport → Phase 3A Module | parsed request plus authenticated/session context |
| runner → report/artifact | JUnit paths/counts, log content, teardown ledger |

### STRIDE register

| Threat ID | Category | Component | Disposition | Specific mitigation / evidence |
| --- | --- | --- | --- | --- |
| T-3B-01 | Spoofing | ProcessHarness PID/process group | mitigate | Store PID + creation-time identity; signal/terminate only matching run-owned identity; T09-PROC stale-PID mutation. |
| T-3B-02 | Spoofing | transport sender/session context | mitigate | Business sender/context comes from production connection/session Interface, never an arbitrary test payload override. |
| T-3B-03 | Tampering | production/test target wiring | mitigate | Formal EXE and tests reference one production library; CheckTestStructure fails duplicate `.cpp`, fake mode or disconnected Module. |
| T-3B-04 | Tampering | malformed/oversized HTTP/TCP/gRPC input | mitigate | Validate bounds before allocation/dispatch; real parser/codec only; max+1 and malformed RED/mutation tests. |
| T-3B-05 | Repudiation | missing/overwritten failure evidence | mitigate | Preserve primary failure and separate cleanup result, stable Test ID/JUnit/run-id, missing report is nonzero. |
| T-3B-06 | Information disclosure | logs/JUnit/temp evidence | mitigate | Synthetic-only data, bounded sanitized logs, secret-marker scans, no body/token/code/password/email values. |
| T-3B-07 | Denial of service | frames, queues, deadlines, stuck child | mitigate | Max sizes, finite queues, per-operation deadline/cancel, bounded stop escalation, no retry-to-green. |
| T-3B-08 | Elevation of privilege | test-only production controls | mitigate | No clearForTest, test macro, fake-dependency flag, public private container or test-only endpoint. |
| T-3B-SC | Tampering | package/dependency supply chain | mitigate | 3B adds no package-manager dependency; retain locked vcpkg/npm/Qt/action identities. Any proposed install stops for legitimacy review. |

## 16. Stop conditions

- Phase 3A is not fully complete, or any upstream Module lacks a real shared production target used by caller and tests.
- A test requires real/personal Redis, MySQL, SMTP, fixed developer endpoint, public network, real user data or secret value.
- A production transport cannot be shared without duplicating source/algorithm, or an executor proposes wrapper-on-wrapper rather
  than deepening the existing Module.
- Any implementation needs `clearForTest`, test macro, fake EXE mode, public private container, copied parser/serializer/business
  algorithm or different compile definitions for tests.
- A formal EXE path intrinsically requires disposable real dependencies: record the 3C dependency and stop that path; do not inject
  a fake or describe IntegrationHost evidence as formal four-process evidence.
- Required runner unavailable, process/dependency startup failure, timeout, missing report, count drift, secret leakage or cleanup
  failure. None may be manually bypassed or retried into a green result.
- The same failure reason occurs three times without new evidence: stop, preserve logs/ledger and create a handoff rather than loop.
- Existing 232 testcase/12 report baseline, four check names, user/protected paths or staged-index scope would be reduced or altered
  without D-04 review.
- 本地 vcpkg 固定路径缺失/不一致、发生隐式 manifest install，或任一 restore/install/remove/update/upgrade/path change
  未取得 DG-25 的本次明确批准；立即停止，不得切换 installed tree 或删除重建。

## 17. Phase completion criteria

- Phase 3A complete evidence exists and all five upstream Module Interfaces remain shared by production callers/tests.
- T09-HOST, T09-PROC, T09-GHTTP, Q04-HTTP, T09-SGRPC, T09-CTCP, Q04-TCP and T09-COMP implemented behavior is registered with
  unique stable IDs; unused planned IDs remain planned rather than fake cases.
- Gate HTTP, Status gRPC, Chat TCP, Qt QNAM and QTcpSocket all have real loopback evidence through production transport and Phase 3A
  Modules with in-memory Adapters only.
- DG-20 matrix is deterministic and mutation-proven; every wait/operation has deadline and each run has run-id plus complete
  identity-scoped teardown ledger.
- All pre-existing 232 testcases and 12 reports remain; actual new counts/reports match runner manifest, matrix, READMEs and Summary.
- `RunServerTests`, `RunClientTests`, unaffected `RunVarifyTests`/`RunScriptTests`, and the single final `RunAllTests` are green;
  reports contain zero failure/error and no secret marker.
- Four exact `develop` check names are unchanged and green on clean PR plus post-merge SHA. No self-hosted Required runner exists.
- G-008/G-009/G-010/G-011/G-015 are updated only for verified 3B ownership; G-012..G-018 remain open for later phases.
- No process, thread, QObject, socket, port or run-owned temp directory remains; pre-existing/user state is untouched.

## 18. Artifacts this phase produces

All items below are planned outputs, not current facts:

- `tests/plans/PHASE-3B-TEST-PLAN.md` and `tests/plans/PHASE-3B-SUMMARY.md`.
- `tests/support/IntegrationHostFactory.*`, `RunContext.*`, `ProcessHarness.*`, `Win32ProcessAdapter.*`.
- Non-empty `tests/server/integration-host/` and `tests/server/process-harness/` Module sources/READMEs.
- Shared production targets `GateTransport.vcxproj`, `StatusTransport.vcxproj`, `ChatTransport.vcxproj`, referenced by formal EXEs
  and Integration tests; planned `status::StatusGrpcServer` and deepened Gate/Chat server lifecycle Interfaces.
- Qt production Modules `GateHttpTransport` and `ChatTcpTransport`, their non-empty test Modules and planned
  `client_integration.xml` when actual Integration cases exist.
- Actual post-3B Test ID/report manifest, structure contracts, sanitized evidence and teardown ledgers.
- Clean PR plus post-merge GitHub hosted Windows evidence under the unchanged four check names.

## 19. 下一阶段路由

Only after every criterion in section 17 is met, route to
[`PHASE-3C-PLAN.md`](PHASE-3C-PLAN.md) (planned downstream document). Phase 3C owns the Linux compile-link preflight,
same-source CMake/vcpkg path, disposable Redis/MySQL/Mailpit, schema/migration, DG-24 MySQL transaction/idempotency, real Adapter
Integration, four-process topology and N/N-1 bootstrap. 3B IntegrationHost/in-memory evidence must never be relabeled as those 3C
results.
