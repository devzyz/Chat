# 本地桌面测试与补充联调记录（2026-10-04）

本轮先完成自动回归与真实后端联调，再绕过截图通道故障，分三次完成实际 Qt 桌面走查；续测扩展到四个独立客户端。
**累计记录八项待修复问题，其中四项 P1：附件历史同步阻塞、注册验证码请求错误校验、注册/改密验证码长度不一致、Escape 隐藏主聊天页。未修改产品代码。**
桌面清单共 63 项执行记录：前两次 51 项，第三次新增 12 项（10 项通过、2 项发现问题）。
前两次为 40 项通过、8 项发现问题、1 项部分通过但提示有问题、1 项观察项、1 项首次审批阻塞；
该审批阻塞已由第三次 UI-52 补测完成，当前不再等待授权。
同一缺陷可以影响多个场景；63 个场景不等于 63 个独立缺陷或自动化测试。
这里的项目是走查场景，不是 CTest 数量，也不代表所有发布验收完成。当前进度与下一步见 [Status](../Status.md)。

## 修复与复验（本次 PR）

以下原始63项记录保留测试发生时的结论；UI-BUG-01～08现已在修复分支处理，当前进度以 Status 为准。

| 问题 | 最终修复与自动回归 |
| --- | --- |
| UI-BUG-01 | 资源对账仅排除4个上传临时字段，真实内容/身份冲突仍回滚；`uploadedResourceCanonicalHistory` 覆盖私聊、群、ACK后重启及后续消息 |
| UI-BUG-02 | 认证流程保留业务错误类别，三个表单共用提示；`auth_flow.forms` 覆盖1006/1007/1009凭据错误及验证码错误 |
| UI-BUG-03 | 文件草稿显示文件名、字节数及完整名称提示；`fileAttachmentPreview` 验证实际图像不是灰块、会话切换与撤销重做 |
| UI-BUG-04 | 清除找回页初始占位提示；`resetInitiallyClear` |
| UI-BUG-05 | 首次获取注册码校验邮箱；`initialCodeRequest`、`invalidCodeRequestEmail` |
| UI-BUG-06 | 两个表单接受服务端8位码；真实表单提交、成功/错误响应及空/短/长码边界均覆盖 |
| UI-BUG-07 | 使用值复制替代向同一 JSON 对象插入时失效的字段引用；生产目录→SQLite→UserMgr 测试保留姓名/头像/性别 |
| UI-BUG-08 | 主聊天页拒绝默认 QDialog 隐藏行为；编辑器/搜索框 Escape、草稿保留及真正等待窗取消均覆盖 |

修复前新增回归已出现对应失败；首版认证夹具的单例释放错误在复跑前校正，不计产品失败。
GUI 旧密码复测进一步发现生产 Gate 使用1009，补充该码回归再次观察 RED，再修复并 GREEN。
最终 `RunClientTests -Configuration Release`：77个 CTest 条目通过（25 Unit、19 Component、33 Integration），
包含生产程序构建；`CheckConventions -ConventionBase origin/develop`、测试注册校验及 diff 检查通过。
这些 CTest 聚合入口与下面桌面场景、此前专项方法数量不同，不能相加为不重复总数。

本次实际窗口及真实隔离后端复验：

- 保留旧账号SQLite，私聊附件7由 `server_copy=0` 合并为1，游标6→7；申请姓名恢复为字符串。
  证据：`fix-alpha-cache.json`、`fix-private-restored.png`。
- 按 Escape 后聊天页仍在，见 `fix-escape-visible.png`。
- 合成新账号在注册页空验证码首次获取成功，实际8位码提交后返回登录；随后以真实重置用途8位码改密成功，
  旧密码被拒绝、新密码登录进入空会话界面。SMTP替身共接收两封邮件，没有外发。
  证据：`fix-auth-result.json`、`fix-new-password-login.png`。
- `fix-old-password-rejected.png` 捕获的是补上1009映射前的通用提示；最终精确文案由表单回环回归验证，不能把该图当最终文案截图。
- 旧群在上一轮已经解散，不能借修复绕过成员权限补回历史；群资源继续同步由临时数据库用例验证。
  原生附件选择器复测遇到工具焦点/控件缓存不一致，已取消；文件草稿展示采用组件渲染断言，不记作本次桌面通过。
