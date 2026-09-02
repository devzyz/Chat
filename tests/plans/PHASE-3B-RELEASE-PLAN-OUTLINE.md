# Phase 3B～Release gate 分块执行计划大纲

状态：**Planned（outline only；尚未执行）**

依据：[`PHASE-3B-RELEASE-DECISIONS.md`](PHASE-3B-RELEASE-DECISIONS.md) 的 DG-09..DG-25（全部 Confirmed）与
[`PHASE-3B-RELEASE-RESEARCH.md`](PHASE-3B-RELEASE-RESEARCH.md)。当前 12-report/180-testcase baseline、四个稳定
Windows `develop` check 名称以及 G-008..G-018 的完成状态均不因本大纲改变。

## 执行边界

- 只有 Phase 3A 全部完成后才可执行 3B；3C 只在 3B 完成后执行；3D 只在 3C 完成后执行；Release gate
  只在 3D 完成后执行。下表依赖形成单向无环图。
- 所有列出的新 Interface、Adapter、runner、report、fixture、migration、artifact schema 与文件类别都是
  **planned**，不是当前仓库事实；正式单 plan 文件必须以执行时 preflight 复核后才能锁定精确路径与 symbol。
- 每个 observable behavior plan 按 focused `RED → GREEN` 收口，只在失败传播、lifecycle、兼容或 gate 行为适用时做
  meaningful mutation，并在代码稳定后运行一次 owning public runner；每个 phase 末尾只运行一次完整 lane。
  新增 testcase 数量在真实注册后更新 manifest，不在计划中预填。
- 所有运行资源必须由 planned `RunContext` 以 run-id、deadline、identity-scoped cleanup 与 teardown ledger
  管理；报告、日志、manifest、artifact 与 UAT 证据不得含合成或真实 secret。
- 禁止 `clearForTest`、测试宏、fake-mode EXE、复制 parser/业务算法、private container 穿透或以静态 fixture
  冒充跨版本进程证据。production caller 与测试共用同一 production Interface；只有 production +
  in-memory/local 两个真实 Adapter 都存在时才建立 internal port。
- DG-25 保护 `D:\vcpkg\test-vcpkg` 与 `D:\git\Chat\vcpkg_installed`：本机路径默认只读，本地 build/test
  关闭 manifest 自动安装并在缺包时 fail closed；任何 package/path/triplet/baseline/tool identity 变更逐次取得用户明确
  批准。hosted runner 只使用与本机隔离的 run-owned 临时 install root。

## Plan / Wave 大纲

