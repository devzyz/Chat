---
phase: "2.5"
name: regression-baseline-hardening
status: planned
type: test-governance-and-foundation
autonomous: false
depends_on:
  - phases-1-and-2-local-baseline
decisions:
  - D-01
  - D-02
  - D-04
  - D-05
  - D-08
  - D-09
  - D-10
  - D-11
requirements:
  - G-001
  - G-002
  - G-003
  - G-004
  - G-005
  - G-006
---

# Phase 2.5：回归基线补强计划

## 1. Objective

在进入阶段三业务测试前，把阶段一、二形成的本地测试结构收口为可信的 `develop` 全量快速门禁：测试语义、报告、协议兼容、启动/关闭、gRPC 有界失败、Qt session reset 和敏感响应合同均可在本地与 clean CI 重复验证。

本阶段完成后，`develop` 的绿色结果应能可靠表达：所有已登记的快速合同均执行、断言、报告且无清理错误。真实 Redis/MySQL/SMTP、双 ChatServer 业务 E2E 和正式发布 artifact 晋升不属于本阶段，它们分别进入 Phase 3C、3D 和 release gate。

## 2. Must-haves

### Truths

1. 当前 95 个 runner testcase 在任何重构中不得减少或弱化，除非按 D-04 完成合同评审。
2. 本地与 CI 调用同一 `Run*Tests`/`RunAllTests` 入口；测试失败、超时、缺报告或清理失败均返回非零。
3. Unit/Component 不访问 Redis、MySQL、SMTP、公网或个人配置。
4. 真实 loopback socket、gRPC 和子进程仍标为 Integration，不能因运行快而降级。
5. 测试通过生产 Module 的 Interface，不复制核心实现，不新增只供测试使用的 `clearForTest` 或全局开关。
6. 协议兼容检查能够发现共享字段号、类型或 RPC 方法漂移，而不只是同一份新代码的 round-trip。
7. Gate HTTP 响应和测试报告不得包含密码、Token、验证码或凭据。
8. 每个异步、网络和进程测试有硬超时，并在所有结果下完成本次资源清理。
9. clean GitHub Runner 全绿前不得宣布 Phase 2.5 完成。

### Artifacts

| Artifact | Purpose |
| --- | --- |
| `tests/CI-GOVERNANCE.md` | 分支、发布、失败、覆盖率和合同治理的权威规则 |
| `tests/TEST-CONTRACT-MATRIX.md` | 现有 Test ID、runner testcase、报告、lane 与缺口 |
| 本计划 | Phase 2.5 的任务、依赖、验收和验证顺序 |
| 分层 JUnit/XML | 逐 testcase 诊断并作为 Required Check 证据 |
| 协议兼容 fixture | 当前/历史 wire 合同输入与预期 |
| clean-runner 结果 | 同一 commit 上的 required job 和 artifact 证据 |

## 3. Scope fences

本阶段不得：

- 实现阶段三的 LogicSystem、完整 Gate 业务、Status 负载均衡或 Qt 页面 E2E；
- 连接开发者固定 Redis/MySQL、真实 QQ SMTP 或个人凭据；
- 初始化 Docker 真实依赖环境；
- 建立假设性的群聊、缓存、文件、媒体或 LAN Interface；
- 手工修改 protobuf 生成文件；
- 为测试方便复制生产解析/路由实现；
- 删除本地 `build`、`vcpkg_installed`、`node_modules` 或运行基线；
- 在未通过 clean CI 前修改 branch protection 为一个尚不存在或不稳定的 check 名称。

## 4. Pre-implementation decision gates

以下合同必须在对应 plan 开始前确认并写入 Module README；它们不是由测试自行猜测的实现细节：

| Gate | Proposed direction | Blocks |
| --- | --- | --- |
| DG-01 gRPC 时间 | **Confirmed**：pool acquire 1s；Status/Chat RPC 3s；Varify RPC 15s；配置范围 100ms–60s；内部分类错误、公开映射 `RPCFailed`；无自动重试 | Plan 2.5-05 |
| DG-02 Qt session reset | **Confirmed**：返回登录页即结束认证会话；清除 connection、decoder、pending batch、账号级内存状态及旧 Qt 模型；仅保留非敏感应用配置，未来缓存由独立账号级 Module 管理 | Plan 2.5-06 |
| DG-03 Gate 响应 allowlist | **Confirmed**：验证码、注册、重置成功/失败均只返回 `error`；登录成功返回 `error/uid/token/host/port`，失败只返回 `error`；禁止请求秘密和非必要 PII 回显 | Plan 2.5-03 |
| DG-04 proto 权威来源 | **Confirmed**：建立 `proto/varify.proto`、`proto/status.proto`、`proto/chat.proto` 三个按服务拆分的单一权威源；固定生成工具链，并以 descriptor、旧 wire fixture 和互操作测试约束上一正式发布版本兼容性 | Plan 2.5-02 |

