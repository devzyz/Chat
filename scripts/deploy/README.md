# AutoDL 上的 Ubuntu 22.04 服务端部署

在 GitHub Actions 的 **CI → Run workflow** 选择分支和 `autodl`。默认 `skip`。
`package` 在 Ubuntu 22.04 x64 / GCC 11 上构建 Gate、Chat、Resource 与 Node 验证码服务，上传
`jammy-server-release`。`deploy` 只在 `develop` 生效，测试包就绪且本次 quick 或 full
回归门禁成功后才连接目标机。普通推送默认跳过。需要持续部署时，把**仓库变量**
`AUTODL_AUTO_DEPLOY` 设为 `true`。CI 在选择本次运行范围时记录该开关；恢复为 `false` 即关闭后续自动部署。PR、周检、
master 和性能模式不激活目标机。Windows Release 仍走原有 master 发布流程。

## 首次准备目标机

使用普通部署用户，给它独立的持久目录、Python 3.8+、Bash、Supervisor 4.x 的
`supervisord` 和 `supervisorctl`。以该用户执行
`bash scripts/deploy/setup.sh /srv/chat-deploy/app`。将专用 SSH 公钥写入该用户的
`authorized_keys`，其选项为
`restrict,command="env CHAT_DEPLOY_HOME=/srv/chat-deploy/app /usr/bin/python3 /srv/chat-deploy/app/receive.py"`。
若使用私有安装的 Python 和 Supervisor，请在强制命令中指定解释器和 PATH。
主机的 SSH 服务须允许该用户使用公钥登录。把 GitHub 环境 `autodl` 的允许部署分支
限定为 `develop`，在环境中设置以下内容。

| 配置 | 内容 |
| --- | --- |
| secret `AUTODL_DEPLOY_KEY` | 该部署公钥对应的专用私钥 |
| variables `AUTODL_HOST`、`AUTODL_PORT`、`AUTODL_USER` | SSH 主机、端口、普通部署用户 |
| variable `AUTODL_KNOWN_HOSTS` | 经实例指纹核对的完整 OpenSSH known_hosts 行；非 22 端口使用 `[host]:port` |

## 服务配置和运行

在部署目录的 `shared/` 中提供 `GateServer.ini`、`ChatServer.ini`、`ResourceServer.ini`、
`VarifyServer.json`、仅部署用户可读的 Bash 环境文件 `runtime.env`。原生配置来源见
[运行配置](../../docs/Operations.md)，让 Gate、Chat、Resource 使用同一数据库和 Redis；
四个服务的本地端口彼此独立，且可从回环就绪探针访问。Gate 的 `[VarifyServer]` 地址
与 `CHAT_VARIFY_BIND_ADDRESS` 匹配。相对资源目录和日志目录放在 `shared/` 下。
`runtime.env` 提供 `CHAT_DB_PASSWORD`、`CHAT_VARIFY_EMAIL_USER`、`CHAT_VARIFY_REDIS_PASSWORD`，
SMTP 登录时另提供 `CHAT_VARIFY_EMAIL_PASS`。使用真实业务前配置 TLS 入口及发信服务。

首次上线和数据库升级按[数据库说明](../../docs/Data.md)备份、显式迁移及 verify。
包中包含 `node` 和 `schema/migrate.js`。部署作业只切换程序。
接收端核对包和源码摘要，在主机锁内切换 `current`、启动四个 Supervisor 进程，
检查 Gate HTTP、Resource 数据库与 Redis 就绪、Chat 和验证码监听。启动失败时恢复上一版本
并复测。每个通过的版本保存在 `releases/`，业务数据、主机配置和日志独立保存。

用 `supervisorctl -c /srv/chat-deploy/app/supervisord.conf status 'chat:*'` 查看状态，
用 `shutdown` 停止该私有进程组。需要在容器重启后恢复时，在实例启动命令中启动
`supervisord -c /srv/chat-deploy/app/supervisord.conf`，然后执行
`supervisorctl -c /srv/chat-deploy/app/supervisord.conf start 'chat:*'`。实例重建或 SSH
地址、主机密钥变化后，需要重新配置接收端和可信目标。

## 验证

`node --test tests/build/ciBudget.test.js` 检查部署门禁。打包作业对包内 ELF 的
GLIBC、GLIBCXX、CXXABI 导入做上限检查，在原生 Jammy 上以独占 PostgreSQL、Redis、
端口及进程组运行 `tests/deploy/jammy-smoke.sh`，覆盖首次激活、摘要拒绝和失败回滚。
