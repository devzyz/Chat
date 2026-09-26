# 仓库冗余清理

本计划基于 `develop` 提交 `7f41441e8e093da59b197a92dd1f46b10ef8b78c` 的源码和实际调用关系执行。
范围是删除无消费者的产物与实现、合并已确认相同的维护点；不清理本地运行数据、不改写 Git 历史、不恢复依赖。
当前验证结果见 [Status](../Status.md)。

| 项目 | 目标与落实 | 验收方式 |
| --- | --- | --- |
| A1 旧 MySQL DLL | 删除三套服务目录中的 6 个已无链接/复制引用的 DLL，合计 77,042,688 字节；继续使用 vcpkg 与 app-local 部署 | 从独立工作树构建正式服务，运行真实 EXE 的启动回归 |
| A2 旧 Qt 气泡 | 删除 `bubbleframe`、`chatbubble`、`picturechatbubble`、`textchatbubble` 的 8 个源码/头文件及 `chatbubble.ui`；它们未加入 CMake 且无外部引用 | Qt 正式程序构建及客户端回归 |
| A3 Status 数据库副本 | 删除未调用的 `MysqlDao`/`MysqlMgr`、项目条目、配置校验和示例段；CMake 的 Status 目标不再链接 connector；启动、四/五进程、发布冒烟夹具同步去掉 MySQL 段 | 无 MySQL 配置的真实 Status 启动测试，拓扑合同及发布合同测试；真实 Redis 业务回归由 Linux 服务环境验证 |
| A4 重复基础设施 | 删除 Chat/Status 无外部调用的两份 `DistLock` 和 Redis 包装方法；Gate/Status 的相同滚动日志实现收敛到 `common/logging/RotatingLog.h`，服务保留配置适配 | 服务构建、日志/关闭/配置回归、Linux/MSBuild 源码归属检查 |
| A5 兼容入口 | 核实后保留 `/get_test`、`start.bat`、`TestPhase1`；不以旧名字推断无用 | 检索正式调用方与兼容说明，保持现有消费者可运行 |

## 保留依据

- `/get_test` 被 `scripts/release/smoke.py`、`tests/services/fourProcessCases.js`、
  `fiveProcessCases.js` 和 Server 启动/HTTP 传输回归使用，是监听就绪探针。
- `start.bat` 只委托统一协议生成入口，见 [proto README](../../proto/README.md)。
- `TestPhase1` 是明确告警后转发 `RunAllTests` 的兼容别名，仍见于用户维护的
  `tests/auto/` 资料；保留这些资料原样，统一命令见 [测试入口](../../tests/README.md)。
- Asio pool 的服务包装已共用底层实现，保留职责明确的薄适配。
- Chat 的日志实现与 Gate/Status 并非完全相同，本轮只合并已确认相同的两份实现。

## 验证边界

删除源码不能单独证明运行时依赖完整，必须用独立工作树构建并运行实际产物。
Linux CMake 配置合同只证明目标图与源码归属，不能代替 Linux 原生编译及 Redis 多进程业务测试。
从当前 Git 树移除 DLL 不缩减已有提交的历史体积。
