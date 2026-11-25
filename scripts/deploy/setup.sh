#!/usr/bin/env bash
# 以部署用户初始化私有进程组；配置、数据及数据库凭据由主机维护。
set -euo pipefail
home=$(realpath -m "${1:?deployment directory}")
[[ "$home" =~ ^/[A-Za-z0-9_./-]+$ && "$(id -u)" != 0 ]]
src=$(cd "$(dirname "$0")" && pwd)
umask 077
mkdir -p "$home"/{releases,shared,logs}
install -m 700 "$src/receive.py" "$home/receive.py"
cat > "$home/supervisord.conf" <<EOF
[unix_http_server]
file=$home/supervisor.sock
chmod=0600
[supervisord]
logfile=$home/logs/supervisord.log
pidfile=$home/supervisord.pid
childlogdir=$home/logs
[rpcinterface:supervisor]
supervisor.rpcinterface_factory=supervisor.rpcinterface:make_main_rpcinterface
[supervisorctl]
serverurl=unix://$home/supervisor.sock
[group:chat]
programs=VarifyServer,ChatServer,ResourceServer,GateServer
EOF
for service in VarifyServer ChatServer ResourceServer GateServer; do
    cat >> "$home/supervisord.conf" <<EOF
[program:$service]
command=/bin/bash $home/current/launch.sh $service
directory=$home/shared
environment=CHAT_DEPLOY_HOME="$home"
autostart=false
autorestart=unexpected
startsecs=1
startretries=2
stopsignal=INT
stopasgroup=true
killasgroup=true
redirect_stderr=true
stdout_logfile=$home/logs/$service.log
stdout_logfile_maxbytes=10MB
stdout_logfile_backups=2
EOF
done
