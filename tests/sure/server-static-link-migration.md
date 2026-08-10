# 服务端静态链接迁移验收清单

> 本文件是下一会话执行改造后的验证清单，不是自动化测试代码。
> 每项应记录：执行时间、配置、结果、失败日志位置和修复提交/改动。

## 1. 改造前保护

- [ ] 已执行并保存 `git status --short`。
- [ ] 已确认没有覆盖用户当前未提交改动。
- [ ] 已备份现有可运行的 Debug/Release EXE 和配套 DLL。
- [ ] 已备份四个服务的 `config.ini`，但没有把密码内容复制到测试报告。
- [ ] 已记录旧 EXE SHA256。
- [ ] 已记录旧 EXE DLL 导入表。
- [ ] 已确认旧产物可在迁移前正常启动。

## 2. vcpkg 环境

- [ ] `VCPKG_ROOT` 指向明确、可复现的 vcpkg 安装。
- [ ] vcpkg 能解析 baseline `fc3be1ebea7eaeb3071fe716ac65713af1f3a146`。
- [ ] 使用 triplet `x64-windows-static`。
- [ ] manifest 安装成功。
- [ ] Boost 解析为 1.90.0。
- [ ] MySQL Connector/C++ 解析为 9.1.0，并启用 `jdbc`。
- [ ] spdlog 解析为 1.17.0。
- [ ] spdlog 没有启用外部 fmt feature。
- [ ] Protobuf 解析为 6.33.4，或者已经使用同一安装目录的工具重新生成代码。
- [ ] Debug 和 Release 库均存在。

## 3. 构建配置检查

- [ ] 四个主要服务 `.vcxproj` 不再包含 `D:\VisualStudio\boost`。
- [ ] 四个主要服务 `.vcxproj` 不再包含 MySQL 8.3.0 绝对路径。
- [ ] 不再手工声明 `mysqlcppconn.lib`。
- [ ] 不再手工声明 `mysqlcppconn8.lib`。
- [ ] GateServer 不再使用 `xcopy *.dll`。
- [ ] C++ 标准保持 C++17。
- [ ] Release 使用 `/MD`，Debug 使用 `/MDd`。
- [ ] ChatServer1 和 ChatServer2 使用不同输出目录。
- [ ] 每个服务能通过 `CHAT_CONFIG` 或输出目录中的 `config.ini` 加载正确配置。

## 4. Debug x64 构建

- [ ] StatusServer Rebuild 成功。
- [ ] GateServer Rebuild 成功。
- [ ] ChatServer1 Rebuild 成功。
- [ ] ChatServer2 Rebuild 成功。
- [ ] ResourceServer Rebuild 成功。
- [ ] `Chat.sln` Debug x64 全量 Rebuild 成功。
- [ ] 构建日志中没有从手工 Boost/MySQL 路径读取文件。
- [ ] 没有 Debug/Release 库混用警告。
- [ ] 没有 LNK2038 RuntimeLibrary 不匹配。
- [ ] 没有 Protobuf 版本检查错误。
- [ ] 没有 fmt/spdlog 符号冲突。

## 5. Release x64 构建

- [ ] StatusServer Rebuild 成功。
- [ ] GateServer Rebuild 成功。
- [ ] ChatServer1 Rebuild 成功。
- [ ] ChatServer2 Rebuild 成功。
- [ ] ResourceServer Rebuild 成功。
- [ ] `Chat.sln` Release x64 全量 Rebuild 成功。
- [ ] 没有 Debug 库进入 Release 链接。
- [ ] 所有 Release EXE 位于各自独立输出目录。

## 6. 静态链接验证

对每个新服务 EXE 检查 PE 导入表。

- [ ] 未导入 `mysqlcppconn-9-vs14.dll`。
- [ ] 未导入 `mysqlcppconn8-2-vs14.dll`。
- [ ] 未导入 `hiredis.dll` 或 `hiredisd.dll`。
- [ ] 未导入 `jsoncpp.dll`。
- [ ] 未导入 `libprotobuf.dll`、`libprotobufd.dll` 或 lite 版本。
- [ ] 未导入 `abseil_dll.dll`。
- [ ] 未导入 `cares.dll`。
- [ ] 未导入 `fmt.dll` 或 `fmtd.dll`。
- [ ] 未导入 `re2.dll`。
- [ ] 未导入 `libssl-3-x64.dll`。
- [ ] 未导入 `libcrypto-3-x64.dll`。
- [ ] 未导入 `zlib1.dll` 或 `zlibd1.dll`。
- [ ] 允许导入 MSVC CRT 和 Windows 系统 DLL。

额外验证：

- [ ] 临时将旧第三方 DLL 目录从 DLL 搜索路径中隔离后，新服务仍能启动。
- [ ] 新服务不会因为项目源目录中的旧 MySQL DLL 是否存在而改变行为。

## 7. 配置加载

- [ ] ChatServer1 使用 `ChatServer1/ChatServer/config.ini`。
- [ ] ChatServer2 使用 `ChatServer2/ChatServer/config.ini`。
- [ ] GateServer 使用 `GateServer/GateServer/config.ini`。
- [ ] StatusServer 使用 `StatusServer/StatusServer/config.ini`。
- [ ] 每个服务日志中记录的配置路径与预期一致。
- [ ] 配置缺失时服务给出明确错误，而不是静默使用另一个服务的配置。
- [ ] ChatServer1 和 ChatServer2 的端口、服务标识没有串用。