- 两个复测客户端与所有测试服务已停止，10个端口关闭、MySQL正常SHUTDOWN；`fix-cleanup.json`。

本地回归日志位于忽略目录 `build/desktop-fix-*.log`，Qt报告位于 `build/test-results/client_*.xml`。
截图、合成数据与凭据仍只留在忽略目录，不随 PR 上传。下文未完成的原始场景是历史边界，以上已明确补验者除外。

## 基线与环境

- 工作区开始时干净，`develop` HEAD 为 `64b58f2`（PR #33 合并提交）。未修改生产代码或修复产品行为。
- `RunClientTests -Configuration Release` 重新构建 Qt 客户端及测试；测试安装复制到独立目录，不使用原有客户端数据。
- 五个真实服务：GateServer、StatusServer、ChatServer、ResourceServer、VarifyServer。
  C++ 服务复用上一轮本地产物，本轮没有重新构建 Server，不能宣称重新完成全部 Server 源码构建验收。
- 使用本机已有 MySQL 8.0.34 程序启动隔离实例，单独数据目录、数据库与 loopback 端口；未安装系统服务或迁移个人数据库。
- Redis 为上一轮内存 RESP 替身，SMTP 为本地接收器；没有外发邮件，也没有使用个人凭据。
- 原始证据保存在仓库本地 `build/ui-comprehensive-20261004/`，该目录不进入版本控制。
  配置与测试凭据只留在此忽略目录，不能作为公开工件上传。
- 后续真实桌面证据在 `build/ui-desktop-20261004/evidence/`；两个客户端分别使用 `client/`、`client-beta/`
  的独立安装及数据目录，账号、好友和群均为本次隔离环境的合成数据。
- 授权续测增加 `client-gamma/`、`client-delta/`，最终四个窗口分别登录 UID 1～4；同账号顶替单独先行测试。
  新账号因 GUI 注册缺陷改用本地协议夹具准备，不计作 GUI 注册成功。SMTP 续测保存合成邮件用于读取真实验证码，
  仅留在忽略目录，不外发、不开公开工件；没有修改 vcpkg 或新增依赖。

## 第一轮自动回归与协议联调

下列数量按各自报告统计，**存在重叠，不能相加作为不重复测试总数**。初始化检查也不是业务覆盖数量。

| 执行项 | 结果 | 证据（相对本轮证据目录） |
| --- | --- | --- |
| Qt 常规回归 | 76 个 CTest 条目通过：Unit 25、Component 19、Integration 32；无失败/跳过 | `client-regression.log`、`evidence/client_*.xml` |
| 资源与界面控件专项 | 19 个测试方法通过；JUnit 共 21 项中含 init/cleanup 两项，不计业务测试 | `resource-widget.xml`、`resource-widget-retry.log` |
| Varify 常规回归 | 57 项通过：Unit 35、Integration 22；无失败/跳过 | `evidence/varify_*.xml` |
| 五服务启动、迁移、认证 | 16 项通过 | `status.json` |
| 基础真实后端业务联调 | 13 项通过，unexpectedErrors 为空；没有本轮 GUI 收发消息 | `business-results.json` |
| 扩展四账号协议联调 | 28 项通过 | `extended-results.json` |
| 本轮进程与端口清理 | 后端、替身、对端和测试客户端已停止；无本轮后端存活进程/监听；MySQL 正常关闭 | `evidence/cleanup.json`、`mysql.stderr.log` |

### 覆盖范围

| 领域 | 本轮实际检查 | 证据边界 |
| --- | --- | --- |
| 登录与账号 | 错误密码、四账号注册登录、同账号顶替、改密用户名/邮箱校验、旧密码拒绝、旧 Token 撤销、退出撤销 | 真实后端协议；登录/注册/改密页面未点击完成 |
| 好友与资料 | 申请/接受、双向删除、拒绝、迟到审批拒绝、重新添加、备注、中文与 emoji 资料、搜索及资料版本冲突 | 真实后端协议；另有自动搜索/备注控件测试 |
| 私聊 | 消息提交与落库、双向回复、UUID 重放不重复、冲突 UUID 拒绝、错误会话拒绝、离线消息重登补拉 | 真实后端协议；没有本轮桌面输入收发 |
| 群聊 | 创建重放、群消息、群外拒绝、四成员、后入群历史边界、非群主改名拒绝、群主改名、移除、重新加入代次、转让/退出/解散 | 真实后端协议；自动控件检查失去成员资格、分页失败、迟到回包与管理失败提示 |
| 附件与头像 | 真实资源服务分块上传、偏移冲突、续传、完成、下载内容相同、私聊/群聊授权、被移除后拒绝下载；自动测试另覆盖头像发布/隔离、过期检查点、中断重试、页面销毁 | 协议与控件/HTTP 测试；没有本轮原生文件选择器或桌面裁剪走查 |
| 本地交互 | 草稿、撤销重做、附件提交、退出保护、历史/目录搜索、未读可见性、窗口几何、会话重置与有限重连 | Qt 自动测试；不代表实际 Windows 显示、输入法、多显示器验收 |

