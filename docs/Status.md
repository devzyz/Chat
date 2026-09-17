# 当前状态

## CI 精简与缓存优化：实现完成，PR #6 进入远端验证

- 基于 PR #6 的 `93eb98c`，工作树为 `Cache/worktrees/simplify-ci-regression`。
- develop PR/push 保留现有 Windows 单元、组件、loopback/进程回归和构建。
- master PR/push、每周一北京时间 03:17 和手动执行，复用同一套 Windows + Linux 全量检查。
- master push 全量成功后组装本次 CI 的 Windows 包，新 runner 下载冒烟后自动发布同一 ZIP。
- 已移除 owner 凭据、永久版本占用、首次 attempt 限制、跨工作流轮询与独立人工 UAT/promotion 门禁。
- v3 缓存按平台、系统系列和 target/host triplet 分层恢复；镜像版本仅记录，旧 v2 归档迁移一次。
- 依赖成功后、业务构建前保存缓存；摘要与七天诊断制品记录实际复用、耗时、ABI 和编译器信息。
- 业务源码与业务测试未改动；保留本机 vcpkg、主工作区和其他工作树的现有修改。
- 交付到现有 [PR #6](https://github.com/devzyz/Chat/pull/6)，以读取其 merge ref 下已保存的 v2 缓存。

## 已验证

- `f6dc147` 的 CI `35240862883` 已通过：Windows 从 v2 恢复 111 个包、重编译 0 个，并成功保存 v3；Server job 用时 12 分 46 秒。
- 本次继续修复 Linux 保存边界：独立执行 vcpkg manifest 安装并保存缓存，随后才配置项目；CMake 项目配置失败不再丢弃已安装的依赖缓存。
- actionlint 验证全部四个工作流；9 项触发/依赖/预算与缓存回归、7 项发布打包回归、报告失败传播、测试登记和 diff 检查通过。
- 使用上述 Windows CI 的真实应用 ZIP，补入本机 MSVC redistributable 和 Node 验证新组装路径。
  解压包六项检查通过：清单、三个 C++ 服务启动/正常退出、Qt 窗口启动/关闭、Node/proto/gRPC 加载。
  这证明本地脚本可用，不替代新工作流在干净托管 runner 上的验证，也不代表已发布。
- Windows 包冒烟不连接真实数据库；业务 E2E 使用同 SHA 的 Linux 真实依赖环境。

## 远端配置与后续验证

1. 本次 Linux 修复提交到 PR #6，等待最新提交的 `Regression checks`；后续全量入口验证 Linux v3 和独立安装路径。
2. 默认分支已设为 develop；新 workflow 合入 develop 后，每周定时检查才会生效。
3. develop Required Checks 已改为 `Regression checks`；master 已改为该检查加 `Full regression checks`。
   严格更新、管理员保护和禁止强推/删除设置保留；未增加人工 reviewer。
4. 在 develop 更新 `VERSION`，通过 master PR 全量检查后合并；master push 才会实际发布。

本地复核已完成：规范复核指出的发布说明已修正，需求复核无阻塞项。
Windows v3 再次恢复、Linux v3 及新发布链路仍需各自托管运行证明；未合并、未创建 Release。
`release-uat` / `release-promotion` 环境不再被新流程引用，无需审批；未删除其历史记录。
现有阶段计划保留历史证据，当前策略以 [CI-GOVERNANCE](../tests/CI-GOVERNANCE.md) 为准。
