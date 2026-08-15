<!-- generated-by: gsd-doc-writer -->
# C++ 原生代码规范

## 语言基线

- Server 和 Qt 新代码 MUST 兼容 C++17。
- Server 当前使用 Visual Studio 2022 v143；新 Server 代码 MUST 避免依赖 MSVC 专有扩展，为后续 Linux 编译保留空间。
- Qt 客户端当前使用 Qt 6.5.3 MinGW；不得假定 MSVC 和 MinGW 的 ABI、警告或标准库行为完全相同。
- 新代码 MUST 在启用标准一致性检查的编译配置下工作，禁止依赖未定义行为。

## 命名

Server C++ 新代码遵循：

| 元素 | 规则 | 示例 |
| --- | --- | --- |
| 类型、类、结构体 | PascalCase | `ConnectionPool`, `SessionState` |
| 公共成员函数 | PascalCase，保持现有 Server API 一致 | `GetSession`, `Close` |
| 局部变量、参数 | snake_case | `server_address`, `message_id` |
| 私有成员 | 前导下划线 + snake_case | `_worker_thread`, `_is_stopped` |
| 常量、枚举值、宏 | UPPER_SNAKE_CASE | `MAX_LENGTH`, `LOGIN_COUNT` |
| 命名空间 | 小写 snake_case | `chat_protocol` |

- 名称 MUST 表达业务含义和单位，例如 `timeout_seconds`，不得只写 `t` 或 `val`。
- 布尔成员 SHOULD 使用 `_is_`、`_has_`、`_should_` 等判断式名称；修改既有字段时可逐步迁移。
- 新类型的 `.h` 和 `.cpp` SHOULD 与类型名保持一致，并确保大小写完全匹配。
- Qt 的函数和信号槽命名例外见 [Languages.md](Languages.md)。

## 头文件与包含

- 头文件 MUST 使用 `#pragma once` 或一致的 include guard；项目新文件优先使用 `#pragma once`。
- include 顺序 SHOULD 为：对应头文件、C++ 标准库、第三方库、项目头文件；各组之间空一行。
- 头文件 MUST 直接包含其公开声明所需的类型，不能依赖包含顺序偶然成立。
- 能前置声明的项目类型 SHOULD 前置声明，避免在公共头文件引入 gRPC、MySQL 或大型 Boost 头。
- 公共头文件 MUST NOT 使用 `using namespace`。
- `#include` 路径大小写 MUST 与磁盘文件一致。
- `message.pb.*` 和 `message.grpc.pb.*` MUST 由 proto 工具生成，不得手工编辑。

## 所有权与资源

- 资源生命周期 MUST 使用 RAII 表达，包括线程、socket、锁、文件、数据库连接和临时状态。
- 独占所有权使用 `std::unique_ptr`；只有确实共享生命周期时才使用 `std::shared_ptr`。
- 裸指针和引用默认不拥有对象，接口文档必须能判断其有效期。
- 禁止用 `new`/`delete` 管理可以由标准容器或智能指针管理的对象。
- 异步对象使用 `shared_from_this()` 前 MUST 确保对象由 `std::shared_ptr` 创建。
- callback 捕获 MUST 审核生命周期；禁止捕获将在 callback 执行前失效的局部引用。
- 析构函数不得抛出异常；清理操作需要报告失败时，应提供显式 `Close`/`Stop` 并保持析构兜底安全。

## 值、参数与返回

- 所有变量 MUST 在声明时初始化。
- 小型标量按值传递；只读大型对象使用 `const T&`；需要转移所有权时按值接收并 `std::move`。
- 返回值 MUST 表达失败语义。不得在同一层混用“返回 false”“抛异常”“写日志后继续”而没有清晰契约。
- 不可忽略的返回结果 SHOULD 使用能促使调用者检查的接口设计；调用者不得无理由丢弃 Redis、MySQL、绑定和启动结果。
- `std::stoi` 等转换 MUST 验证完整消费、范围和异常；端口必须限制在 1..65535。
- 与网络长度相关的 `size_t`、有符号短整型和协议字段转换 MUST 显式检查范围后再转换。

## 容器与缓冲区

- 优先使用 `std::string`、`std::vector`、`std::array` 管理存储。
- 使用裸缓冲区时 MUST 同时保存容量、有效长度和终止条件，任何复制前都要验证源与目标范围。
- 网络输入 MUST 在分配或读取 body 前校验消息长度和允许的最大值。
- 禁止用 `memcpy`、`memset` 绕过非平凡类型的构造和析构。
- 迭代容器时修改容器 MUST 明确迭代器失效规则。
- 共享容器的线程安全由拥有该容器的类负责，不能要求所有调用者“自行小心”。

## 类与接口

- 类 SHOULD 只有一个主要职责；配置解析、网络传输、持久化和业务逻辑不得无边界地堆在同一类。
- 默认将成员设为 `private`，只公开稳定且可测试的操作。
- 禁止为了单元测试复制私有算法；需要测试时优先提取无状态逻辑或最小接口 seam。
- 复制和移动语义 MUST 明确。拥有 mutex、thread、socket 或连接的类型通常应删除复制。
- 重写虚函数 MUST 使用 `override`；不再允许派生时可使用 `final`。
- 单参数构造函数 SHOULD 使用 `explicit`，除非隐式转换是明确设计。

## 错误和日志

- 捕获异常优先使用 `const std::exception&`。
- 禁止空 `catch`；忽略失败时必须说明为什么安全。
- 同一个错误 SHOULD 在最有上下文的一层记录一次，随后传播，不要每层重复打印。
- 日志不得包含密码、Token、验证码、完整连接字符串或原始私密消息。
- 启动阶段不可恢复的错误 MUST 令进程返回 `EXIT_FAILURE`。

并发与关闭细节以 [Concurrency.md](Concurrency.md) 为准，配置和日志细节以 [Operations.md](Operations.md) 为准。
