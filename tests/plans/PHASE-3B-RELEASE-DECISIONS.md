# Phase 3B～Release gate 实施前合同决策

状态：**Confirmed**

确认日期：2026-08-30

本文件记录 Phase 3B、Phase 3C、Phase 3D 与 Release gate 的锁定决策。决策来自用户完成并确认的
16 项规划问答；修改这些合同必须遵守 `tests/CI-GOVERNANCE.md` D-04，不得由执行者自行弱化。

## DG-09 — 阶段边界

- Phase 3B 只处理不依赖 Redis、MySQL、SMTP 的真实 transport/process Integration。
- Phase 3C 处理 Linux 原生构建、disposable Redis/MySQL/SMTP、schema/migration、四进程真实依赖与兼容。
- Phase 3D 处理双 ChatServer、双客户端的跨实例业务 E2E。
- Release gate 独立负责不可变 artifact smoke、版本化人工 UAT 和晋升。

## DG-10 — Docker 与应用打包范围

- Phase 3 不制作 Gate、Status、Chat、Varify 的正式应用 Docker 镜像。
- Phase 3C 必须让四个应用能够在 GitHub 托管 Ubuntu runner 上原生构建和运行；这属于 CI 可移植性，
  不等同于应用容器化。
- 正式应用镜像、容器编排和生产 Docker 部署属于后续独立阶段。

## DG-11 — CI runner 拓扑

- `develop` 保持 GitHub 托管 Windows runner 的全量快速门禁。
- 不新增 self-hosted Required runner，不让个人电脑在线状态影响合并或发布。
- Phase 3C/3D 使用 GitHub 托管 Ubuntu runner 与 disposable service containers。
- runner 或依赖不可用、启动失败、超时、报告缺失或清理失败均视为 CI 失败，不允许人工无记录绕过。

## DG-12 — 测试数据政策

- 使用版本化、生产形状一致的合成数据集，不复制真实用户数据、邮箱、Token、验证码或凭据。
- 数据集必须包含 Unicode、边界长度、重复输入、历史分页、异常关系和足以触发批量/顺序行为的规模。
- 每次运行使用唯一 run-id 隔离 schema、Redis key、邮箱、端口和临时目录。

## DG-13 — 兼容基线

- Phase 2.5 descriptor 与旧 wire fixture 继续作为初始协议基线。
- 第一次通过 Release gate 晋升的不可变 artifact 成为完整 N-1 artifact/schema 基线。
- 之后默认只验证当前版本 N 与上一正式发布版本 N-1；扩大到更老版本需要独立合同评审。

## DG-14 — Phase 3B transport 范围

- Gate HTTP、Status gRPC、Chat TCP 以及 Qt `QNetworkAccessManager`/`QTcpSocket` 均必须有真实
  loopback transport Integration。
- transport 内部依赖使用 Phase 3A 已建立的生产业务 Module 与 in-memory Adapter，不访问真实
  Redis、MySQL、SMTP 或公网。
- 3B 的确定性 loopback/process 套件属于 `develop` Required gate。

## DG-15 — MySQL schema 验证

- Phase 3C 同时验证全新数据库初始化和 N-1 正式 schema/data 到 N 的升级。
- 只有 migration 明确声明为可逆时才要求自动回滚；不可逆 migration 必须记录备份、前滚恢复和阻断条件。
- migration 运行结果、schema 版本和 fixture 版本必须可审计。

## DG-16 — Phase 3D 最小拓扑

- 最小 E2E 拓扑为两个 ChatServer 和两个客户端，每个客户端连接不同 ChatServer。
- 固定核心流程覆盖验证码、注册、登录/服务发现、Chat Token 登录、好友申请与接受、私聊、跨实例
  投递、断线重登、历史分页、顺序与去重。
- 单 ChatServer 流程不能替代跨实例验收；更大 mesh 和高并发属于 stress/soak。

## DG-17 — 第一版发布产物

- Release gate 晋升一个统一、版本化的 Windows x64 发布集合，而不是四个互不相关的临时构建。
- 集合至少包含 Gate/Status/Chat EXE、Varify Node 包、配置模板、canonical proto、migration、manifest
  与校验值。
- smoke、兼容和人工 UAT 必须使用同一个待晋升集合；通过后不得重新构建另一个产物发布。

## DG-18 — Linux CI 可运行性

- 为兑现无 self-hosted runner 条件下的真实依赖与四进程门禁，Phase 3C 必须提供四应用的 Linux 原生
  build/run 路径。
- Linux build 必须链接与 Windows 发布目标相同的生产 Module/Adapter 实现；禁止为 CI 复制第二套业务实现。
- Windows 发布仍由 Windows toolchain 产生；Linux 路径首先是 Integration/E2E 执行平台，不提前承诺
  Linux 正式发布支持。

## DG-19 — Integration test host