## 首次截图故障与环境记录

### ENV-01：标准截图通道超时（后续已绕过）

复现步骤：启动本轮独立 `client/chat.exe`，通过 Computer Use 枚举并唯一选中 Chat 窗口；
捕获窗口画面；重新枚举/绑定后重试；再尝试直接桌面只读截屏。

实际结果：

- 窗口捕获先后返回 `FrameArrived timed out: timed out waiting on channel`、
  `window capture timed out: timed out waiting on channel`。
- `CopyFromScreen` 返回“句柄无效”。没有生成可供视觉检查的截图。
- 无障碍树可读取登录表单，邮箱控件为当前焦点。
- 点击“登录”返回 `coordinate input geometry is unavailable`；Return 未产生可观测校验，Tab 后焦点未移动。
- 首次尝试请求用户确认桌面状态；随后用户要求直接截图重试，见下面的恢复结果。

分类：环境/自动化阻塞，**不能据此判定 Chat 登录按钮、键盘导航或窗口显示存在产品缺陷**。
重试结果：输入桌面为 `Default`，前台进程为 Chat，没有证据证明用户锁屏。
限定本次 Chat 进程、校验窗口归属后，使用只读 `PrintWindow` 成功取得主窗口和对话框截图；
输入继续通过 Computer Use 的 `sky` API 完成。标准捕获通道失败的具体根因仍未确认。

全桌面截图曾被自动审批拒绝，原因是其他 Chrome/Codex 窗口可能包含无关私密内容；
改为仅截图测试 Chat 后通过，没有绕过该拒绝去获取全桌面。
工具还存在刚切页后的无障碍树滞后、窗口缩放后的坐标缓存滞后、模态元素索引不可用等现象；
通过重新读取状态、绑定窗口及窗口内坐标/键盘操作继续。失败的自动化点击不计作产品失败。

### WARN-01：MySQL 连接库弃用警告（已知维护项）

Gate、Chat、Resource 的 stderr 仍出现 `MYSQL_OPT_RECONNECT is deprecated`；
与上一轮一致，本轮业务断言通过，未证明存在运行故障。
后续在独立依赖维护任务评估 Connector/libmysql 兼容性；本次没有恢复或升级本机 vcpkg。
临时 MySQL 自签 CA 警告只涉及隔离实例，不作为实际客户端 TLS 验收结果。

### 测试夹具问题（已校正，不计产品缺陷）

1. 扩展脚本最初断言资料修改响应直接包含 description，实际合同只返回新版本；
   改为再次读取权威资料后校验。失败记录：`extended-attempt1.json`。
2. 最初请求 `Range: bytes=5-19` 并期待 206，但 [资源接口](../../ResourceServer/README.md)
   只承诺 `bytes=N-`；校正为开放结尾范围，并同时断言封闭范围返回 416。
   失败记录：`extended-attempt2.json`。
3. 资源专项首次进程未产生报告，停止后使用 Qt 对应 MinGW 路径、插件路径及正确引用的
   JUnit 输出参数重新执行通过。最初未完成执行不计通过，根因未单独隔离。
4. 桌面重试期间，重置 Node 工具会话意外关闭其子进程中的隔离服务。客户端进入有限重连并回到登录页；
   之后保留 MySQL 数据，使用独立后台进程恢复服务并重新登录。不能将这次工具中断归因于产品崩溃。
5. 本人资料页的 UIA `set_value` 未产生与键盘输入相同的编辑行为；空用户名提交结果不计通过。
   改用实际键盘输入中文描述后保存并重新载入成功。

## 真实桌面走查结果