若 proposed direction 被拒绝，先更新本计划和 `CI-GOVERNANCE.md` 的影响说明，再实施；不得边写测试边固化偶然行为。
已确认的完整合同以 [`DG-DECISIONS.md`](DG-DECISIONS.md) 为准。

## 5. Wave plan

| Wave | Plan | Depends on | What it delivers |
| --- | --- | --- | --- |
| 1 | 2.5-01 Test metadata and reports | P0 docs | 精确 Level、逐 testcase XML、单一 Module 文档归属 |
| 1 | 2.5-02 Protocol compatibility | P0 docs、DG-04 | 跨服务共享 proto 检查和旧 wire golden fixture |
| 1 | 2.5-03 Gate response safety | P0 docs、DG-03 | 敏感字段 allowlist 和 RED-GREEN 回归 |
| 2 | 2.5-04 Gate/Status startup | 2.5-01 | CLI/config/bind/ready/stop 的黑盒合同 |
| 2 | 2.5-05 gRPC deadlines and pools | 2.5-01、2.5-02、DG-01 | 有界 RPC、pool close/borrow 合同与 loopback 错误映射 |
| 2 | 2.5-06 Qt session lifecycle | 2.5-01、DG-02 | 重连不继承半包、pending batch 或旧账号 session |
| 3 | 2.5-07 Clean CI and required gate | 2.5-01..06 | 本地/clean-runner 同入口全绿和稳定 Required Check |

同一 wave 的计划可以并行分析，但修改同一 runner/build 文件时必须串行合并并复跑 `CheckTestStructure`。

## 6. Executable plans

### Plan 2.5-01 — Test metadata and report integrity

**Objective**
修正测试 Level 语义和报告粒度，不改变生产行为或既有断言。

**Files expected to change**

- `chat/CMakeLists.txt`
- `chat/tests/message-model/README.md`
- `scripts/windows-local.ps1`
- `tests/scripts/validation/chatserver-instances.tests.ps1`
- `tests/scripts/lifecycle/chatserver-instances.tests.ps1`
- `tests/scripts/**/README.md`
- `tests/server/concurrency/README.md`、`tests/server/lifecycle/README.md`
- `tests/README.md`、`tests/TEST-CONTRACT-MATRIX.md`

**Tasks**

1. 将 Q01-MODEL-01..06 标为 Unit，Q01-MODEL-07..08 保持 Component；目录仍由 message-model Module 所有。
2. 让 `RunClientTests` 根据 CTest Level 生成正确的 `client_unit.xml` 和 `client_component.xml`。
3. PowerShell runner 输出 9 个 validation 与 4 个 lifecycle testcase，而不是每个脚本一个聚合 testcase。
4. 保留每个脚本的控制台逐项 PASS/FAIL 和非零退出；报告生成失败也必须失败。
5. 合并 `concurrency` 与 `lifecycle` 对同一 Asio Test ID 的重复文档归属；源码只保留一份。
6. 扩展 `CheckTestStructure`：验证报告分组、CTest Level、npm file list、PowerShell testcase 注册和 Module README 存在。

**Verify**

```powershell
.\scripts\windows-local.ps1 -Task CheckTestStructure
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
.\scripts\windows-local.ps1 -Task RunScriptTests
```

检查 XML：Qt runner testcase 合计仍为 9；PowerShell 分别为 9 和 4；failure/error 为 0。临时制造一个未注册 testcase 或错误 Level 必须使结构检查 RED，恢复后 GREEN。

**Acceptance criteria**

- 95 个基线 testcase 一个不少；
- Qt Level 与实际测试表面一致；
- PowerShell XML 可以定位具体 Test ID；
- 没有平行 Level 目录和重复 Module 文档；
- 生产二进制行为未改变。

### Plan 2.5-02 — Cross-service protocol compatibility

**Objective**
保护跨 Gate、Status、Chat、Varify 的共享 wire 合同，并建立当前版到上一发布版兼容测试的基础。

