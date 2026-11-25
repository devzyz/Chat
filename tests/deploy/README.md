# Ubuntu 22.04 部署验收

Jammy 打包作业运行 `bash tests/deploy/jammy-smoke.sh <制品目录>`，使用真实的三个原生服务、
验证码服务、独占 PostgreSQL、Redis、回环端口和 Supervisor。仅运行于有工具链及上述依赖的
普通 Linux 用户。测试检查摘要拒绝、首次激活、Resource 就绪以及新 Gate 启动失败后的旧版本恢复。
退出时停止夹具进程并清理临时目录。上线入口和主机配置见[部署说明](../../scripts/deploy/README.md)。

`python3 tests/deploy/test_abi.py` 检查 Jammy 自带的 GLIBC 2.35 和 GLIBCXX 3.4.30 可用，
并拒绝曾在目标机加载失败的 GLIBC 2.38 和 GLIBCXX 3.4.32。
