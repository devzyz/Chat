<!-- generated-by: gsd-doc-writer -->
# 构建、依赖与发布规范

## 当前基线

| 单元 | 工具链 | 依赖方式 | 发布方式 |
| --- | --- | --- | --- |
| GateServer、StatusServer、ChatServer | Visual Studio 2022 v143、MSBuild、C++17 | 根 `vcpkg.json` manifest | 每个 Server 独立 app-local 目录和 ZIP |
| Qt 客户端 | Qt 6.5.3 MinGW、CMake、Ninja | Qt kit 与仓库内 spdlog | `windeployqt` 后独立 ZIP |
| VarifyServer | Node.js 22（CI） | `package-lock.json` + `npm ci` | JS/JSON/proto 与 `node_modules` 独立 ZIP |

Windows 本地操作以 `scripts/windows-local.ps1` 为统一入口，详细环境说明见仓库根目录 `WINDOWS_BUILD.md`。

## C++ Server

- x64 本地默认 triplet MUST 为 `x64-windows-chat`。
- CI Release-only triplet MUST 为 `x64-windows-chat-release`，target 和 host triplet 保持一致。
- CRT MUST 使用动态链接：Debug `/MDd`，Release `/MD`。
- 第三方库默认动态链接；静态链接只能作为有记录的单库例外，禁止恢复 `x64-windows-static` 全静态方案。
- vcpkg 依赖 MUST 在根 `vcpkg.json` 声明，版本解析使用已提交的 `builtin-baseline`。
- `.vcxproj`、`.props`、`.targets` MUST NOT 添加开发者个人 include/lib/DLL 绝对路径。
- 新 Server 目标 MUST 使用统一 OutDir/IntDir 策略，不得与其他服务共享中间输出。
- Debug 和 Release 依赖名称不同的情况 MUST 用配置条件表达，不能复制项目文件。

## 依赖变更

新增或升级依赖前 MUST：

1. 证明标准库或现有依赖不能合理满足需求。
2. 检查许可证、Windows/MSVC 支持和未来 Linux 支持。
3. 明确动态/静态链接和运行时文件。
4. 更新 manifest 或 lockfile，而不是只在本机安装。
5. 在干净环境验证恢复、构建、测试和打包。
6. 评估磁盘、构建时间和 binary cache key 影响。

- 禁止提交 `vcpkg_installed`、buildtrees、packages 或 `node_modules`。
- CI 只可缓存 vcpkg binary archives 和包管理器下载缓存，不缓存已安装树或编译中间目录。
- Windows vcpkg 的 GitHub archive 下载适配器使用官方 codeload 路径，并以 port 固定的 SHA-512 校验。
  校验通过 .NET 文件流执行，不依赖子进程能自动加载 `Get-FileHash`；下载失败或摘要不符不得接受文件。
  `scripts/ci/test-vcpkg-github-asset.ps1` 在 Windows PowerShell 下覆盖命令不可用、正确摘要及失败传播。
- GitHub Release 文件先按原 repository/tag/name/browser URL 解析唯一 asset ID，再通过官方资产 API 下载；
  仍校验固定 SHA-512，不接受缺失、重复或 URL 不匹配的资产，也不增加重试。
  Linux 四个 job 通过 `scripts/ci/install-linux-tools.sh` 调用同一下载器，
  `linux-tool-assets.json` 保持 CMake 3.28.3/Ninja 1.12.1；仅在 job 临时目录解压并验证实际版本后加入 PATH。
  CMake 摘要核对官方 `cmake-3.28.3-SHA-256.txt` 后固定，Ninja 摘要由官方 v1.12.1 资产计算；
  迁移只替换失败的 Release 下载入口，不改变工具版本、vcpkg baseline 或 installed root。
- GitHub Actions MUST 固定第三方 Action 到完整 commit SHA。

## Qt 客户端