以下操作实际发生在 Qt 窗口中，截图保存在第二轮证据目录。
`desktop-cases.json` 保存逐项结果；UI-14/16/18 与 UI-26 只证明附件选择、投递、下载或显示，
不能抵消 UI-36 的历史同步缺陷。两个客户端，不是四个桌面客户端。

| ID | 场景 | 结果与证据 |
| --- | --- | --- |
| UI-01～03 | 登录空邮箱、非法邮箱、空密码 | 通过；邮箱不能为空 / 邮箱地址不正确 / 密码长度应为6～15；`empty-password.png` |
| UI-04 | 正确邮箱、错误密码 | 拒绝登录，但提示错误；见 UI-BUG-02、`wrong-password.png` |
| UI-05 | 正确登录、主界面 | 通过；`chat-login.png` |
| UI-06～07 | 服务中断、有限重连、重新登录历史 | 通过所观察路径；`reconnect-state.png`；未覆盖原连接自动恢复成功 |
| UI-08～09 | 中文、英文、emoji、多行发送和另一端离线后登录显示 | 通过；`sent-multiline.png`、`beta-history.png` |
| UI-10～11 | 双端回复、已读、后台未读与激活清除 | 通过；`alpha-reply-receipt.png` |
| UI-12 | 本机历史按“多行”搜索 | 两个匹配结果；`history-matches.png` |
| UI-13 | 中文好友备注 | 保存后标题和会话列表更新；`remark-saved.png` |
| UI-14～16 | 原生文件选择、附件草稿关闭确认、取消后发送 | 通过；`attachment-ready.png`、`draft-exit-warning.png`、`attachment-sent.png` |
| UI-17 | 普通附件草稿可识别性 | 问题；见 UI-BUG-03 |
| UI-18 | 乙端收到文件和缓存下载内容 | 75,000 字节，SHA-256 与源文件一致；`file-download.json`；未证明外部编辑器成功打开 |
| UI-19～20 | 建群空输入校验、选择成员建群 | 通过；`group-create.png`、`group-created.png` |
| UI-21 | 群草稿切到私聊再切回 | 私聊不混入群草稿，切回后恢复；`group-draft-restored.png` |
| UI-22 | 群文字消息、未读与打开 | 通过；`beta-group-notification.png` |
| UI-23 | 群主和成员权限控件 | 普通成员禁用添加/移除/转让/改名/解散；群主禁用直接退出；`member-permissions.png`、`owner-controls.png` |
| UI-24 | 群主中文改名 | 成功，甲端标题和列表更新；`image-draft.png` |
| UI-25 | 群主转让确认 | 到达确认框，确认动作被自动审批拒绝，Escape 取消；不计转让成功 |
| UI-26 | 原生选图、草稿缩略图、群内发送与乙端显示 | 通过该子流程；`image-draft.png`、`beta-group-image.png` |
| UI-27 | 后台群名同步 | 观察项：列表先更新、打开的聊天标题仍旧名，激活后恢复；`beta-group-image.png` |
| UI-28 | 键盘输入中文个人描述、保存、重新载入 | 通过；`restored-layout.png` |
| UI-29 | 头像选图、放大到110%、拖动、取消 | 通过；取消后原头像不变；`avatar-loaded.png`、`avatar-zoom-drag.png`；未发布新头像 |
| UI-30 | 最大化、恢复 | 通过观察尺寸下布局，控件可见；`maximized-layout.png`、`restored-layout.png` |
| UI-31 | 主动退出登录 | 返回空白登录表单；再次登录成功 |
| UI-32 | 注册必填、验证码长度、密码不一致、错误验证码 | 本地校验通过，服务端错误被泛化成参数错误；`register-empty.png`；见 UI-BUG-02 |
| UI-33 | 注册验证码请求（已有四位占位值） | 界面成功提示，SMTP 接收器计数1；`register-code-countdown.png`。当时表单保留 `0000`，不能证明首次空验证码路径正常，见 UI-37；文件名虽含 countdown，实际未验证倒计时 |
| UI-34～35 | 找回页初始显示、空用户名和非法邮箱 | 初始占位提示有问题；空值和格式校验通过；`reset-form.png` |
| UI-36 | 附件后离线消息、重新登录补拉 | 失败，见 UI-BUG-01；双端截图与数据库对照确认 |

### 授权后的续测（UI-37～51）

`continuation-cases.json` 记录新增 15 项：11 项通过、4 项发现问题。沿用隔离数据，无产品修复。

