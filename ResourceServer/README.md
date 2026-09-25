# ResourceServer：流式上传、续传与聊天资源

本模块提供头像、图片、视频和普通附件的字节流传输。Qt 文件按钮选择文件，上传后通过现有
Chat TCP 和跨服 gRPC 通道发送资源描述。图片在消息列表展示；视频下载后双击由系统播放器打开。
不新增播放器、对象存储或 Redis。头像校验直接使用现有依赖树的 zlib，根 manifest 显式声明它。
头像发布、安装目录用户隔离和兼容合同见 [Resources](../docs/Resources.md)。

## 存储与复用

- `common/asio/IOServicePool.h` 是三个现有 Server 和资源服务共用的线程池实现；服务原接口保留。
- 资源服务沿用 Boost.PropertyTree INI、spdlog 轮转日志、Status `Login` 鉴权。
- `common/resource/ResourceCatalog.h` 复用已有 `rpc::BoundedPool` 的有界借用和 RAII 归还。
  未抽取旧 `MysqlPool`，因为旧实现把后台线程 detach，不能直接作为新服务的关闭基础。
- 上传任务元数据以 JSON 伴随文件保存，`.part` 实际长度是续传偏移；完成时校验 SHA-256 和媒体签名，
  重命名为不可变 `.data`。已完成资源与消息引用保存在 MySQL。
- 一个存储执行线程处理磁盘/摘要/数据库操作；网络上下文保持响应。最多 32 个连接，文件读写块为 64 KiB，
  缓冲区不随文件大小增长；PATCH 每次最多 1 MiB，Qt 每次发送 64 KiB。网络等待超时为 30 秒。
- 当前为单资源实例，文件上限默认 8 GiB，可配置；没有声明完成 8 GiB 实传或吞吐压测。

## HTTP 合同

除 `/health` 外，每次请求均携带 `X-User-Id` 和 `Authorization: Bearer <token>`。
使用 Content-Length，不支持 HTTP chunked 请求。HTTP Keep-Alive 可复用连接。

| 请求 | 输入/输出 |
| --- | --- |
| `POST /uploads` | JSON：`name`、`media_type`、十进制字符串 `size`、小写 `sha256`；返回 `upload_id/resource_id` |
| `GET /uploads/{id}` | 仅所有者；返回十进制字符串 `offset`、`size`、`ready` 等元数据 |
| `PATCH /uploads/{id}` | 原始字节流，`Upload-Offset` 必须匹配实际偏移；返回新的偏移 |
| `POST /uploads/{id}/complete` | 长度、摘要、签名校验后发布到数据库；重复调用幂等 |
| `GET /resources/{id}` | 下载完整资源；`Range: bytes=N-` 返回 206，可续传下载 |
| `GET /avatars/{uid}` | 登录后获取当前头像描述，没有头像时返回空对象 |
| `PUT /avatars/{uid}` | `resource_id`，仅本人可发布自己已完成的有效头像资源 |

上传连接中断后查询偏移再继续，不能假定客户端已发送的数据全部落盘。服务重启后也能续传。
409 表示偏移冲突或未完成；401/403 表示身份/权限；413 表示大小超限；415 表示类型/签名不符；
422 表示摘要不符；507 表示文件写入失败。只做媒体签名检查，不声称验证完整视频编码格式。
摘要不匹配需创建新上传任务；第一版不提供远程删除接口。未完成任务保留用于续传，管理员需要
根据存储目录保留策略清理废弃上传；不自动删除可能仍要续传的文件。

## 聊天合同

复用 1016/1017/1018 消息和现有 `TextChatData` protobuf 字段：1016 根对象增加 `resource_id`，
`text_array` 仅含一条 `msg_uuid`，`msg_content` 由服务端生成。
服务端确认当前 Session、私聊双方和资源所有者，在一个事务中写入 `chat_message` 和 `resource_message`。
`(sender_uid, client_uuid)` 唯一，重复请求返回同一消息 ID，不同内容复用 UUID 被拒绝。
资源消息持久化成功即成功，接收者离线不回滚消息。历史查询和跨服通知原样携带版本化描述：

```text
@resource:v1:{"resource_id":"...","name":"...","media_type":"...","size":"...","sha256":"..."}
```

这个前缀是保留的类型标记，普通文本发送不得伪造。旧客户端会看到描述文本；没有修改 protobuf 编号。
新客户端识别描述并下载。普通附件下载者须为上传者，或是已提交资源消息的参与者；当前已发布的
头像额外允许所有已认证用户读取。数据库查询使用明确的
共享资源数据合同，不链接 ChatServer 私有 DAO。

## 本地运行

1. 经现有 `schema/migrate.js plan/apply/verify` 入口迁移到版本 3；资源表的唯一来源为 `schema/migrations/003_avatar_resources.sql`，保留完整 schema 校验。不要直接执行旧工作树的独立 SQL。
2. 复制示例 INI，填写本机 MySQL/Status 配置和存储目录，不提交真实凭据。
3. 编译后运行 `ResourceServer.exe --config <配置路径>`。
4. 客户端 `config.ini` 可设置 `[ResourceServer] Url=http://127.0.0.1:8090`，默认即此地址。
5. 登录后点击文件按钮上传；再次点击暂停，重新选择同一内容文件继续。消息发送失败时双击重试。
   下载中断时双击消息继续。退出账号取消任务；头像、附件及续传信息保存在安装目录的 `data/` 内，按环境和账号隔离。

统一构建和常规回归入口为 `scripts/windows-local.ps1 -Task BuildServers` / `RunServerTests`。
Windows CI 构建资源服务、执行八项文件存储合同，并在完整运行中提供独立 ResourceServer.zip。
发布包启动冒烟使用隔离临时 MySQL，不代表资源业务全链路验收。

补充本地集成入口（不会恢复依赖或触发 CI）：

```powershell
.\scripts\resource-local.ps1 -Task Build -VcpkgRoot <已有vcpkg路径> -InstalledDir <已有vcpkg_installed路径>
.\scripts\resource-local.ps1 -Task Test -VcpkgRoot <已有vcpkg路径> -InstalledDir <已有vcpkg_installed路径> -QtRoot <Qt目录> -MinGwRoot <MinGW目录>
```

测试要求 Python，视频生成使用本机 OpenCV/NumPy，数据库测试使用已安装的 MySQL 8 `mysqld/mysql`。
缺少工具时报告失败，不自动安装。测试覆盖和证据边界见 `tests/server/resource/README.md`。