## 8. MySQL 9.1 JDBC

- [ ] MySQL 连接池初始化成功。
- [ ] `get_mysql_driver_instance()` 正常。
- [ ] `get_driver_instance()` 正常。
- [ ] `connect()` 正常。
- [ ] `setSchema()` 正常。
- [ ] 普通 SELECT 正常。
- [ ] PreparedStatement 参数绑定正常。
- [ ] INSERT/UPDATE 正常。
- [ ] 存储过程 `CALL` 路径正常。
- [ ] ResultSet 字符串、整数和 `int64` 读取正常。
- [ ] SQLException 的错误码、SQLState 和日志正常。
- [ ] 连接心跳 `SELECT 1` 正常。
- [ ] 数据库短暂断开后重连逻辑正常。
- [ ] 应用退出时连接资源能够释放。

## 9. Redis/hiredis

- [ ] 四个相关服务均能建立 Redis 连接。
- [ ] 基本 GET/SET 正常。
- [ ] 连接池取出和归还正常。
- [ ] Redis 断开后错误日志正常。
- [ ] 重连路径正常。
- [ ] 分布式锁加锁成功。
- [ ] 分布式锁释放成功。
- [ ] 锁超时或竞争路径符合原行为。

## 10. gRPC/Protobuf

- [ ] StatusServer gRPC 服务启动成功。
- [ ] GateServer 到 StatusServer 的 RPC 成功。
- [ ] GateServer 到 VarifyServer 的验证 RPC 成功。
- [ ] ChatServer 到 StatusServer 的 RPC 成功。
- [ ] ChatServer1 与 ChatServer2 相关 RPC 正常。
- [ ] 请求/响应序列化正常。
- [ ] 非法请求返回预期状态码。
- [ ] 服务不可达时超时和错误日志正常。
- [ ] 没有 Protobuf runtime version 错误。

## 11. GateServer HTTP

- [ ] GateServer 监听成功。
- [ ] 注册入口正常。
- [ ] 登录入口正常。
- [ ] 验证码/邮件验证路径正常。
- [ ] 错误参数返回原有错误结构。
- [ ] StatusServer 不可用时返回可控错误。
- [ ] MySQL/Redis 不可用时不会崩溃。

## 12. ChatServer

- [ ] ChatServer1 能独立启动。
- [ ] ChatServer2 能独立启动。
- [ ] 两个 ChatServer 能同时启动。
- [ ] 两个 EXE 没有因输出同名而互相覆盖。
- [ ] 客户端 TCP 连接正常。
- [ ] 用户登录后的会话建立正常。
- [ ] 好友申请/认证流程正常。
- [ ] 私聊创建正常。
- [ ] 消息发送、缓存和持久化正常。
- [ ] 历史消息分页加载正常。
- [ ] 断线与异常连接清理正常。

## 13. 日志

- [ ] 每个服务能够创建日志文件。
- [ ] `SPDLOG_TRACE` 在预期配置中生效。
- [ ] `SPDLOG_ERROR` 和异常信息正常。
- [ ] rotating sink 正常轮转。
- [ ] 不需要 `fmt.dll`/`fmtd.dll` 即可记录日志。
- [ ] 多服务日志文件不会写入同一错误目录。

## 14. 整个项目重新编译与运行

- [ ] MSVC 服务端 Debug x64 全量 Rebuild。
- [ ] MSVC 服务端 Release x64 全量 Rebuild。
- [ ] Qt/MinGW 客户端 Debug 重新构建。
- [ ] Qt/MinGW 客户端 Release 重新构建。
- [ ] Qt 客户端仍动态加载正确的 Qt6 和 MinGW runtime DLL。
- [ ] `VarifyServer/package-lock.json` 与安装结果一致。
- [ ] `VarifyServer` 启动成功。
- [ ] StatusServer 启动成功。
- [ ] ChatServer1 启动成功。
- [ ] ChatServer2 启动成功。
- [ ] GateServer 启动成功。
- [ ] ResourceServer 能启动或完成其当前控制台行为。
- [ ] Qt 客户端启动成功。
- [ ] Qt 客户端能够完成注册/登录。
- [ ] Qt 客户端能够进入聊天并发送消息。

## 15. 回归和恢复

- [ ] 新 Debug 构建失败时，旧 Debug 运行快照仍可使用。
- [ ] 新 Release 构建失败时，旧 Release 运行快照仍可使用。
- [ ] 迁移没有覆盖原始配置文件。
- [ ] 迁移没有删除数据库或 Redis 数据。
- [ ] 迁移没有修改业务协议字段。
- [ ] 迁移没有无关格式化大量业务源码。
- [ ] 全部验收完成前没有删除旧 MySQL DLL。
- [ ] 全部验收通过后再清理旧 DLL 和旧动态输出。

## 16. 最终通过条件

只有同时满足以下条件，才能认定迁移完成：

1. Debug/Release x64 全量构建成功。
2. 新服务端 EXE 不再依赖第三方业务 DLL。
3. MSVC CRT 和 Windows 系统 DLL 依赖符合预期。
4. MySQL、Redis、gRPC、HTTP、聊天和日志测试全部通过。
5. ChatServer1/ChatServer2 能同时运行且产物不互相覆盖。
6. Qt 客户端和 Node 验证服务仍保持原有行为。
7. 旧可运行快照仍保留，直到新 Release 完成至少一次完整回归。