**Files expected to change**

- 各发布单元的 `message.proto` 或新的权威 proto Module（以 DG-04 决策为准）
- 对应生成命令/构建注册，不手工编辑生成代码
- `tests/server/protocol/*`
- `VarifyServer/test/protocol/*`
- 兼容 fixture 目录及 README
- `tests/TEST-CONTRACT-MATRIX.md`

**Tasks**

1. 列出四份 proto 的共享 message/service、字段号、类型和 RPC 方法；Chat 专有 RPC 单独归属 Chat Module。
2. 选择一个权威来源或生成兼容 manifest，避免把“文件完全相同”误当目标。
3. 增加静态/descriptor 检查：共享合同任一副本漂移时失败。
4. 保存不含秘密的旧 wire golden fixture，验证新代码可解析旧 payload、未知字段不会破坏旧消费者。
5. 增加 C++ ↔ Node Varify 动态 loopback 互操作；客户端必须有 deadline。
6. 固定生成工具版本，生成物来源可复查。

**Verify**

- 修改一个测试副本字段号或 RPC 方法时兼容检查 RED；恢复后 GREEN。
- 旧 fixture 能被当前 C++/Node 解析并保持公开字段。
- 动态 loopback 使用 `127.0.0.1:0`，超时后清理 client/server。
- 运行 `RunServerTests`、`RunVarifyTests` 和 `CheckTestStructure`。

**Acceptance criteria**

- 同版本 round-trip 不再是唯一兼容证据；
- 共享合同漂移在 `develop` 被阻断；
- 没有手工修改 generated sources；
- fixture 和报告不含凭据、Token 或真实邮箱。

### Plan 2.5-03 — Gate response allowlist and secret regression

**Objective**
确保 Gate 公开响应永不回显请求密码、确认密码、验证码或内部异常文本。

**Files expected to change**

- Gate response shaping 所属生产 Module
- Gate Component/loopback tests 与 Module README
- Server test build registration/report
- `tests/TEST-CONTRACT-MATRIX.md`

**Tasks**

1. 根据 DG-03 为验证码、注册、重置、登录响应定义字段 allowlist 和稳定错误 envelope。
2. 将 JSON response shaping 集中到一个生产 Module Interface；handler 和测试通过同一 Interface，避免复制序列化逻辑。
3. 先添加能够捕获当前敏感字段回显的 RED 测试，再最小修改生产实现使其 GREEN。
4. 覆盖成功、业务失败、JSON 失败和内部异常路径；断言秘密字段名和值均未出现在 body/log。
5. 若走真实 loopback HTTP，标记 Integration；仅使用 in-process fake 依赖时标记 Component。

**Verify**

- 受控请求含虚构密码/验证码，所有响应与日志均不包含它们。
- 临时恢复任一敏感回显时测试 RED。
- Gate 既有构建和其他 Server/Varify 脱敏测试保持 GREEN。

**Acceptance criteria**

- 所有公开响应使用 allowlist；
- 不通过字符串事后替换掩盖泄漏；
- 新测试进入明确 Level 报告并阻断 `develop`。

### Plan 2.5-04 — Gate/Status startup and shutdown contracts

**Objective**
让 Gate、Status 达到与 ChatServer 一致的 CLI fail-fast、本地 bind、ready 和有限退出基线。

**Files expected to change**

- Gate/Status main、ConfigMgr、启动/关闭拥有者
- 可共享的 CLI options Module（仅当三个生产 Adapter与测试确实复用）
- Server Integration tests/projects
- Module README、runner、报告与矩阵

**Tasks**

1. 黑盒测试 `--config` 缺值、未知参数、缺文件、非法端口和关键 endpoint 缺失。
2. 统一配置来源优先级：CLI、`CHAT_CONFIG`、工作目录默认值。
3. Gate/Status 检查 bind/start 结果后才报告 ready；Status 必须处理 `BuildAndStart()` 失败。
4. 动态占用 HTTP/gRPC 端口，验证非零退出和端口释放。
5. 使用协议探针而不是 PID 判断成功 ready。
6. 测试 SIGINT/SIGTERM 或 Windows 等价路径：停止接收、关闭 server、join thread、释放端口。
7. 失败时保存 stdout/stderr，finally 只清理本次 PID 和临时目录。

**Verify**