| Plan ID | Objective | Wave | Depends On | Gap / DG coverage | 预期文件类别（均为 planned） |
| --- | --- | ---: | --- | --- | --- |
| 3B-00 | 冻结 IntegrationHost、RunContext、ProcessHarness 与 report/Test ID 合同，并证明 3A production Module 可由正式 composition root 与测试 host 共用 | 1 | Phase 3A complete | G-008..G-011、G-015；DG-09、DG-12、DG-19 | Integration test infrastructure Interface、Module README/Test Plan、测试注册与结构门禁 |
| 3B-01 | 落地 Windows Process Adapter、协议级 ready、bounded log/stop/kill escalation 与 identity-scoped teardown ledger | 2 | 3B-00 | G-015；DG-12、DG-19、DG-20 | ProcessHarness/RunContext 实现、Windows process Adapter、focused lifecycle tests、runner glue |
| 3B-02 | 通过 production Beast Gate transport + 3A Gate Module + in-memory Adapters 验证 Gate HTTP，并用 production `QNetworkAccessManager` Module 做 Qt HTTP loopback | 3 | 3B-01 | G-009、G-011；DG-14、DG-19、DG-20 | Gate/Qt HTTP transport Module 接线、IntegrationHost composition、HTTP/Qt Integration tests、Module README |
| 3B-03 | 通过 production Status gRPC service/stub + 3A Status Module + in-memory Adapter 验证 deadline/cancel/refused/late completion/port conflict | 3 | 3B-01 | G-010、G-015；DG-14、DG-19、DG-20 | Status gRPC production target 接线、IntegrationHost、gRPC Integration tests、Module README |
| 3B-04 | 通过 production Chat acceptor/session/frame path + 3A Modules 验证 Chat TCP，并用 production `QTcpSocket` Module 覆盖分片/合包/边界/畸形/中断/backpressure/cleanup | 3 | 3B-01 | G-008、G-011、G-015；DG-14、DG-19、DG-20 | Chat/Qt TCP transport Module 接线、IntegrationHost、TCP/Qt Integration tests、Module README |
| 3B-05 | 注册 3B deterministic loopback/process suite 到现有 Windows `develop` Required gate，按实际 case 更新 manifest 并完成一次全 lane 收口 | 4 | 3B-02, 3B-03, 3B-04 | G-008..G-011、G-015；DG-09、DG-11、DG-12、DG-14、DG-20 | Windows runner/workflow/structure gate、JUnit manifest、矩阵/治理文档、phase summary |
| 3C-00 | 先做四应用 Linux configure/compile/link/start preflight，锁 hosted Ubuntu、compiler、CMake、Qt 获取方式与 vcpkg triplet；任一同源 proof 失败即停止 | 5 | 3B-05 | G-015；DG-09、DG-10、DG-18、DG-23 | 顶层 CMake/presets/vcpkg triplet preflight、Linux proof workflow、build evidence（不含应用 Docker 镜像） |
| 3C-01 | 将 Windows/Linux 正式 EXE、IntegrationHost 与 tests 统一到同一 production library targets，并把 Win32/POSIX 差异收进 platform Adapters | 6 | 3C-00 | G-015；DG-10、DG-18、DG-19 | shared production CMake targets、existing MSBuild parity wiring、POSIX process/signal/path Adapter、structure checks |
| 3C-02 | 证明 GitHub hosted Ubuntu disposable Redis/MySQL/Mailpit topology、随机端口、health、synthetic credential 表达式与 `always()` evidence/cleanup | 7 | 3C-01 | G-012..G-015；DG-11、DG-12、DG-23 | hosted Ubuntu workflow/service definitions、dependency coordinator、secret-redaction/cleanup reports |
| 3C-03 | 建立应用拥有的 SchemaMigration Module、checksum ledger、fresh 0→N synthetic fixture 与 N-1→N 审计/恢复合同 | 8 | 3C-02 | G-013、G-017；DG-12、DG-13、DG-15、DG-21、DG-24 | versioned migration/schema files、migration CLI/Module、synthetic fixtures、migration Integration tests/audit schema |
| 3C-04 | 通过真实 disposable Redis 验证 C++/Node production Adapters 的 atomic TTL、deadline、disconnect/restart、bad-connection eviction、recovery 与 pool teardown | 8 | 3C-02 | G-012；DG-09、DG-11、DG-12 | Redis production Adapter/lifecycle policy、real-Adapter Integration tests、service evidence/cleanup registration |
| 3C-05 | 建立 MessageCommit 深 Module 与 MySQL production Adapter：认证 sender、whole-batch explicit transaction、`UNIQUE(send_id, client_msg_uuid)`、Created/Existing/Conflict 和零部分提交 | 9 | 3C-03 | G-013、G-016；DG-15、DG-24 | MessageCommit Interface/implementation、MySQL Adapter、schema migration、concurrency/fault Integration tests、client/proto mapping |
| 3C-06 | 让 Varify production SMTP Adapter 支持运行时 endpoint/security/auth 配置，并以 Mailpit API 验证 success/failure/deadline/no-public-network/no-secret | 8 | 3C-02 | G-014；DG-09、DG-11、DG-12 | SMTP production Adapter/config validation、Mailpit Integration tests/API evidence、Node runner registration |
| 3C-07 | 在一个 RunContext 中原生启动 Gate/Status/Chat/Varify 与真实 disposable dependencies，验证共享状态、协议 ready、失败恢复和逆序 teardown | 10 | 3C-04, 3C-05, 3C-06 | G-012..G-015；DG-09、DG-11、DG-12、DG-18、DG-19 | four-process scenario driver、real config templates、topology/evidence/teardown reports、master runner wiring |
| 3C-08 | 建立 N/N-1 artifact resolver、manifest verifier、schema fixture Interface 与实际进程兼容矩阵；首发无不可变 N-1 时只输出稳定 bootstrap/block 诊断 | 11 | 3C-07 | G-017；DG-13、DG-15、DG-21 | compatibility resolver/verifier、descriptor/wire + artifact matrix tests、schema fixture metadata、diagnostic reports |
| 3C-09 | 将 Linux same-source build、real Adapter、migration、four-process 与适用 compatibility 纳入 hosted Ubuntu `master` gate，并只运行一次完整 3C lane | 12 | 3C-08 | G-012..G-015、G-017；DG-09、DG-10、DG-11、DG-18、DG-23 | master workflow/runner、report manifest、CI governance/matrix、phase summary |
| 3D-00 | 冻结 production-shape synthetic dataset 与双 ChatServer/双 production-module client-driver topology，分配独立 server identity、endpoint 与 namespace | 13 | 3C-09 | G-016；DG-12、DG-16 | versioned fixture/scenario schema、two-server/two-client topology driver、RunContext extensions、Test Plan |
| 3D-01 | 执行验证码→注册→Gate 登录/发现→Chat Token 登录→好友申请/接受，并证明两个客户端始终连接不同 ChatServer | 14 | 3D-00 | G-016；DG-16 | friend/auth E2E scenario、state assertions、master E2E report registration、Module README |
| 3D-02 | 验证私聊 commit 后跨实例投递、授权失败、相同 UUID retry 返回相同 `message_id`、不同 payload conflict 与通知去重 | 15 | 3D-01 | G-016；DG-16、DG-24 | cross-instance messaging E2E、peer RPC evidence、DB/client observable assertions、mutation cases |
| 3D-03 | 覆盖 ACK 丢失、断线重登、原 UUID bounded retry、route recovery、历史多页边界、server-ID 顺序与通知/历史重复去重 | 16 | 3D-02 | G-016；DG-12、DG-16、DG-24 | reconnect/history E2E scenarios、client model/session assertions、fault schedules、teardown evidence |
| 3D-04 | 在真实 N-1 artifact 存在时运行 N-1 client→N server、N client→N-1 server 与 N/N-1 peer RPC 业务流；否则稳定阻断而不伪造通过 | 17 | 3D-03 | G-017；DG-13、DG-16、DG-21 | cross-version client/server driver、artifact-only compatibility inputs、supported/unsupported diagnostics |
| 3D-05 | 将固定双实例 E2E 与适用 N/N-1 业务矩阵接入 hosted Ubuntu `master/release` lane，完成一次全 3D lane 与清理审计 | 18 | 3D-04 | G-016、G-017；DG-09、DG-11、DG-12、DG-16、DG-20、DG-23 | master/release workflow/runner、E2E JUnit/topology/cleanup reports、矩阵/治理文档、phase summary |
| R-00 | 从已通过 3D 的同一 SHA 仅构建一次统一 Windows x64 staging set，生成逐文件 SHA-256、内部 manifest 与可执行的依赖清单/SBOM fallback | 19 | 3D-05 | G-018；DG-09、DG-12、DG-17 | release staging/build script、Gate/Status/Chat/Qt/Varify packages、config templates、proto/migrations、manifest/hash/dependency inventory |
| R-01 | downstream 只下载并校验 R-00 artifact，执行 install/config/start/upgrade/stop/port-release smoke 与适用 N/N-1；禁止 checkout 后重建被测二进制 | 20 | R-00 | G-017、G-018；DG-13、DG-17、DG-21 | artifact-only consumer workflow、manifest verifier、smoke/upgrade harness、compatibility/cleanup reports |
| R-02 | 对 R-01 同一 digest 执行版本化人工 UAT 清单与签署；缺陷阻断并将可自动化项路由回对应 3B/3C/3D gate | 21 | R-01 | G-018；DG-17、DG-22 | versioned UAT checklist、evidence schema/validator、blocking human checkpoint、defect routing record |
| R-03 | 原样 promotion 同一 bytes 到 tag/Release asset，复核 digest 与证据链；失败回滚 promotion 元数据并回到对应 gate，首个成功产物登记为完整 N-1 baseline | 22 | R-02 | G-017、G-018；DG-13、DG-17、DG-21、DG-22 | promotion/tag/release workflow、durable release manifest/hash、rollback/stop evidence、N-1 baseline pointer、release summary |