| ID | 实际 GUI 场景 | 结果与证据 |
| --- | --- | --- |
| UI-37 | 有效邮箱、空验证码首次获取验证码 | 失败，误报邮箱地址不正确；`r2-register-filled.png`；UI-BUG-05 |
| UI-38 | 填入本地 SMTP 收到的真实注册验证码 | 8 位验证码被 4 位校验拒绝，注册未完成；`r2-register-eight-digit-rejected.png`；UI-BUG-06 |
| UI-39 | 搜索 UID、申请好友、对端红点、拒绝 | 通过；`r2-found-alpha.png`、`r2-friend-notification.png`、`r2-friend-rejected.png` |
| UI-40 | 拒绝后重新申请、空说明、接受 | 成为好友并进入联系人；`r2-reapply-arrived.png`、`r2-friend-accepted.png`；列表姓名显示另见 UI-51 |
| UI-41 | 0 字节附件 | 正确拒绝并提示附件不存在、为空或不可读；`r2-empty-file-rejected.png` |
| UI-42 | 17 MiB 附件超过服务端 16 MiB 上限 | 拒绝且提供重试/取消，取消成功；`r2-over-limit-result.png` |
| UI-43 | 合成 AVI 视频上传、接收、下载 | 81,940 字节、SHA-256 一致；`r2-video-received.png`、`r2-video-download.json`；外部播放未确认 |
| UI-44 | 找回密码填入真实重置用途验证码 | 8 位验证码失焦即报请输入4位验证码，未提交改密；`r2-reset-eight-digit-rejected.png`；UI-BUG-06 |
| UI-45 | 原生选图、发布头像、好友端更新 | 通过；`r2-avatar-published.png`、`r2-avatar-peer.png` |
| UI-46 | 同账号在另一个独立客户端登录 | 新端进入聊天，旧端回空登录页；随后观察到“无法恢复当前会话，请重新登录”提示；`r2-account-replaced.png` |
| UI-47 | 同安装目录切换 UID 3→4 | 空登录表单；新账号没有前账号会话、消息或自定义头像；`r2-account-isolation.png` |
| UI-48 | 四账号在线、ChatServer 短时重启 | 约 3 秒后四账号自动重新认证；乙端 GUI 新消息收到服务器确认；`r2-chat-restart2.json`、`r2-reconnect-sent.png`、服务日志 |
| UI-49 | SQLite 写锁故障、解除后重试 | 对乙端数据库持 `BEGIN IMMEDIATE` 50 秒后回滚；发送提示本地保存失败、内容保留，解锁后重试成功；`r2-sqlite-locked.png`、`r2-sqlite-retried.png` |
| UI-50 | ResourceServer 不可用、恢复后重试上传 | 显示 Connection refused，保留提交；恢复同配置服务后重试得到服务器确认；`r2-resource-unavailable.png`、`r2-resource-retry-sent.png`；未覆盖中途分块续传 |
| UI-51 | 好友申请人身份显示 | 姓名空白，空说明时无法从列表判断申请者；`r2-pending-friend.png`、`r2-friend-accepted.png`；UI-BUG-07 |

关闭客户端后只读核查 SQLite：重连消息和数据库锁重试消息各一条，均有服务器消息 ID 且 `server_copy=1`，
见 `r2-persistence-check.json`。这只支持这两条消息的持久化，不抵消资源消息 UI-BUG-01。

续测夹具异常单独记录：第一次 `Stop-Process` 报空引用，旧 ChatServer 未退出，重复启动因此端口冲突；
核实 PID 与独立配置后用进程 API 重启成功，以 `r2-chat-restart2.json` 为有效注入证据。
附件选择曾误填缺少 `fixtures/` 的路径，Windows 正确提示找不到文件，校正后继续；不计产品失败。

### 四账号与社交生命周期续测（UI-52～63）

本次沿用隔离数据，由四个真实 Qt 进程分别登录甲/乙/丙/丁（UID 1～4），通过窗口输入和限定窗口截图操作。
此前群管理审批阻塞已解除，转让、移除、重加、退出和解散均实际点击完成；没有改用协议来代替 GUI 操作。
独立 SQLite 仅用于只读核对，不直接修改联系人、成员或消息。12 项中 10 项通过、2 项发现问题。

