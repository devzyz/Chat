# 项目当前状态

更新：2026-09-18。此页维护当前进度、下一步与验证边界；计划和阶段总结保留历史证据。
项目目标：先完成基本聊天功能，后续主要投入可测量的性能优化。

## 已合并基线

- PR #2～#6 已合入 develop；本地及远端 develop 为 `719236e5cd7b80908a7b530f6e872b508fd9df2c`。
- 3B 传输集成、3C 当前版本真实依赖、3D 双实例消息与恢复已有验收记录。
- [合并后 CI 35337424539](https://github.com/devzyz/Chat/actions/runs/35337424539) 成功，
  Windows 静态检查、Server、Qt、VarifyServer 与 Regression checks 于北京时间 9 月 18 日 22:27 完成。
- 本轮依赖恢复 0 个包、编译 111 个包，安装约 3.3 小时；develop v3 缓存已保存约 1 GB。
  下一轮实际暖缓存复用仍待确认。Linux/full/release 本轮按 develop push 规则跳过。

## 本轮 CI 去重与旧方案清理

工作分支 `fix/ci-redundancy`，基于上述 develop；工作树 `Cache/worktrees/simplify-ci-regression`。

- Server 由 RunServerTests 一次构建生产与测试目标，移除之前的独立 BuildServers 调用。
- 静态 job 检查一次全仓测试注册；其成功后四个 CI 测试入口复用结果，测试与报告校验保持。
  本地入口默认检查，RunAllTests 在同一进程内缓存成功检查结果；跳过参数仅允许 CI 测试入口使用。
- phase3dEvidence.test.js 仅在 Linux 报告 job 执行一次；真实业务报告仍逐项校验。
- 普通 develop PR/push 不再生成或上传应用 ZIP；master、每周及手动全量保留打包，测试报告照常上传。
- 已移除 3C-08 bootstrap 入口、实现及三个占位测试；保留 schema 标记与真实协议/迁移回归。
  Linux 无用预检输出已删除，JSON PASS 断言保留。旧发布计划标记为历史方案，合同矩阵与当前策略一致。
- 本地验证：17 项 Node 定向回归、7 项发布包回归、8 项报告聚合测试通过；
  13 项脚本测试、四个工作流 actionlint、Linux 十项静态合同及十五项负向变异通过；
  Bash 语法及已退役 selector 拒绝检查通过。完整 Server/Qt 构建由本次 PR CI 验证。
  托管结果以本次 PR 精确提交的检查为准，不继承旧 PR 或上述 develop 的通过结果。
- 之前复核的 Linux 60 分钟总预算与各步骤预算协调问题仍待单独处理，本轮只修复确认的冗余项。

## 发布配置与尚未完成的验收

- 发布实现已在 develop：master push 全量通过后，组装同一次 CI 的 Windows 包，
  新 runner 下载冒烟，通过上传后字节回验，再公开 GitHub Release；没有第二次应用编译或人工审批。
- 已只读核对：Actions 已启用；master 严格要求 Regression checks 和 Full regression checks，
  管理员保护、禁止强推/删除保留。发布 job 显式声明 contents: write，未发现缺少 PAT 的配置依赖。
- 远端 master 仍为 `0622710`，尚未包含新工作流；当前没有 GitHub Release。
  因此“发布代码和权限已配置”不等于“master 已启用并成功发布”。
- 旧 release-uat / release-promotion 环境仍保留，新流程不引用它们，不会增加审批等待；未修改远端设置。
- 首次发布仍需：确认 VERSION，通过 develop→master PR 的全量检查后合并，再验证 master push
  的全量、包冒烟和实际发布。新 Linux 安装/v3 路径与首次发布均尚未完整托管验收。
- N-1 完整矩阵按当前 CI 政策暂缓；保留已有协议/schema 回归，不将缺失的跨版本验证记为通过。

## 本地功能与工作区

- 主工作区为 feature/local-avatar，仍基于 `d04512e`，包含既有文档整理和其他用户修改。
  头像支持选择、预览、圆形框拖动/缩放裁剪、256×256 PNG 保存和登录恢复；只在本机生效。
  已有 Qt Release 构建及 24/24 Unit、10/10 Component 通过记录，完整窗口和原生选择器待人工验收。
- feature/resource-stream-transfer 位于 `.worktrees/resource-stream-transfer`，仍有未提交功能。
  独立 ResourceServer、断点续传、Range 下载、摘要校验、资源消息与权限已有本地测试记录；
  Redis/Status 部分场景使用替身，真实全依赖联调、双客户端播放、8 GiB 实传与吞吐测试未完成。
- 两条功能线均未随本轮提交或推送，后续与最新 develop 集成时需处理消息模型等重叠修改。
- 保留 master、develop、两条功能分支、CI 修复分支；3A 分支仍有未合入文档提交，
  旧 release-r00 工作树仍有十条未提交变更，待核对后再处理。

## 文档与清理

- 旧原始 CI_TASKS、Phase 2 continue-here/HANDOFF 和静态链接迁移清单从 CI 修复工作树删除；
  主工作区原有相同删除状态保留。构建、测试和发布说明统一指向现有入口，移除过时数量与 R-00 审批说明。
- 已删除无修改、无忽略文件、提交已合并的 pr6-ci-budget 工作树，释放约 80 MiB；旧 PR 正文临时文件已删除。
- 其余工作树的可运行产物、依赖、有效测试证据、tests/auto、隔离目录、代理脚本及个人参考资料保留。
  本轮不恢复或修改本机 vcpkg，也不删除远端分支、缓存或审批环境。

## 下一步

1. 提交并验证本轮 CI 去重，确认 develop 暖缓存，处理 Linux 预算后完成一次手动全量验收。
2. 整理并集成头像与资源传输，完成登录、好友、消息、历史、重连及资源发送的真实依赖和双客户端验收。
3. 固定 Release 提交、机器和数据规模，建立连接数、接收端吞吐、p50/p95/p99 延迟、CPU/内存、
   队列长度、数据库/Redis 耗时和错误率基线，再据此确定性能目标。
4. 按剖析结果逐项优化线程/锁、数据库访问、内存分配、网络背压或 Qt 渲染，每项保留同负载前后对比及正确性回归。
