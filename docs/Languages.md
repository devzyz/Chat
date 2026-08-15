<!-- generated-by: gsd-doc-writer -->
# Qt、JavaScript 与脚本规范

## Qt 客户端

### 结构

- Widget 负责展示和用户交互，MUST NOT 直接实现持久化或服务端业务规则。
- HTTP/TCP 调用 SHOULD 集中在网络管理类；界面通过信号、槽或模型状态消费结果。
- 消息数据、`MessageListModel`/`MessageModelStore` 和 delegate 展示职责 MUST 分离。
- 新的可复用状态逻辑 SHOULD 放入非 Widget 类，以便在 `QT_QPA_PLATFORM=minimal` 下测试。

### 生命周期和线程

- QObject 优先使用父子所有权；不得同时由父对象和智能指针重复拥有。
- 跨线程信号槽 MUST 明确 connection type 和接收对象线程归属。
- UI 线程 MUST NOT 执行阻塞网络、磁盘、数据库或长计算。
- QNetworkReply、timer、dialog 和临时 model 的销毁路径必须明确，避免悬空 signal callback。
- 访问 QWidget 和 GUI model MUST 位于 GUI 线程。

### 命名和格式

- Qt 类型使用 PascalCase；方法、信号、槽和属性使用 lowerCamelCase，与 Qt API 一致。
- 既有 Qt 文件名以小写为主；新增成对 `.h/.cpp/.ui` SHOULD 继续使用小写且名称一致。
- Qt 信号命名描述已发生事件，槽命名描述处理动作。
- UI 文案使用 `QString` 和 UTF-8 源文件；跨 `std::string` 转换必须明确编码。

### 测试

- Model 测试验证行数、角色、索引、去重、顺序和状态，不依赖私有容器类型。
- delegate/layout 测试验证相对关系或合法范围，不固定不同系统字体的精确像素。
- 测试不得要求真实显示器、个人字体或公网服务。

## VarifyServer JavaScript

### 语法和模块

- 当前使用 CommonJS；新文件 MUST 使用 `require`/`module.exports`，除非进行一次明确的整体模块迁移。
- 新代码 MUST 使用 `const` 和 `let`，MUST NOT 新增 `var`。
- 每条语句使用分号，字符串引号在同一文件中保持一致。
- 文件、函数和变量使用 lowerCamelCase；常量对象或错误枚举使用清晰稳定名称。
- 模块加载 SHOULD 无副作用；服务只应在作为主入口执行时启动监听。

### 异步与依赖

- Promise 必须 `await`、return 或显式处理 rejection。
- gRPC handler MUST 在所有路径上只调用一次 callback。
- Redis、SMTP、UUID 和时钟 SHOULD 通过可注入边界访问，使 handler 可在无外部服务时测试。
- 捕获错误后不得只 `console.log` 并继续返回成功。
- `bindAsync` MUST 检查 error 和实际端口后再 `server.start()`。
- 验证码、邮件密码和 Redis 密码 MUST NOT 输出到控制台。

### 配置与测试

- `--config`、`CHAT_CONFIG` 和默认 `config.json` 遵循 [Operations.md](Operations.md) 的优先级。
- 配置模块 SHOULD 导出可测试的加载函数，避免依赖不可重置的 require cache。
- 新行为使用 Node 内置 `node:test`，避免无必要引入大型测试框架。
- 测试只使用临时配置和 fake 依赖，不连接真实 Redis 或发送邮件。

## PowerShell

- 脚本 MUST 开启 `Set-StrictMode -Version Latest` 和 `$ErrorActionPreference = 'Stop'`。
- 外部命令执行后 MUST 检查 `$LASTEXITCODE`；PowerShell cmdlet 错误使用终止错误传播。
- 路径参数使用 `-LiteralPath`，组合路径使用 `Join-Path`，不得依赖当前盘符。
- 删除或移动前 MUST 解析并验证绝对目标位于预期目录，禁止对工作区根、用户目录或未解析变量执行递归破坏操作。
- 复杂脚本使用带类型和 Validate 属性的 param block。
- 函数使用 `Verb-Noun` 命名；变量使用清晰 PascalCase/camelCase 并保持文件内一致。
- 启动后台进程默认使用隐藏窗口，记录 PID、可执行文件和启动时间；停止前必须验证进程身份。
- 所有轮询和等待 MUST 有有限 timeout。
- `finally` 负责恢复目录和清理本次创建的临时资源。
- PowerShell 文件保持 CRLF，与仓库文本策略一致。

## GitHub Actions

- Action MUST 固定到完整 commit SHA，并在注释中保留可读版本。
- job 和 step 名称使用可理解的英文行为描述。
- workflow 权限采用最小授权，当前构建默认只需要 `contents: read`。
- 多行 PowerShell MUST 设置严格错误策略并检查外部命令退出码。
- runner 临时数据使用 `${{ runner.temp }}`，仓库输出使用 `${{ github.workspace }}`。
- destructive cleanup 只允许删除已知 runner 临时目录，并必须验证路径不是 reparse point 或意外目标。
- cache 只保存可安全复用的下载/binary archive，不保存 installed tree、buildtrees、packages 或 Server 中间产物。
- artifact 上传必须设置 `if-no-files-found` 和合理 retention；测试报告在失败时也应保留。

## MSBuild、CMake 和配置脚本

- XML、CMake 和 YAML 修改后 MUST 先做语法验证。
- 构建逻辑优先放在共享 props/targets 或统一脚本，避免复制到每个 `.vcxproj`。
- 平台条件必须具体且最小，禁止用条件分支隐藏两套长期漂移的业务实现。
- 注释只解释非直观的工具链限制和兼容原因，不保留 IDE 模板的大段无关说明。