| ID | 场景 | 实际结果与证据 |
| --- | --- | --- |
| UI-52 | 群主转让及双端权限 | 通过；群 2 由甲转给乙，甲管理禁用/可退出，乙管理启用/不可直接退出；`r3-transfer-old-owner.png`、`r3-transfer-new-owner.png` |
| UI-53 | 多申请与批量添加成员 | 流程通过；丙丁分别申请并由乙接受，再批量加入群，显示四成员；姓名空白仍属 UI-BUG-07；`r3-four-group-members.png` |
| UI-54 | 主聊天页收到 Escape | 发现 UI-BUG-08；丁及甲窗口变空白，进程仍存活，重启恢复；`r3-delta-search-exit.png`、`r3-alpha-escape-state.png` |
| UI-55 | 四账号群收发 | 发现 UI-BUG-01；丙丁收到四账号消息，甲乙因旧附件冲突漏收其他成员消息；`r3-multi-account-matrix.json` |
| UI-56 | 新成员历史边界 | 通过；丙丁只有入群后 ID 15 起消息，没有旧群历史；`r3-delta-group-joined.png`、`r3-multi-account-matrix.json` |
| UI-57 | 移除成员后禁发 | 通过；丁即时只读、编辑及发送禁用，已保存历史保留；`r3-delta-removed.png` |
| UI-58 | 重新入群代次与恢复 | 通过；丁缺席期间 ID 20 不可见，重新入群后可发 ID 21，丙收到；`r3-rejoin-boundary.json`、`r3-rejoin-delivered.png` |
| UI-59 | 主动退群及群主刷新 | 通过；丁只读，乙收到资料变化提示并刷新为三人；`r3-delta-left.png`、`r3-before-dissolve.png` |
| UI-60 | 解散及跨端权限同步 | 通过；乙解散后甲乙丙均只读，丁此前已退出；`r3-owner-dissolved.png`、`r3-alpha-dissolved.png`、`r3-gamma-dissolved.png` |
| UI-61 | 群解散后的独立私聊 | 通过；丁乙双向发送 ID 22/23，有未读及已读表现，仅出现在 UID 2/4 的会话 4；`r3-private-unread-beta.png`、`r3-private-reply-delta.png` |
| UI-62 | 好友双向删除 | 通过；丁删除乙后联系人移除，乙已有会话即时只读、旧历史保留；`r3-beta-friend-deleted.png` |
| UI-63 | 删除后重新申请/接受 | 通过；乙重申、丁接受后恢复可写，新消息 ID 24 送达，仍为会话 4且旧消息保留；`r3-friend-restored-delivered.png` |

最终四账号核对见 `r3-final-account-matrix.json`，12 项明细见 `multi-account-cases.json`。
ID 22～24 仅在乙丁本地数据库且 `server_copy=1`，甲丙没有这些私聊；丁没有离群期间的 ID 20。
群消息 ID 15/16/17/19 分别由乙/丁/丙/甲 GUI 发出，丙丁均存有正确的发送者和会话号；
甲乙群同步游标仍卡在 8/13，不能将权限变化成功表述为其消息历史同步成功。

工具缓存也出现独立问题：登录窗口尺寸变化及模态切换后，无障碍树/点击几何有时陈旧，重建工具会话后恢复。
甲首次计划发送群消息时因这一工具误操作，实际发到了甲丙私聊（ID 18）；核对标题后重新发送群消息 ID 19。
该次误发不计产品串号；其余隔离断言以实际 `chat_id`、`sender_id` 与截图为准。

## 待修复问题

### UI-BUG-01 / P1：本人发送附件后，该会话历史同步被阻塞

复现（全程真实 GUI）：

1. 甲乙登录；甲在私聊选择 `ui-test.txt` 并发送，再在双人群选择 `ui-image.png` 并发送。
2. 乙能收到文件和群图片；文件 SHA-256 一致，不能据此判断发送端同步正常。
3. 甲主动退出；乙发送“附件之后的离线群消息 FOLLOWUP-20261004”。
4. 甲重新登录并打开该群，仍只有原群文字和图片，缺少新消息。

实际证据：服务端消息10已落库，乙端也有10且群游标为10；甲端只有7/8/9，私聊游标为6、群游标为8。
甲端附件7/9 的 `server_copy=0`，重复补拉时出现 `Server message UUID identity conflict`。
观察与截图持续跨越重新登录，不是单次立即读取 UI 的缓存滞后。