- 每个进程测试启动上限 10–20 秒、停止上限 5 秒，超时按已核验 identity 兜底清理。
- 无 Redis/MySQL 的快速场景在访问外部依赖前完成；不得连接仓库开发 endpoint。
- `RunServerTests` 与全套已有 48 个 Server testcase 保持 GREEN。

**Acceptance criteria**

- Gate/Status 不再静默接受畸形 CLI；
- bind/start 失败不报告 ready；
- 正常/失败退出均无进程或端口残留；
- 新测试属于 develop required Integration。

### Plan 2.5-05 — Bounded gRPC clients and pool lifecycle

**Objective**
让 Gate、Status、Chat 的 owned-remote Adapter 具备有限 deadline、有限 pool acquire 和确定关闭合同。

**Files expected to change**

- Gate/Status/Chat `*GrpcClient*`、pool 与配置
- gRPC Module tests、loopback fixtures、README
- Server test projects/runner/reports
- example config 与 Operations/Protocol 文档（若新增配置项）

**Tasks**

1. 完成 DG-01：记录 production RPC deadline、pool acquire timeout、配置范围和默认值。
2. 每次项目 RPC 使用有限 `ClientContext` deadline；测试可以注入短值但默认生产行为来自正式配置。
3. 参数化保护每份现有 pool：正常 borrow/return、耗尽超时、close 唤醒、重复 close、关闭后 borrow、关闭后 return。
4. 修复由合同测试捕获的 stop 状态初始化/赋值问题；不以 sleep 证明关闭。
5. 动态 loopback 验证 success、unavailable、deadline exceeded 和对端 shutdown 的项目错误映射。
6. 生产 EXE 和测试逐个 Module 改为共享同一个 production library；不在本阶段全仓一次性迁移。

**Verify**

- 未启动 endpoint 在 deadline 内返回稳定错误，不永久阻塞。
- pool waiting thread 在 close 后被唤醒，所有 worker 可 join。
- 临时破坏 stop flag 或移除 deadline 时测试 RED。
- Server 快速套件无固定外部服务依赖。

**Acceptance criteria**

- 所有 C++ 项目 RPC 有有限 deadline；
- pool 生命周期合同覆盖 Gate、Status、Chat 的实际生产实现；
- loopback Integration 动态分配端口并可靠清理。

### Plan 2.5-06 — Qt connection and session reset

**Objective**
保证断线、被踢、登出和新账号登录不会继承旧连接半包、pending batch 或内存 session 状态。

**Files expected to change**

- `chat/tcpframedecoder.*`
- `chat/tcpmgr.*`
- `chat/usermgr.*`
- owning MainWindow/session lifecycle code
- Qt production libraries、tests、CMake、README 与矩阵

**Tasks**

1. 完成 DG-02，列出必须清除和允许保留的状态；未来本地缓存不通过 `UserMgr` 偶然内存保留实现。
2. 为 `TcpFrameDecoder` 提供真实 connection lifecycle `reset` Interface，并由 disconnect/reconnect 路径调用。
3. 在 owning session Module 提供业务含义明确的 `resetSession`，集中清理认证、UserMgr transient state、pending batch 和分页 loading 状态。
4. 测试半包断线后新连接不拼接旧字节、新账号看不到旧好友/会话、未知旧 ack 不修改新 session。
5. 使用 `QSignalSpy` 和真实 Module；不增加 `clearForTest`。
6. 若测试真实 `QTcpSocket`，添加 `integration` CTest label 与 `client_integration.xml`；纯状态测试保持 Unit/Component。

**Verify**

- 断线前注入半 frame，重连后完整 frame 独立解析。
- 登出后重新登录另一账号，所有 transient map、cursor 和 pending ack 隔离。
- 临时移除 reset 调用时回归测试 RED。
- Qt minimal 环境下重复运行无顺序依赖。

**Acceptance criteria**

- connection 与 authenticated session 生命周期各有清晰拥有者；
- reset 是生产 Interface，不是测试入口；
- 当前消息模型 9 个 testcase 保持 GREEN；
- 新 CTest case 有正确 Level 和报告。

### Plan 2.5-07 — Clean-runner convergence and develop gate

**Objective**
用同一 SHA 证明本地与 GitHub clean runner 的全量快速回归一致，并建立稳定 `develop` Required Check。

**Files expected to change**

- `.github/workflows/windows-ci.yml`
- `scripts/windows-local.ps1`
- 测试/构建文档与矩阵
- GitHub branch protection（仓库外管理动作，check 名稳定后执行）

