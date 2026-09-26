# 用户目录、头像与资源传输

本文定义当前资源与头像合同。消息持久化见 [MessageStorage](MessageStorage.md)，实际验证范围见 [Status](Status.md)。

## 客户端目录合同

所有新用户数据以 `QCoreApplication::applicationDirPath()/data` 为根，和启动工作目录无关。
安装目录必须可写；写入失败报告错误，不回退到 AppData 或系统缓存目录。
路径由 `chat/userstoragepaths.*` 统一生成，使用规范化 Gate URL 的 SHA-256 隔离环境，再按登录 UID 隔离账号：

```text
<安装目录>/data/environments/<环境SHA256>/users/<登录UID>/
  static/head/<头像所属UID>/<resourceId>.png
  static/head/<头像所属UID>/index.json
  static/head/<当前UID>/current.png          # 旧本地头像迁移副本
  files/images/<resourceId>.png|jpg
  files/videos/<resourceId>.mp4|avi
  files/documents/<resourceId>.<安全扩展名>
  transfers/uploads/<任务摘要>.upload.json
  transfers/uploads/avatar-<UUID>.png       # 未完成头像发布的暂存文件
```

未完成下载使用目标文件旁的 `.part`，完成长度与 SHA-256 校验后才重命名。
头像索引使用 QSaveFile 原子替换；更新使用新资源 ID，不覆盖旧版本文件。
用户名、服务器返回的原始文件名均不作为目录名。普通附件仅保留安全的扩展名。
退出取消网络请求、销毁头像会话和聊天页面，但保留已经保存的文件及续传记录。
登录后的异步回调不能更新另一个账号。安装目录分账号是应用逻辑隔离，不是操作系统访问控制。

旧 `AppLocalDataLocation/avatars/v1/<原始Gate地址SHA256>/<uid>.png` 只读迁移：
新目录没有 current.png 时才读取、验证并复制，保留源文件；新目录已有头像时不覆盖。
文件传输旧工作树的系统缓存不自动迁移，避免在缺少环境标识时混入其他服务的数据。
用户资料和好友由服务端及会话缓存提供；消息已接入账号级 SQLite、持久化发送和增量同步，
消息数据库与资源文件共用账号根目录，详见 [MessageStorage](MessageStorage.md)。
应用内默认头像仍打包在 `:/res` 中。

## 头像发布与读取

头像复用 ResourceTransferManager 的上传协议，裁剪为 256×256 PNG 后暂存并上传。
上传完成再调用头像发布接口；上传成功本身不会替换当前头像。
发布完成、安装目录内头像文件及索引保存成功后更新本机界面。
发布失败保留旧图并允许重试；服务端已提交但本地索引写失败时明确报告“已发布但本地缓存失败”，
后续刷新可恢复。成功后删除本次暂存 PNG，续传任务记录可供相同内容复用。

本版将头像引用放在 ResourceServer 管理的 `user_avatar` 表中，以 HTTP 读取作为权威来源，
不修改旧 `user.icon` 字段或其 Redis 缓存，避免头像依赖聊天消息格式与跨服通知协议。
旧客户端保持原先 icon 显示；新客户端按 UID 获取头像，旧 icon 仅用于内置资源回退。
查找用户、联系人、好友申请、资料页及聊天列表使用统一头像加载入口。
会话中已查看的用户每 30 秒刷新一次，自己发布后立即更新；本版没有新增 TCP 推送消息 ID。
断网时显示已缓存头像，首次无缓存使用默认头像。

除 `/health` 外沿用 `X-User-Id` 和 Bearer token，由生产 Status Login RPC 鉴权。

| 请求 | 合同 |
| --- | --- |
| `GET /avatars/{uid}` | 已登录用户可读取头像描述；未设置返回 `{}`；非法 UID 返回 400 |
| `PUT /avatars/{uid}` | JSON `resource_id`；只能改自己的头像，资源必须归自己且上传完成 |
| `GET /resources/{id}` | 上传者、当前已发布头像的已登录查看者、或已提交附件消息的参与者可下载 |

头像发布验证 1 MiB 上限、PNG 签名、256×256 IHDR、8 位 RGB/RGBA、非交错、chunk CRC、
有界 zlib 解压长度、扫描行过滤器及结束块；不接受伪装扩展名或只包含头部的图片。
普通附件的权限不会因头像可查看而放宽；替换头像后旧资源不再获得头像公开读取资格，
但已经作为聊天附件授权的资源仍遵循聊天权限。旧文件保留，本版不自动回收。

## 本地启用

1. 按 [Data 的迁移入口](Data.md#operational-entry) 应用 manifest 中的全部待执行迁移，当前为 schema 4。
   migration 003 引入资源及头像表，004 引入消息回执；空库应用 001～004，受管版本 2 应用 003、004。
   启用前备份并停止写入，迁移后更新全部 Gate/Chat/Resource 二进制；旧二进制不能混跑新 schema。
   已应用迁移保持原校验和，重复 apply 不改数据。
   未登记版本的个人数据库仍需原有受审导入流程，不能直接套用；自动测试只操作临时数据库。
2. 配置并启动 ResourceServer；`StorageRoot=data/resources` 相对于配置文件目录解析。
   默认将配置文件放在资源服务安装目录，因此文件也保存在该安装目录内。
3. 客户端 `[ResourceServer] Url` 指向该服务，头像和附件共用此地址。
4. 按 [资源服务说明](../ResourceServer/README.md) 构建与验证。

头像解压使用根 vcpkg manifest 显式声明的 zlib；本地构建保持 `VcpkgManifestInstall=false`。
ResourceServer 已接入统一构建、Windows 存储回归、完整运行打包和发布包启动冒烟。
HTTP/数据库/跨服务资源专项仍有独立入口；上述接线不代表全真实依赖 GUI 或大文件性能验收，见 [Status](Status.md)。

## 验证入口

- `chat/tests/local-avatar`：原有裁剪、原子保存、账号/环境隔离，新增安装目录约定和旧头像复制迁移。
- `chat/tests/resource-transfer`：真实 HTTP 上传与续传、两个 Qt 控制器共享头像、独立用户目录、重新创建缓存恢复、失败保留旧图。
- `tests/server/resource`：真实文件、HTTP 权限、PNG 校验、临时 MySQL 发布持久化、资源服务重启、跨 Chat 实例附件与历史。

资源集成测试使用明确的 Status/Redis fixture；不能记为真实全依赖或人工窗口验收。
