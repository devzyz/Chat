---
phase: "3A"
plan: "00"
subsystem: regression-contract-freeze
tags: [baseline, contracts, deep-modules, deduplication, windows-ci]
requires:
  - Phase 2.5 twelve-report/173-testcase regression baseline
  - R0.1/R0.2 complete on develop a03d0c30
  - DG-05..DG-08 confirmed
provides:
  - Five pre-RED production Interface and Test Plan registrations
  - Existing-coverage versus Phase 3A gap audit
  - Protected-path and CI baseline snapshot
affects: [phase-3a-01, phase-3a-02, phase-3a-03, phase-3a-04, phase-3a-05, phase-3a-06]
key-files:
  created:
    - tests/plans/PHASE-3A-TEST-PLAN.md
    - tests/plans/PHASE-3A-00-SUMMARY.md
  modified:
    - tests/plans/PHASE-3A-PLAN.md
    - tests/TEST-CONTRACT-MATRIX.md
completed: 2026-08-30
---

# Phase 3A Plan 00：基线与合同冻结 Summary

Phase 3A 的五个新业务/架构 Module 已在首个 RED 前冻结唯一生产 Interface、planned Test ID、
真实 seam 与报告归属，同时以源码、测试和项目注册证据证明当前 173-case baseline 只覆盖相邻
合同，并未提前关闭 G-007..G-011。

## 执行前快照

| 项目 | 结果 |
| --- | --- |
| branch | `develop` |
| local HEAD | `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e` |
| `origin/develop` | `a03d0c30e9a5e90d2de11320bf7c9e492294ed0e` |
| staged index | 空 |
| post-merge baseline | GitHub Actions push run `33261698599`，head SHA 同上，completed/success |
| baseline scope | 12 reports / 173 runner testcase；Server 119、Qt 12、VarifyServer 29、PowerShell 13 |

远端 run 只读复核得到四个 success job；本 plan 没有重新触发 CI，也没有重复运行完整
`RunAllTests`。

### Required Check 精确名称

- `Static configuration checks`
- `Server Release build`
- `Qt client Release`
- `VarifyServer dependency and package check`

### 12-report / 173-case manifest

| Report | Exact testcase count |
| --- | ---: |
| `server_unit.xml` | 53 |
| `server_component.xml` | 24 |
| `server_integration.xml` | 34 |
| `server_chat_grpc_integration.xml` | 4 |
| `server_gate_unit.xml` | 2 |
| `server_status_unit.xml` | 2 |
| `client_unit.xml` | 7 |
| `client_component.xml` | 5 |
| `varify_unit.xml` | 18 |
| `varify_integration.xml` | 11 |
| `script_component.xml` | 9 |
| `script_integration.xml` | 4 |
| **合计** | **173** |

manifest 来自未修改的 `scripts/windows-local.ps1` declarative registration。planned Phase 3A
Test ID 没有被写成 runner testcase，也没有预填将来的报告数量。

## 保护范围快照

只记录路径、Git 状态、大小/文件数与 SHA-256；未输出敏感文件内容。

| 路径 | 执行前状态 | 大小/文件数 | SHA-256 或目录 manifest SHA-256 |
| --- | --- | ---: | --- |
| `tests/auto/chat-test.md` | tracked modified（预存） | 21383 bytes | `4E91D9951016CE9739ED203ECBC0D1924B8CF58A521B49674EB78EB1D0F51179` |
| `tests/auto/test-strand.md` | tracked modified（预存） | 12796 bytes | `BBB3872171442A5D3D2772A79FA82480E6D2B0AAB62E266CA691045E3DE740F4` |
| `.huorong-quarantine-check/` | untracked（预存） | 3 files | `C50ECC3B350B8B4E63F43461EE3FB6D2F533526D74A06A302DC16DB15C6FC98E` |
| `ChatGPT-Proxy.ps1` | untracked（预存） | 3537 bytes | `1F6E4B6D4C66E5DF7080C59952AA8F7C7B407969B163E7FA556D03132F4AA491` |
| `.planning/HANDOFF.json` | protected pre-existing file | 4610 bytes | `2944F03AC8DECB7B8B8444769E244F93FB552D234C71C526EE84B39CE1F5A601` |

`build/`、`vcpkg_installed/` 与 `VarifyServer/node_modules/` 均为预存目录。本 plan 未读取其
内容，未修改、清理、暂存或提交这些目录；根 `node_modules/` 不存在。最终复核确认上述五个
保护路径的状态与 hash 未变，staged index 仍为空。

## 交付

