# B：过期与矛盾内容修正

目标：读者能找到唯一的当前状态、正确的部署合同和真实的测试边界，不被历史阶段描述误导。
总入口见 [仓库复核计划](RepositoryReview.md)，执行结果只维护在 [Status](../Status.md)。

| 批次 | 范围 | 处理步骤 | 验收条件 |
| --- | --- | --- | --- |
| B1 状态与历史 | docs/Status.md | 保留已有 CI 工作区记录；把旧阶段记录归档并标注原时间范围；当前页集中说明待办、证据边界与计划入口 | 不再同时出现多个当前分支/下一步；旧记录、失败和限制可追溯；不把历史远端快照写成今日事实 |
| B2 功能与部署 | Architecture、Resources、Operations、ResourceServer README | 对照 SQLite、公共 MySQL 池、schema manifest、构建与发布实现；纠正本地存储、资源服务依赖与迁移版本 | 启用步骤使用完整当前 schema；ResourceServer 已接入构建/存储测试/打包与未验收全链路明确分开 |
| B3 测试合同 | REGRESSION、矩阵和模块 README | 清除早期阶段下一步和过期总数；核对资源升级测试与消息专项；矩阵保留 Test ID/覆盖关系 | 数量由 runner 负责；注册、执行、替身、真实依赖及桌面验收不混淆 |
| B4 名称与入口 | Standards、Native、Protocol、proto README 等当前文档 | 使用实际拆分协议名与当前公共接口；说明 start.bat 仅是协议生成兼容包装；不在本批改命令或程序行为 | 示例对应实际文件；用户能找到公开生成入口；接口拼写与实现一致 |
| B5 验证 | 所有本批文档 | 检查 UTF-8、相对链接及锚点、关键事实、历史迁移完整性和 diff；核对已有工作区修改保留 | 验证无遗漏；不运行无关业务测试或迁移个人数据库；未验证远端状态不作新结论 |

事实权威：迁移以 `schema/manifest.json` 和 [Data](../Data.md) 为准；报告注册以
`scripts/windows-local.ps1` 为准；业务范围以所属测试实现和 README 为准；命令以
[WINDOWS_BUILD](../../WINDOWS_BUILD.md)、[测试入口](../../tests/README.md) 和 [proto](../../proto/README.md) 为准。
历史文件仅保留当时记录，不持续改写为当前结果。start.bat 的移除/改名归 A5，本批只纠正用途说明。
