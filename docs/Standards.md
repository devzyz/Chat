<!-- generated-by: gsd-doc-writer -->
# 代码规范总则

本目录定义 Chat 项目新增和修改代码的强制规范。规范服务于当前 Windows 可构建、可测试、可发布的基线，同时要求 Server 代码不得人为阻断后续 Linux 支持。

## 适用范围

- `GateServer/`、`StatusServer/`、`ChatServer/`、`ResourceServer/` 中的 C++ 代码。
- `chat/` 中的 Qt 客户端代码。
- `VarifyServer/` 中的 Node.js 代码。
- `scripts/`、`.github/workflows/`、MSBuild、CMake、vcpkg 和 triplet 配置。
- 测试、示例配置、协议文件和发布脚本。

第三方源码和工具生成文件不要求追溯整改，但不得手工修改来迁就业务需求。当前典型例子包括 `chat/packages/spdlog/` 和 `message.pb.*`、`message.grpc.pb.*`。

## 约束级别

- **MUST**：强制要求。新代码违反时不得合并。
- **SHOULD**：默认要求。只有存在可说明的工程理由时才能例外，并应在评审中记录。
- **MAY**：可选建议，用于在不增加复杂度时改善可维护性。

当规则冲突时，优先级为：明确的任务需求、安全与数据正确性、本文件、专项规范、局部历史风格。历史代码不能作为继续复制缺陷或漂移实现的理由。

## 修改原则

1. 每次修改 MUST 只解决任务范围内的问题，不顺手重写无关模块。
2. 修改既有文件时 MUST 遵守本规范；不要求为一次小改动格式化整个历史文件。
3. 新功能 MUST 优先扩展现有模块，禁止复制目录、类或服务形成第二套实现。
4. 行为变化 MUST 有对应的自动测试或明确记录无法自动化的原因。
5. 修复缺陷 MUST 优先增加能复现缺陷的回归测试。
6. 新依赖、公开协议变化、配置变化和发布结构变化 MUST 单独说明影响面。
7. 代码生成工具 MUST 读取本文件及任务涉及的专项规范后再生成代码。

## 通用文件规则

- 文本文件 MUST 使用 UTF-8。
- 默认换行 MUST 为 LF；`.bat`、`.cmd`、`.ps1` MUST 为 CRLF，与 `.editorconfig` 和 `.gitattributes` 保持一致。
- 标识符、文件名、日志键和协议字段 MUST 使用 ASCII 英文；用户界面文案可以使用中文。
- 文件名大小写和 `#include` 大小写 MUST 完全一致，Windows 上可用不能替代 Linux 大小写验证。
- 仓库中 MUST NOT 出现个人绝对路径、真实密码、Token、验证码、私钥或本机服务凭据。
- 构建产物、缓存、临时日志、`node_modules` 和本机状态文件 MUST NOT 提交。
- 注释 MUST 解释原因、约束或不明显的生命周期，禁止逐行复述代码。
- TODO MUST 描述后续动作和原因；不得使用无法追踪的“以后再改”。

## 命名与格式基线

- 缩进使用 4 个空格，MUST NOT 使用 Tab 作为代码缩进。
- 一行 SHOULD 不超过 120 个字符；长参数和布尔表达式应按语义换行。
- 一个声明一行；变量应在首次使用附近声明并立即初始化。
- 布尔名称 SHOULD 表达判断，例如 `is_ready`、`has_session`、`should_retry`。
- 缩写仅使用项目中稳定且明确的术语，例如 `tcp`、`rpc`、`uid`、`pid`；不得创建难以理解的新缩写。
- 代码格式调整 MUST 与行为修改保持可审查性；大规模机械格式化应独立提交。

语言特定命名、Qt 例外和 PowerShell 规则见 [Languages.md](Languages.md)，C++ 规则见 [Native.md](Native.md)。

## 规范索引

| 文档 | 负责范围 |
| --- | --- |
| [Architecture.md](Architecture.md) | 服务职责、模块边界、依赖方向和发布单元 |
| [Native.md](Native.md) | C++17、所有权、接口、资源和数据安全 |
| [Concurrency.md](Concurrency.md) | Boost.Asio、线程、队列、连接池和关闭流程 |
| [Operations.md](Operations.md) | 配置、启动、日志、错误处理和敏感信息 |
| [Protocol.md](Protocol.md) | protobuf、gRPC、HTTP/TCP 和 Redis/MySQL 数据契约 |
| [Build.md](Build.md) | vcpkg、MSBuild、Qt、npm、发布包和跨平台构建 |
| [Quality.md](Quality.md) | 测试、CI、提交、审核和完成定义 |
| [Languages.md](Languages.md) | Qt、Node.js、PowerShell 和工作流专项规则 |

## 例外流程

确需违反 MUST 时，变更说明至少包含：违反的规则、无法遵守的原因、影响范围、替代保护措施、恢复计划。单纯的“现有代码也是这样”不是有效理由。

## 完成定义

一项代码变更只有同时满足以下条件才算完成：

- 修改符合本总则和相关专项规范。
- 没有混入无关文件、个人路径或秘密。
- 相关最小测试在本地或等价干净环境真实执行。
- CI 中对应 job 能发现失败并返回非零状态。
- 新配置、协议、依赖和运行方式已同步文档或示例。
- 已知但未处理的问题被明确记录，没有通过弱化断言或吞掉错误伪装成功。