- 新建集中 [`PHASE-3A-TEST-PLAN.md`](PHASE-3A-TEST-PLAN.md)，冻结 3A-01..05 的唯一生产
  Interface、不变量、错误、顺序、生命周期/timeout、依赖分类、production/test Adapter、
  planned report、确定性同步/清理、RED/GREEN/mutation、剩余 Integration/E2E gap 与项目接线。
- 在 [`TEST-CONTRACT-MATRIX.md`](../TEST-CONTRACT-MATRIX.md) 登记五组 planned Test ID，明确
  它们尚未进入 173 baseline、尚未关闭 Gap，也不代表 runner testcase。
- 更新 [`PHASE-3A-PLAN.md`](PHASE-3A-PLAN.md)，把 3A-00 标记为 2026-08-30 Complete，并保留
  R0.1/R0.2 Complete 证据；下一项明确为 3A-01。
- DG-05..08 均保持 Confirmed，没有改变公开错误码、proto、CI lane、report 或 Required Check。

## 现有覆盖 / 明确未覆盖去重审计

| 目标 Module | 已有相邻合同与证据 | 3A 新合同为何仍未覆盖 |
| --- | --- | --- |
| Chat Logic dispatcher | T02-FRM 只测 `ChatFrameCodec` header/length；F03-ASIO/T03 只测 `AsioIOServicePool` lifecycle；T08-RUNTIME 只测 gRPC pool/deadline。`ServerUnitTests.vcxproj` 未注册 `LogicSystem.cpp`，现有测试也无 `LogicSystem`/`PostMsgToQue` 引用。 | `LogicSystem.cpp` 当前内部拥有 queue/worker，`PostMsgToQue` 无 Accepted/Full/Closed 结果且使用 `size() > MAX_DEALQUE`；FIFO、多 producer exactly-once、精确容量、stop drain/closed 与 unknown-ID 连续性均无测试表面。 |
| Status routing/token | T08-GATE-RPC 测生产 Gate client 对动态 loopback fake Status 的 transport/error mapping；startup 只测真实 EXE CLI/bind/ready；T08-RUNTIME/pool 已保护 deadline/lifecycle。任何 Server test project 都未注册 `StatusServiceImpl.cpp`。 | `StatusServiceImpl::getChatServer` 仍直接 `_servers.begin()->second`；empty/list count/tie/unknown 选择未测。`insertToken` 忽略 Redis write 结果，assign fail-closed、token validation 三态与并发确定性未测。 |
| Chat session registry/send state | T02-FRM 只保护 byte header；T04-RDS 只保护 Chat Redis pool wait/close；Chat gRPC tests 只保护 RPC clients。Server tests 无 `CSession`、`CServer` 或 server `UserMgr` 测试源/注册。 | `CSession::Send` 的容量判断为 `> MAX_SENDQUE` 且返回 void；write failure/close/cleanup exactly-once 不可观察。虽然 `UserMgr::RemoveUserSession` 已按 session ID 条件删除，但原子替换、旧关闭不删新映射、并发 close 与完整 FIFO/capacity lifecycle 未经 Interface 测试。 |
| Gate request orchestration | T06-GATE 通过真实 `gate::HandleJsonRequest` 保护 parse、exception boundary、allowlist/envelope；T08-GATE-RPC 与 pool tests 保护 production gRPC client/deadline。`ServerComponentTests.vcxproj` 只编译 `GateResponse.cpp`，不编译 Gate `LogicSystem.cpp`。 | 四条 route 的注册/重置/登录顺序仍位于 singleton lambda；现有 GateResponse callback 可证明 shaping/调用一次，但不能注入 Redis/MySQL/Varify/Status 来证明 early-return、依赖 failure mapping 与后续 Adapter 未调用。 |
| Qt auth/network coordinator | T07-FRM 只测 `TcpFrameDecoder`；Q02-SESSION 只测 `ClientSession::resetSession`、`TcpMgr::resetConnection`、账号/pending/UI 清理。CMake 的三个现有 test executable 均未注册 `HttpMgr`、dialogs 或 `MainWindow` auth flow。 | HTTP/network/JSON/business/TCP/Chat-login 分支分散在 manager/dialog；未知 `ReqId` 直接 `_handlers[id]`，重复/迟到 flow 没有统一 generation/dedup。`TcpMgr::sig_login_failed` 当前没有 auth-flow consumer。Q03 只新增 outcome→action 合同，并复用而不重测 decoder/reset。 |

审计结论：五个 Gap 都是真实未覆盖，不应通过新增 GateResponse、GrpcClientRuntime、
ClientSession 或 TcpFrameDecoder 近义测试来“补数量”。后续测试必须穿过新深 Module 的同一
production Interface；相邻既有测试保留不降级。