## Coverage audit

| Source | IDs | Owning plans | Status |
| --- | --- | --- | --- |
| CONTEXT | DG-09 | 3B-00/05, 3C-03/04/06/07/09, 3D-05, R-00 | COVERED |
| CONTEXT | DG-10 | 3C-00/01/09 | COVERED |
| CONTEXT | DG-11 | 3B-05, 3C-02/04/06/07/09, 3D-05 | COVERED |
| CONTEXT | DG-12 | 3B-00/01/05, 3C-02/03/04/06/07, 3D-00/03/05, R-00 | COVERED |
| CONTEXT | DG-13 | 3C-03/08, 3D-04, R-01/03 | COVERED |
| CONTEXT | DG-14 | 3B-02/03/04/05 | COVERED |
| CONTEXT | DG-15 | 3C-03/05/08 | COVERED |
| CONTEXT | DG-16 | 3D-00..05 | COVERED |
| CONTEXT | DG-17 | R-00..03 | COVERED |
| CONTEXT | DG-18 | 3C-00/01/07/09 | COVERED |
| CONTEXT | DG-19 | 3B-00..04, 3C-01/07 | COVERED |
| CONTEXT | DG-20 | 3B-01..05, 3D-05 | COVERED |
| CONTEXT | DG-21 | 3C-03/08, 3D-04, R-01/03 | COVERED |
| CONTEXT | DG-22 | R-02/03 | COVERED |
| CONTEXT | DG-23 | 3C-00/02/09, 3D-05 | COVERED |
| CONTEXT | DG-24 | 3C-03/05, 3D-02/03 | COVERED |
| REQ | G-008 | 3B-00/04/05 | COVERED（planned；未标 complete） |
| REQ | G-009 | 3B-00/02/05 | COVERED（planned；未标 complete） |
| REQ | G-010 | 3B-00/03/05 | COVERED（planned；未标 complete） |
| REQ | G-011 | 3B-00/02/04/05 | COVERED（planned；未标 complete） |
| REQ | G-012 | 3C-02/04/07/09 | COVERED（planned；未标 complete） |
| REQ | G-013 | 3C-02/03/05/07/09 | COVERED（planned；未标 complete） |
| REQ | G-014 | 3C-02/06/07/09 | COVERED（planned；未标 complete） |
| REQ | G-015 | 3B-00/01/03/04/05, 3C-00/01/02/07/09 | COVERED（planned；未标 complete） |
| REQ | G-016 | 3C-05, 3D-00..05 | COVERED（planned；未标 complete） |
| REQ | G-017 | 3C-03/08/09, 3D-04/05, R-01/03 | COVERED（planned；未标 complete） |
| REQ | G-018 | R-00..03 | COVERED（planned；未标 complete） |