**Tasks**

1. 在本地运行 `CheckTestStructure` 和 `RunAllTests`，保存所有报告摘要。
2. 确保 workflow 每个 job 调用相同 `Run*Tests` 子入口并 `if: always()` 上传报告。
3. required job 失败、超时、缺报告或 cleanup failure 均非零；禁止 `continue-on-error`。
4. 在 clean PR runner 运行完整快速门禁；根据真实日志做最小修复，不能根据耗时猜测。
5. 对每种 runner 至少保留一份可恢复 RED-GREEN 证据。
6. 稳定 job/check 名后，由仓库管理员把它们设为 `develop` branch protection Required Check。
7. 更新基线 testcase 数量、报告、运行时间和剩余 Phase 3 gap。

**Verify**

```powershell
.\scripts\windows-local.ps1 -Task CheckTestStructure
.\scripts\windows-local.ps1 -Task RunAllTests -Configuration Release
git diff --check
```

CI 侧验证所有 required jobs、JUnit artifacts、发布目录基础检查和 failure propagation。不得用重新运行获得一次偶然绿色替代根因修复。

**Acceptance criteria**

- 本地和 clean runner 在同一 SHA 全绿；
- 所有报告存在、case count 与矩阵一致；
- branch protection 只引用已稳定存在的 check；
- 无 secret、进程、端口或临时状态残留；
- Phase 2.5 的 G-001..G-006 全部关闭或由用户明确接受剩余项。

## 7. Verification loop

每个 plan 使用以下闭环：

1. **Pre-flight gate**：确认合同、文件所有权、依赖类别、当前 testcase 数量和无个人服务。
2. **RED**：临时破坏断言、fixture 或受控生产行为，观察对应 runner 非零。
3. **GREEN**：恢复/最小修复后运行 focused suite。
4. **Regression**：运行 owning toolchain 的全部 suite。
5. **Structure**：运行 `CheckTestStructure`。
6. **Clean runner**：相关 wave 完成后使用 PR runner 验证。
7. **Revision gate**：若验收不满足，修订实现/计划；不得弱化已确认合同。

同一问题最多进行三次无进展修订。若 blocker/warning 数量不下降，停止并提交 Escalation gate，由用户决定调整合同、拆分范围或延后；不得自行宣布通过。

## 8. Phase completion criteria

- [ ] DG-01..04 已确认并写入对应 Module README。
- [ ] 现有 95 个 runner testcase 均保留，新增数量已更新矩阵。
- [ ] Qt Level 和 PowerShell 逐 testcase 报告准确。
- [ ] 共享 proto 漂移与旧 wire 兼容可自动检测。
- [ ] Gate 敏感响应回归为 required。
- [ ] Gate/Status CLI、bind、ready、stop 具有确定 Integration 合同。
- [ ] C++ gRPC deadline 与 pool close/borrow 均有有限合同。
- [ ] Qt reconnect/session reset 不继承旧状态。
- [ ] `CheckTestStructure`、`RunAllTests` 和 `git diff --check` 通过。
- [ ] 同一 SHA 的 clean GitHub CI 全绿且报告完整。
- [ ] `develop` branch protection 的 Required Check 已稳定配置。
- [ ] 剩余 G-007..G-018 保持明确归属，没有被误报为已覆盖。

## 9. Decision and gap coverage

| Decision/Gap | Covered by |
| --- | --- |
| D-01、D-05 | 2.5-01、2.5-07，全量快速合同与同入口 |
| D-02 | 本阶段建立 develop lane；master/release 明确为后续 scope |
| D-04 | 所有 pre-implementation gate 和合同变更规则 |
| D-08 | 2.5-04..07 的超时与完整性优先 |
| D-09 | 2.5-07 failure propagation、无 retry 刷绿 |
| D-10 | 2.5-07 先采集 changed-lines baseline，不制造无意义测试 |
| D-11、G-002 | 2.5-02 当前/历史协议基础 |
| G-001 | 2.5-04 |
| G-003 | 2.5-05 |
| G-004 | 2.5-03 |
| G-005 | 2.5-06 |
| G-006 | 2.5-01 |

D-03、D-06、D-07、D-12 与 G-012..G-018 在治理规范中已锁定，但其执行属于 Phase 3C、3D 和 release gate，不在 Phase 2.5 假装完成。