## 冻结的 Module 登记摘要

| Plan | Planned IDs | Domain / Level | 唯一生产 Interface | Planned report |
| --- | --- | --- | --- | --- |
| 3A-01 | T08-LOGIC-01..07 | Architecture / Unit | `LogicDispatcher::Submit/Stop` | `server_unit.xml` |
| 3A-02 | T08-STATUS-01..14 | Business/Architecture / Unit + Component | `StatusRouting::Assign/Validate` | `server_unit.xml` + `server_component.xml` |
| 3A-03 | T08-SESSION-01..10 | Architecture / Component | `ChatSessionState` create/register/find/close/send | `server_component.xml` |
| 3A-04 | T08-GATE-01..16 | Business / Component | `GateRequest::Handle` | `server_component.xml` |
| 3A-05 | Q03-AUTH-01..12 | Business/Architecture / Unit + Component | `AuthFlowCoordinator::Reduce` | `client_unit.xml` + `client_component.xml` |

具体 adapter、timeout、同步、cleanup、mutation 与拟接线以集中 Test Plan 为准。所有 port 都
有 production + in-memory 两个真实用途；纯 In-process 逻辑没有为 mock 方便增加 port。

## 空 future test directory 规则

五个新 Interface 当前都不存在。若现在创建 `tests/server/logic-dispatcher/`、
`status-routing/`、`session-state/`、`gate-request/` 或 `chat/tests/auth-flow/`，只会产生虚假
空目录/README，与 `REGRESSION.md` 冲突。因此本 plan 选择集中 Test Plan 作为最小登记位置；
后续 plan 必须先落地生产 Interface，再创建包含真实测试源的 Module 目录，把对应登记迁入
Module README，并完成项目与 runner 接线。这同时满足“首个 RED 前有 Test Plan”和“不创建
空 future test directory”。

## 验证

- GitHub Actions run `33261698599`：只读复核为 develop push、同一 SHA、completed/success，
  四个 Required Check job 均 success；作为既有 post-merge 行为基线。
- `CheckTestStructure`：GREEN；16 Server、3 Qt、6 VarifyServer、2 PowerShell 测试源均已注册。
  首次直接调用被本机 ExecutionPolicy 拒绝，随后使用同一脚本的
  `powershell.exe -ExecutionPolicy Bypass -File ... -Task CheckTestStructure` 公开结构入口通过。
- 静态登记一致性：GREEN；runner manifest 精确 12/173，五组 planned range 同时出现在集中
  Test Plan 和矩阵，目标 future test directory 均不存在。
- `git diff --check` 与全部变更文档 trailing-whitespace 扫描：GREEN。
- scope 审计：没有生产源、测试源、项目文件、CMake、runner 或 workflow 变化；没有新增可执行
  测试；staged index 为空。
- 未运行 `RunAllTests`、长构建、远端新 CI 或真实 Redis/MySQL/SMTP/network 测试。

## 偏差

没有扩大到生产实现或可执行测试。为同时遵守 Phase 3A 登记要求与 `REGRESSION.md` 的空目录
禁令，采用计划允许的集中 Test Plan，而不是预建五个 Module 目录。这是文档位置调整，不改变
任何合同或执行范围。

本机 PowerShell ExecutionPolicy 阻止了第一次结构命令；改用仓库/CI 同样支持的非交互
Bypass 调用后一次通过，没有修改系统 policy 或脚本。

## 保护范围与下一步

- 未读取敏感文件内容；仅采集允许的路径/hash metadata。
- 未触碰 `tests/auto/`、`.huorong-quarantine-check/`、`ChatGPT-Proxy.ps1`、旧
  `.planning/HANDOFF.json`、`build/`、vcpkg tree、node_modules。
- 未 stash、reset、clean、checkout、stage、commit、push、建 PR 或改分支。
- 可以进入 **3A-01**。其首个 RED 前必须以 `PHASE-3A-TEST-PLAN.md` 的冻结合同为准；不得
  顺手进入 3A-02..05，也不得改变当前 173 baseline，实际 runner 数由实现后的真实用例决定。

## Self-Check: PASSED

- 3A-00 所需 snapshot、去重审计、五个 Test Plan 登记、矩阵 planned registration、计划状态与
  Summary 均存在。
- R0 与 DG 状态未破坏；G-007..G-011 未标记 complete；12/173 baseline 未变。
- 文档/结构验证通过；没有伪造新的测试、构建或 CI 结果。
- 最终 staged index 为空，保护路径状态/hash 与执行前一致，且无 production/test source 变化。