定位线索：[LocalMessageStore](../../chat/localmessagestore.cpp) 的 `sameMessageContent` 比较整个资源 JSON 对象；
本地发送内容含 `offset`、`owner`、`ready`、`upload_id`，服务端规范内容只有
`media_type`、`name`、`resource_id`、`sha256`、`size`。同 UUID 和资源 ID 仍因额外字段不等而被判冲突。
这是源码和两侧数据库对照支持的原因定位，尚未通过修复后的 RED/GREEN 验证。

证据：`attachment-sync-stalled.png`、`followup-sent-beta.png`、`profile-before.png`（仅错误提示窗口）、
`local-sync-comparison.json`、`server-message-comparison.tsv`。
后续修复需保留对真实身份/内容冲突的拒绝，添加资源规范化内容比对及“发附件→离线新消息→重登补拉”的回归；
私聊和群聊均须复验。不要用清空用户 SQLite 来规避。

### UI-BUG-02 / P2：认证业务错误统一显示“参数错误”

正确邮箱加错误密码，登录被拒绝但只显示“参数错误”；注册表单提交不存在的四位验证码也如此。
账号/密码不匹配、验证码失效或错误应给出可操作且不泄露账号存在性的提示。
证据：`wrong-password.png`；相关映射位于 [LoginDialog](../../chat/logindialog.cpp)、
[RegisterDialog](../../chat/registerdialog.cpp)、[ResetDialog](../../chat/resetdialog.cpp) 的 `showAuthError`。
重置错误映射仅源码确认，本轮没有完成真实改密提交。

### UI-BUG-03 / P2：普通文件草稿只有灰色块，没有文件名

原生选择器选择 `ui-test.txt` 后，编辑区只显示灰色矩形；取消关闭确认后仍如此。
发送成功后气泡才显示文件名。多附件时用户难以确认将要发送的内容。
证据：`attachment-ready.png`；图片草稿有正常缩略图，见 `image-draft.png`。
后续为非图片附件增加文件名、类型或大小的可辨认呈现，并覆盖草稿恢复。

### UI-BUG-04 / P3：找回密码初始页残留“错误提示”占位文案

从登录页进入找回密码，尚未输入或提交即显示绿色“错误提示”；空值校验后才显示具体红色错误。
证据：`reset-form.png`。后续初始化时清空/隐藏提示，明确成功和失败状态。

### UI-BUG-05 / P1：注册首次获取验证码错误地要求验证码已存在

填写有效合成邮箱、保留验证码为空，点击“获取”，界面显示“邮箱地址不正确”，没有按预期请求验证码。
[RegisterDialog](../../chat/registerdialog.cpp) 的 `on_get_code_clicked`（119 行）调用 `checkVarifyValid`，
而非邮箱校验。已有四位占位码时才能走到请求，解释了 UI-33 为何未发现该问题。
证据：`r2-register-filled.png`。后续修复应覆盖空验证码首次请求、有效/无效邮箱和请求失败恢复，不能靠预填假码。

### UI-BUG-06 / P1：真实验证码 8 位，注册与找回页面只接受 4 位

本地 SMTP 实际收到注册/重置用途的 8 位验证码，填入 Qt 页面分别被拒绝或失焦报“请输入4位验证码”。
[VarifyServer](../../VarifyServer/server.js) 从 UUID 截取前 8 位；
[RegisterDialog](../../chat/registerdialog.cpp) 366 行和 [ResetDialog](../../chat/resetdialog.cpp) 253 行均固定校验 4 位，提交也调用该校验。
因此完整注册和改密成功路径仍是产品阻塞，不能用本轮协议夹具注册成功代替。
证据：`r2-register-eight-digit-rejected.png`、`r2-reset-eight-digit-rejected.png`。
后续统一验证码合同，增加真实签发→邮件接收→GUI 提交端到端回归；不能仅测试手填四位错误验证码。

### UI-BUG-07 / P2：好友申请列表没有申请人姓名

丙向甲申请后，甲“新的朋友”行显示头像和申请说明，但姓名为空；拒绝后以空说明重申，行内只剩头像和操作按钮。
接受后行内只有“已添加”，而联系人列表能正常显示“桌面测试丙”。不同申请人使用默认头像时难以辨认。
证据：`r2-pending-friend.png`、`r2-friend-accepted.png`。根因未确认；排查申请通知资料与
[ApplyFriendItem](../../chat/applyfrienditem.cpp) 姓名渲染，并覆盖空说明、接受前后和重新登录后的显示。