- Phase 3B 的 test host 组合真实生产 transport、真实生产业务 Module 和 in-memory Adapter。
- 正式 EXE 不增加 `--fake-dependencies`、测试编译宏或测试专用公开 Interface。
- 正式 EXE 的配置、启动、ready、停止由黑盒进程测试保护；`CheckTestStructure` 保护 production composition
  root 与相同 Module/Adapter 的接线。

## DG-20 — transport 故障矩阵

- 确定性 Required 测试覆盖分片/合包、最大长度边界、畸形输入、请求或写入中断、拒绝连接、deadline、
  重复/迟到 completion、端口冲突以及进程、线程、socket、端口和临时目录释放。
- 随机 fuzz、负载和长时间 soak 不进入每个 `develop` PR；它们使用 scheduled/explicit lane。

## DG-21 — N/N-1 兼容组合

- 自动矩阵覆盖 N-1 客户端→N 服务端、N 客户端→N-1 服务端、N/N-1 服务间 RPC，以及 N-1
  schema/data→N。
- rollback 只验证明确支持的组合；不支持的组合必须 fail-fast 并给出稳定诊断，不能产生静默数据损坏。
- 静态 descriptor/wire fixture 不能替代实际跨版本进程证据。

## DG-22 — Release UAT

- 仓库维护版本化 UAT 清单；自动 smoke 全绿后，由用户对同一待晋升 artifact 执行并签署结果。
- UAT 证据随 release 保存；发现缺陷立即阻断发布。
- 能自动化的 UAT 缺陷必须形成回归测试，并重新从相应分支门禁开始验证。

## DG-23 — 本地 Linux 虚拟机

- 本地 Linux VM 只是可选的开发/parity 环境，不注册为 Required self-hosted runner。
- 需要使用时通过标准 SSH key 连接；不得依赖 MobaXterm GUI、交互密码或个人会话状态。
- GitHub 托管 Ubuntu runner 的结果才是 Phase 3C/3D Linux CI 权威证据。

## DG-24 — 私聊可靠性与幂等

- 采用“允许重试、幂等持久化”，不宣称网络 exactly-once。
- 同一发送者与 `msg_uuid` 的组合只能生成一个服务端消息；重复请求返回原 `message_id`。
- 会话内以服务端 `message_id` 表达已提交顺序；通知和历史允许重复到达，客户端按稳定 ID 去重。
- schema/migration、服务端写入、跨实例投递、客户端模型和断线重试必须共同验证此合同。

## DG-25 — 本机 vcpkg 持久目录不可变与变更审批

- 本机持久 vcpkg 工具检出固定为 `D:\vcpkg\test-vcpkg`，包安装树固定为
  `D:\git\Chat\vcpkg_installed`；二者默认只读。普通构建、测试、诊断或“继续执行计划”的授权，均不包含修改这两个目录的授权。
- “修改”包括 package restore/install/remove/update/upgrade、MSBuild manifest 自动安装、删除或重建目录、清理其中内容，
  以及改变 `VCPKG_ROOT`、`VcpkgInstalledDir`、triplet、baseline、tool checkout 或 install root。
- 任一修改前必须停止，向用户展示精确命令、精确目标目录、原因、预计影响与回滚边界，并取得针对该次操作的明确批准；
  不得把构建失败解释为自动修复许可，不得删除整个安装树后重建。
- 本机普通 MSBuild/测试必须显式使用 `VcpkgManifestInstall=false` 与上述固定 `VcpkgInstalledDir`，先做只读完整性
  preflight；包缺失、状态不一致或路径漂移时 fail closed，并把恢复操作作为待批准 blocker。
- GitHub 托管 runner 只能在 workflow 明确声明的 run-owned 临时目录中按锁定 manifest/baseline/triplet 恢复依赖；
  已批准 workflow 的正常执行不需要逐次人工批准，但不得访问或改变本机持久目录。修改 CI install root、manifest、baseline、triplet
  或工具身份仍须先获得专项批准并完成 D-04 评审。

## 全局停止条件

- 任一计划要求真实个人凭据、固定开发 endpoint、共享数据库或公共 SMTP：停止并重新设计 disposable seam。
- 任一 Linux target 复制 Windows 生产算法而不是链接同一 Module：停止并重构 build ownership。
- 任一测试只能通过暴露 private container、`clearForTest` 或 fake-mode EXE 开关实现：停止并重新设计 Interface。
- 完整四进程证据尚未建立时，不得将 Adapter-only 测试描述为完整业务 Integration。
- 没有不可变 N-1 artifact 前，不得伪造完整跨版本程序矩阵；只保留已有 descriptor/wire 基线。
- 任一计划、runner 或构建将修改 DG-25 保护的本机 vcpkg 路径而没有本次明确批准：立即停止；不得通过隐式 manifest
  install、自动 restore、切换安装目录或删除重建来继续。