- 当前版本固定为 Qt 6.5.3 MinGW；未经专门升级验证不得修改 Qt 主次版本。
- CMake MUST 为 3.24 或更新版本；仅当 runner 识别到生成 cache 不完整时，使用
  `cmake --fresh` 恢复 compiler/Ninja 元数据，不删除整个本地构建目录。
- 所有源码、资源、UI 和测试 MUST 通过 `chat/CMakeLists.txt` 注册。
- 不得依赖 Qt Creator 私有 kit 配置或用户目录。
- 构建后 MUST 复制 `config.ini` 和 `static/`。
- Release 包 MUST 执行 `windeployqt` 并验证至少 `Qt6Core.dll`、`Qt6Widgets.dll` 和 `platforms/qwindows.dll`。
- 仓库内 `chat/packages/spdlog` 是客户端当前构建依赖；其自带 tests/bench 不计入本项目测试结果。

## VarifyServer

- `package.json` 与 `package-lock.json` MUST 同步提交。
- CI 和可复现验证 MUST 使用 `npm ci`，不得用 `npm install` 改写 lockfile。
- 发布包 MUST 只包含运行所需 JS、JSON、proto 和锁定依赖，不包含测试报告、开发缓存和真实凭据。
- npm script 增删 MUST 同步本地入口和 CI。

## 本地与 CI 一致性

- 本地和 CI MUST 调用相同的 `scripts/windows-local.ps1` 任务，不得在 workflow 中维护第二套编译逻辑。
- CI MAY 通过参数覆盖临时目录、triplet 和工具根，但不得改变编译语义。
- 构建脚本 MUST 对缺失工具、版本不匹配和依赖恢复失败返回非零状态。
- workflow 新增路径或命令时 MUST 使用 GitHub runner 可用的环境变量，禁止硬编码个人盘符。
- 本地机器磁盘不足不能成为跳过干净 CI 验证的理由，但也不得强制本地重复恢复全部依赖。

## 发布单元

- 上游 develop 检查中的 GateServer、StatusServer、ChatServer 各自目录 MUST 包含自身 EXE、`config.ini` 和运行所需 app-local DLL。
- ChatServer 发布目录 MUST 包含受支持的实例示例配置。
- 每个 Server ZIP 解压后不得依赖另一个 Server ZIP 的文件。
- Qt ZIP MUST 自包含；最终合并包 MUST 包含 VarifyServer 运行库及其相邻 proto 目录。
- artifact 命名、目录层级和必需文件变化 MUST 同步 CI 校验与文档。

master push 的全量 CI 成功后，使用 [`scripts/release/package.py`](../scripts/release/package.py)
组装本次运行的 Windows 制品，生成 `Chat-<version>-windows-x64.zip`，不重新编译业务程序。
候选配置使用无值 `.template`；Server 携带 MSVC runtime，Qt 携带 MinGW/runtime/plugins，
Varify 携带 Node runtime/锁定依赖，并由合并包的相邻 proto 目录提供协议文件。
新 Windows runner 下载该 ZIP 完成启动冒烟后，自动上传、下载回验并发布；无人工审批。
同 SHA 的未发布草稿允许重试，已发布版本不可覆盖。验证范围和命令见 [Release 模块入口](../tests/release/contracts/README.md)。

## 跨平台要求

- 当前 Windows 构建是权威可运行基线；Linux Server 尚未完成时，不得声称已经支持。
- 新 Server 业务代码 MUST 避免 Win32 专用 API；不可避免时用窄平台适配层隔离。
- 文件路径使用标准库或 Boost/Qt 路径 API，禁止手工拼接平台分隔符。
- 文件名、include 和资源路径必须大小写一致。
- socket、signal、动态库和编译器差异需要平台条件时，条件 MUST 集中在构建或适配层，不得散落在业务逻辑中。

## 构建配置审核

修改 `Chat.sln`、`.vcxproj`、MSBuild props/targets、CMake、vcpkg、triplet、package files 或 workflow 时，至少执行：语法解析、个人路径扫描、静态 triplet/旧 ChatServer 路径扫描、相关构建和对应 CI job。