## Stop conditions inherited by every single-plan task

- Phase 3A/previous phase completion evidence missing，或 production Module 尚不能由 caller/test 共用：停止，不创建平行实现。
- Linux compile/link/start preflight、hosted service credential proof、exact image/action/toolchain pin 或 disposable cleanup
  失败：停止该后续链，不把部分成功描述为 Linux/Integration ready。
- 需要个人凭据、固定开发 endpoint、共享数据库、公共 SMTP、真实用户数据或输出 secret 值：停止并重设计 seam。
- 需要 fake EXE mode、test macro、`clearForTest`、private container 穿透、复制 production parser/算法或测试 target
  重复列 production `.cpp`：停止并深化 Module/Interface。
- fresh schema、N-1 artifact/schema、rollback 支持或 release permissions 当前不存在时，保留可审计 bootstrap/block
  结果；不得编造已存在能力或伪造 pass。
- 任一 Required runner 不可用、超时、报告缺失、cleanup 失败、Test ID/manifest 漂移或当前 180 baseline 回归：失败并阻断。

## OUTLINE COMPLETE

本大纲包含 **26 plans / 22 waves**；依赖图从 Phase 3A complete 单向流向 3B、3C、3D 与 Release gate，
DG-09..DG-24 及 G-008..G-018 均已至少映射一次。
