<!-- generated-by: gsd-doc-writer -->
# 测试、CI 与审核规范

## 测试分层

| 类型 | 边界 |
| --- | --- |
| Unit | 单一逻辑单元，无真实网络、Redis、MySQL、SMTP 或跨进程依赖 |
| Component | 组合少量真实模块，通过 fake/interface 隔离外部系统 |
| Integration | 使用真实协议、临时进程或可清理的 Redis/MySQL 等依赖 |
| E2E | 从公开客户端或协议入口验证完整用户流程 |
| Stress | 长时间并发、资源和竞态验证，通常手动或 nightly 执行 |

不得把访问开发者本机 Redis/MySQL 的测试标成 Unit，也不得只运行代码不做断言来制造覆盖率。

## 新代码要求

- 新纯逻辑 MUST 有 Unit Test。
- Bug 修复 MUST 先添加能够复现问题的回归测试，除非无法安全复现并记录原因。
- 配置、协议、序列化、长度、端口和状态机变化 MUST 覆盖正常、边界和非法输入。
- 异步和生命周期变化 MUST 覆盖超时、取消、重复关闭和异常清理。
- Redis/MySQL/gRPC 适配器本身的正确性必须由 Integration Test 验证，不能完全 mock 掉被测行为。
- 测试不得发送真实邮件、使用个人凭据或污染共享数据库。

完整的测试设计标准以 `tests/auto/test-strand.md` 为准；该文件与本规范冲突时，以更严格且不伪造成功的一方为准。

## RED-GREEN

- 新测试 MUST 证明能够捕获回归：临时破坏断言、fixture 或受控 test seam 后应观察非零失败。
- 故意失败内容 MUST 在提交前完全恢复，并复跑 GREEN。
- 已有实现与预期不符时，应判断是测试错误还是潜在 Bug；不得为了全绿而弱化已确认契约。
- 测试未实际执行时 MUST 标记为 blocked 或 pending，禁止报告通过。

## 确定性和隔离

- 测试 MUST 独立于执行顺序、本机历史数据和个人绝对路径。
- 临时文件、实例名、端口、Redis key 和数据库空间 SHOULD 使用测试运行唯一标识。
- 每个可能阻塞的测试 MUST 有硬超时。
- 并发测试使用可控同步原语，不以固定 sleep 作为正确性条件。
- teardown MUST 清理本次创建的进程、文件、端口、Redis key 和数据库记录。
- 失败日志 MUST 足以定位 config、实例、PID、端口、message/chat/user id 或 seed。

## 当前测试入口

从仓库根目录执行：

```powershell
.\scripts\windows-local.ps1 -Task RunScriptTests
.\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
.\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
.\scripts\windows-local.ps1 -Task RunVarifyTests
.\scripts\windows-local.ps1 -Task TestPhase1 -Configuration Release
```

- Server GoogleTest 报告：`build/test-results/server_unit.xml`。
- Qt CTest 报告：`build/test-results/client_unit.xml`。
- VarifyServer Node Test 报告：`build/test-results/varify_unit.xml`。
- PowerShell 轻量 runner 通过逐项 PASS/FAIL 和非零退出传播失败。

## CI 门禁

`.github/workflows/windows-ci.yml` 在 develop push、PR 和手工触发时运行。新增代码必须保持：

- `static-check`：构建配置、单一 ChatServer、依赖和脚本测试。
- `servers-release`：干净 vcpkg 恢复、Server Release、GoogleTest、自包含目录和三个 ZIP。
- `client-release`：Qt Release、CTest、windeployqt 和客户端 ZIP。
- `varify-release`：Node.js 22、`npm ci`、语法/依赖、Node Test 和 ZIP。

- 不得通过 `continue-on-error`、吞掉 `$LASTEXITCODE` 或无条件成功来绕过 required 行为。
- 自动重试不得用于把 flaky test 刷成通过；必须定位不稳定原因。
- 失败时的测试报告 SHOULD 使用 `if: always()` 上传。
- 新重型依赖应复用已有 job 的已恢复环境，避免无必要重复冷构建。

## 代码审核清单

评审者至少检查：

1. 变更是否符合 [Architecture.md](Architecture.md) 的职责和依赖方向。
2. 所有权、异常、并发和关闭路径是否完整。
3. 配置、日志和错误中是否泄漏敏感信息。
4. 协议、Redis key 或 schema 是否影响其他消费者。
5. 是否增加个人路径、重复实现或无必要依赖。
6. 测试是否验证行为而非实现细节，是否真实执行。
7. 发布目录和未来 Linux Server 是否受影响。

## Git 规范

- 一个提交 SHOULD 对应一个可说明的逻辑目标。
- 提交信息 SHOULD 使用简洁英文前缀，例如 `feat:`, `fix:`, `test:`, `build:`, `docs:`。
- 测试、文档和必要构建配置可与对应行为变更同提交；大规模格式化和无关重构必须拆分。
- 提交前 MUST 检查 `git diff --check` 和 staged 文件范围。
- 禁止提交缓存、构建产物、测试秘密、临时状态和意外生成文件。

## 合并完成定义

只有相关本地测试或等价验证通过、干净 CI 全绿、artifact 正确生成、已知缺口明确记录且无规范阻断时，变更才可视为完成。
