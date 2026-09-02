---
phase: "3A"
plan: "01"
subsystem: chat-logic-dispatcher
tags: [architecture, concurrency, fifo, shutdown, windows-server-tests]
requires:
  - Phase 3A-00 frozen LogicDispatcher Interface
  - DG-05 confirmed queue and shutdown contract
provides:
  - Production LogicDispatcher Submit/Stop Interface
  - Seven deterministic T08-LOGIC Unit contracts
  - Shared ChatServer and ServerUnitTests static-library ownership
affects: [phase-3a-02, phase-3a-03, phase-3a-06]
completed: 2026-08-31
---

# Phase 3A Plan 01：Chat Logic dispatcher Summary

Chat message FIFO、精确 pending 容量、worker wake、shutdown drain、关闭状态和未知 ID 脱敏诊断现已集中在 production-owned `LogicDispatcher` 深 Module 中；`LogicSystem` 保留业务 handler registry，`CSession` 与测试通过同一 `Submit/Stop` Interface 协作。

## 交付

- 新增 `LogicMessage`、`LogicSubmitResult::{Accepted,Full,Closed}` 与 `LogicDispatcher::Submit/Stop`。PIMPL 隐藏 queue、worker、condition variable、容量和 stop 状态。
- 新增 `LogicDispatcher.vcxproj` static library 及 filters；`ChatServer` 和 `ServerUnitTests` 都通过 `ProjectReference` 链接同一 implementation，solution 已登记该 project。
- `LogicSystem` 删除自己的 queue/worker/CV/atomic stop/`DealMsg`，仅将 dispatcher callback 路由到既有 handler map；所有原业务 handler 与依赖调用保持原位。
- `CSession::AsyncReadBody` 删除 `LogicNode`，提交具备精确 body length 的 `LogicMessage`：`Accepted`/`Full` 继续读下一帧，`Full` 只丢最新消息；`Closed` 关闭连接并停止继续读。公开错误码、proto 和 wire framing 未改变。
- 新增一个真实 Server Unit 测试源，7 个 runner testcase 对应 T08-LOGIC-01..07；`server_unit.xml` 从 53 增至 60，全仓 manifest 从 173 增至 180，报告数保持 12。
- `CheckTestStructure` 新增 production/test shared-library、精确 7-case、容量/drain/idempotent-stop、LogicSystem ownership 和 CSession 三结果 guard。

## Test ID 与结果

| Test ID | Runner testcase | 结果 |
| --- | --- | --- |
| T08-LOGIC-01 | `AcceptedMessagesDispatchInFifoOrder` | GREEN |
| T08-LOGIC-02 | `ConcurrentProducersDispatchEveryAcceptedMessageExactlyOnce` | GREEN；8 producers / 400 accepted messages |
| T08-LOGIC-03 | `ExactPendingCapacityRejectsOnlyTheNextMessage` | GREEN；精确 `MAX_DEALQUE=1000` pending |
| T08-LOGIC-04 | `WaitingWorkerWakesForMessageAndStop` | GREEN |
| T08-LOGIC-05 | `StopDrainsAcceptedMessagesWithinTwoSeconds` | GREEN |
| T08-LOGIC-06 | `ClosedDispatcherRejectsImmediatelyAndRepeatedStopIsIdempotent` | GREEN |
| T08-LOGIC-07 | `UnknownIdDoesNotBlockValidMessageAndLogOmitsBody` | GREEN |

所有等待使用 condition variable、promise/future 或线程 join，并具有两秒硬上限；没有 fixed sleep、真实 socket、Redis、MySQL 或 gRPC。

## RED / GREEN / Mutation 证据

1. 首个 RED：Interface、测试、README 和 project registration 落地后，Release focused build 因 production `LogicDispatcher.cpp` 尚不存在而以 C1083 非零失败；证明测试真实链接 production library。
2. FIFO 最小 GREEN 后，T08-LOGIC-02 的多 producer 合同也由同一 mutex/FIFO implementation 满足，无额外生产分支。
3. 容量行为 RED：未实现上限时第 1001 个 pending message 错误返回 `Accepted`，最终处理 1002 条；添加 `size() >= MAX_DEALQUE` 后 GREEN。
4. unknown-ID 行为 RED：handler 返回 false 时缺少 ID 诊断；添加只含 `msg_id` 的 warning 后 GREEN，body marker 未进入捕获日志。
5. 实际 mutation：临时把 `>= MAX_DEALQUE` 改为 `>`，T08-LOGIC-03 非零失败并再次观察到 1002 条；精确恢复 `>=` 后 focused GREEN。最终源码无 mutation 残留。

## 验证

| 验证 | 结果 |
| --- | --- |
| `CheckTestStructure` | GREEN（最终：17 Server、3 Qt、6 VarifyServer、2 PowerShell sources） |
| focused `LogicDispatcherTests.*` Release | 7/7 GREEN，约 2 ms |
| full `server_unit_tests.exe` from runner working directory | 60/60 GREEN，约 0.34 s |
| `server_unit.xml` audit | 60 testcase；0 failure；0 error；7 Logic cases；body marker absent |
| real `ChatServer.vcxproj` Release build/link | GREEN；生成 `build/windows-servers/Release/ChatServer/ChatServer.exe` |
| off-by-one mutation | expected nonzero；恢复后 focused GREEN |
| `git diff --check` | GREEN |
| staged index | 空 |

