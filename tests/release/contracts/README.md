# 发布包验证

CI 用 `.github/workflows/release.yml` 复用本次 Windows 构建包，不单独重新编译。
master 全量成功后自动组装、下载冒烟、发布；没有 owner receipt、永久版本登记或环境审批。
应用 ZIP 仅在 master PR/push、每周和手动全量生成；普通 develop PR/push 仍运行打包合同测试，
但不生成或上传整套应用包。只有 master push 触发公开发布。

## 本地回归

```powershell
python tests/release/contracts/run_contracts.py
node --test tests/build/ciBudget.test.js
```

使用临时 ZIP 验证缺文件、修改字节、错误 SHA、版本、配置清理、proto 布局、失败报告以及草稿重试与发布保护。
测试自动发现，不手工维护精确数量或 Test ID 清单；JUnit 输出到 `build/test-results/release_candidate_contracts.xml`。
合成字节测试不是实际程序启动证明。

## 包与运行验证

```powershell
python scripts/release/package.py --artifacts <本次CI制品目录> --output <新目录> --sha <完整SHA>
python scripts/release/smoke.py --directory <ZIP所在目录> --sha <完整SHA> --report <JUnit路径>
```

输入包含 GateServer.zip、StatusServer.zip、ChatServer.zip、chat-client.zip、VarifyServer.zip；每种恰好一份。
Server ZIP 必须携带 MSVC redistributable，Varify ZIP 携带 node.exe；组装时补入相邻 proto 和版本化 SQL。
真实运行配置替换为空白模板；使用说明随包提供，详见 [INSTALL](../../../scripts/release/INSTALL.md)。

Windows 冒烟验证摘要、文件、Gate HTTP/Status 与 Chat 监听启动、独占控制台停止、Qt 窗口和包内 Node/gRPC 模块加载。
程序只使用包内运行库，PATH 不引入构建工具目录；没有真实 MySQL/Redis，不声称覆盖 Windows 业务 E2E。
同 SHA 的 Linux 完整业务 E2E 必须先成功。冒烟失败不发布，JUnit 即使失败也上传。

`VERSION` 在合入 master 前更新，使用 x.x.x。Release 标记为 v<版本>。
未发布草稿可在同 SHA 下重试并重新上传；已公开版本拒绝覆盖。上传后重新下载核对实际字节，再公开草稿。
`publish.py` 仅允许 master push，使用当前 workflow 的 GITHUB_TOKEN，不需要 PAT 或人工审批。