### UI-BUG-08 / P1：Escape 隐藏整个主聊天页

丁端在聊天搜索框清空后按 Escape，窗口只剩空白主框，侧栏、会话列表和聊天区全部消失，进程仍存活。
甲端主聊天页收到 Escape 时再次出现同样现象；关闭并重新启动客户端、登录后恢复。
证据为实际像素截图 `r3-delta-search-exit.png`、`r3-alpha-escape-state.png`，以及 `r3-delta-blank-tree.txt`。
这与无障碍树缓存陈旧不同：重新截取窗口本身仍为空白，需要重启产品进程恢复。

定位线索：[ChatDialog](../../chat/chatdialog.h) 继承 `QDialog`，
[MainWindow](../../chat/mainwindow.cpp) 将它设为 central widget；目前未见聊天页覆盖 `reject`/`keyPressEvent`。
疑似对话框默认 Escape 处理使嵌入页面隐藏，尚未完成根因修复验证。
后续应明确主页面与真正弹窗的 Escape 行为，回归普通聊天、搜索、编辑器、弹窗取消和草稿保留，不能仅禁用所有 Escape。

### 观察项与审批边界

- 群名后台刷新时列表和当前聊天标题更新时机不同；激活窗口后恢复。先记录 UI-27，不宣称已确认长期不一致。
- 群转让确认框只有“确认执行所选操作？本地已保存历史会保留。”，没有操作名或目标成员，建议改善。
- 自动审批拒绝确认群主转让，理由是全面测试未明确授权该组和接收人的权限变更。已取消，未尝试其他途径完成转让。
- 用户随后授权后续测试，续测仍两次收到自动审批拒绝，认为该回复未明确指定群和接收者。
  已提出具体确认：隔离库 `isolated_chat`、群 2“改名后的测试群”、Real alpha UID 1→Real beta UID 2，
  及该合成群的移除/重加/退出/解散。当时取消确认框，没有通过协议绕过拒绝。
- 用户再次授权后，第三次续测的相关 GUI 操作获准并执行成功，见 UI-52～60；上述拒绝仅保留为历史记录。

## 清理与剩余验收

首次两个客户端清理证据为 `cleanup.json`、`cleanup-ports.json`。
续测四个客户端、隔离服务与 runner 均已停止，两项故障注入后启动的替代服务也已清理。
10 个本轮端口连接检查均关闭，MySQL 日志确认正常 SHUTDOWN；证据：`r2-cleanup.json` 和 `mysql.stderr.log`。
第三次续测也正常关闭四个客户端，再由 runner 关闭隔离后端；全部 10 个端口关闭，进程检查无残留，
MySQL 正常关闭。证据：`r3-cleanup.json`、`r3-before-cleanup-status.json`、`mysql.stderr.log`。
仅保留复现数据、截图和日志，没有删除原有产物或用户数据。

以下仍未完成实际 GUI 验收，不能用前面的自动测试/协议联调替代：

| 范围 | 剩余项目 |
| --- | --- |
| 认证 | 完整注册/改密被 UI-BUG-05/06 阻塞；修复后补真实成功路径、请求超时与快速重复操作；账号切换隔离已完成 |
| 好友 | 申请/拒绝/重申/接受、双向删除后重加及消息恢复已完成；申请姓名显示需修 UI-BUG-07 |
| 群管理 | 四成员收发、增删成员、转让、退出、解散及失去资格后禁发已完成；甲乙历史同步缺陷需修 UI-BUG-01 后复验 |
| 附件 | 视频播放、外部程序打开未确认；中途分块中断续传仍待验证；已完成空/超大文件及连接失败重试；完整重登补拉先修 UI-BUG-01 |
| 系统交互 | 已有四个真实客户端；Escape 空白窗口需修 UI-BUG-08；最小化通知、真实输入法、多显示器、不同 DPI 仍需目标环境走查 |
| 故障注入 | 已完成短时 ChatServer 重启自动恢复、SQLite 写锁重试、资源服务连接失败重试；磁盘只读/空间不足、长时间离线、休眠与进程强停持久化仍待验证 |

另未执行真实 Redis、外部 SMTP、真实多机 LAN、防火墙、证书部署、硬件故障和长时间压力测试。
本轮不声明完整发布验收、全部测试通过或局域网部署已就绪。