首次从 repository root 直接运行 Unit binary 时，既有 ConfigMgr fixture 因相对工作目录失败；按 public runner 的 binary working-directory 语义复跑后 60/60 GREEN，没有修改测试或生产预期。

## Public runner 未验证范围

按计划只启动一次 `RunServerTests -Configuration Release`，复用已有 `x64-windows-chat-release` tree，未运行 restore/install。该命令在 solution MSBuild 阶段达到 30 分钟硬上限并以 exit 124 结束，尚未进入测试 executable 阶段，因此没有取得六份 Server report 的整组通过证据。精确核验后只终止了本次 owned PowerShell PID 22064 和 MSBuild PID 30756；最终无该 PID 或 repository Server/build/test 进程残留。

被终止的 manifest MSBuild 使已有 `vcpkg_installed/x64-windows-chat-release` 处于部分状态，最终只读核验发现 `spdlog/spdlog.h` 缺失。依赖树未被手工删除、安装或修复；同因三次最终增量尝试均在外部 include 处停止。超时前已经完成的真实 ChatServer Release、7/7 focused、60/60 Unit 和 mutation 证据有效；继续新的 Server build 前需通过项目既有 restore 流程恢复 pinned dependency tree。

未运行 `RunAllTests`、远端 CI、PR、push 或真实服务测试。完整 12-report/180-case 回归和 clean PR CI 仍由 3A-06 负责。

## 文档与登记

- 新增 `tests/server/logic-dispatcher/README.md`，记录 Interface、Domain/Level、依赖、timeout、cleanup、runner/report、mutation 与剩余 gap。
- 更新 `tests/server/README.md`、`tests/README.md`、`tests/REGRESSION.md`、`tests/TEST-CONTRACT-MATRIX.md`、`PHASE-3A-TEST-PLAN.md` 和 runner manifest；G-007 标记 complete，G-008..G-011 保持 open/planned。
- `PHASE-3A-PLAN.md` 仅将 3A-01 标记 Complete，并将下一项指向 3A-02；未改变 3A-02..06 或 Phase 3B 状态。

## 偏差与自动修复

- **构建环境调整：**默认 `x64-windows-chat` installed tree 不存在依赖；focused/production 验证复用仓库已存在且同一 pinned vcpkg commit 的 release-only triplet `x64-windows-chat-release`，没有安装替代 package。
- **生产编译修复：**`CSession` 的 `uint16_t` message ID 进入既有 signed-short handler Interface 时，列表初始化触发 C2397；添加显式 `int16_t` cast，保持原 handler 数值语义与 wire bytes 不变。
- **runner timeout：**按硬上限终止本次 owned process tree，不重试刷绿，不清理 build/cache/vcpkg/node_modules；其未完成范围如上明确保留。

## 安全、清理与范围

- unknown-ID、Full、Closed 诊断仅含 reason/msg_id，不记录 body、Token 或凭据；合成 body marker 不在 XML/log。
- 最终无 repository build/test/server 进程，无 `chat-config-test-`、`chat-startup-tests-`、`gate-status-startup-tests-` 临时前缀残留；本 plan 未创建 socket/port。
- 未读取或修改 proxy/quarantine 内容，未修改 `.planning/`，未触碰受保护的 `tests/auto/*` 预存修改，未删除 build/cache/vcpkg/node_modules。
- 未切分支、stash、stage、commit、push、建 PR 或触发 CI。工作树中 `README.md`、受保护路径及其他计划文件的预存改动/未跟踪状态未纳入本 plan。
- 新增 thread lifecycle 为 in-process surface，不新增网络 endpoint、auth path、文件访问或 trust-boundary schema；无额外 threat flag。无阻止本计划目标的 stub。

## Key files

新增：

- `ChatServer/ChatServer/LogicDispatcher.h/.cpp`
- `ChatServer/ChatServer/LogicDispatcher.vcxproj/.filters`
- `tests/server/logic-dispatcher/README.md`
- `tests/server/logic-dispatcher/logic_dispatcher_tests.cpp`

修改：

- `ChatServer/ChatServer/LogicSystem.h/.cpp`
- `ChatServer/ChatServer/CSession.h/.cpp`
- `ChatServer/ChatServer/ChatServer.vcxproj/.filters`
- `Chat.sln`
- `tests/server/ServerUnitTests.vcxproj`
- `scripts/windows-local.ps1`
- 上述测试治理/计划文档

## Self-Check: PASSED

- production Interface/implementation、static-library projects、测试、Module README 与 Summary 均存在。
- T08-LOGIC-01..07 实际 7 个 runner testcase，focused 7/7 与完整 Server Unit 60/60 通过。
- 最终容量判断为 `>= MAX_DEALQUE`；LogicSystem 不再拥有 queue/worker/CV；CSession 覆盖三种提交结果。
- XML、marker、stub/mutation、临时前缀、owned process、diff-check 与 staged 审计均通过。
- G-007 已关闭；由于 pinned dependency tree 需恢复且完整 Server runner 未完成，进入 3A-02 前应先恢复 Server dependencies。3A-06 仍负责全量 12-report/180-case 与 CI 收口。
