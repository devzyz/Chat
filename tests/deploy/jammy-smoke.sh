#!/usr/bin/env bash
# 在独占数据库和端口上运行真实归档，验证成功切换、拒绝坏摘要和失败回滚。
set -euo pipefail
bundle=$(realpath "${1:?archive directory}")
repo=$(cd "$(dirname "$0")/../.." && pwd)
work=$(mktemp -d)
export CHAT_DEPLOY_HOME="$work/app"
export PATH="$(pg_config --bindir):$PATH"
cleanup() {
    if [[ -S "$CHAT_DEPLOY_HOME/supervisor.sock" ]]; then
        supervisorctl -c "$CHAT_DEPLOY_HOME/supervisord.conf" shutdown >/dev/null || return 1
    fi
    [[ ! -f "$work/pg/postmaster.pid" ]] || pg_ctl -D "$work/pg" -m fast -w stop >/dev/null || return 1
    if [[ -n "${redis_pid:-}" ]]; then kill "$redis_pid" 2>/dev/null || true; wait "$redis_pid" || true; fi
    rm -rf -- "$work"
}
trap 'result=$?; trap - EXIT; cleanup || exit 1; exit "$result"' EXIT
(cd "$bundle" && sha256sum -c SHA256SUMS)
mkdir "$work/source"
tar -xzf "$bundle/chat-jammy-x64.tar.gz" -C "$work/source"
bash "$repo/scripts/deploy/setup.sh" "$CHAT_DEPLOY_HOME"
read -r pg redis gate chat rpc resource varify < <(python3 - <<'PY'
import socket
listeners = [socket.socket() for _ in range(7)]
for listener in listeners:
    listener.bind(('127.0.0.1', 0))
print(*(listener.getsockname()[1] for listener in listeners))
PY
)
umask 077
password=$(openssl rand -hex 20)
printf '%s\n' "$password" > "$work/dbpass"
initdb -D "$work/pg" -U chat_fixture --no-locale -E UTF8 -A scram-sha-256 --pwfile="$work/dbpass" >/dev/null
pg_ctl -D "$work/pg" -l "$work/db.log" -o "-h 127.0.0.1 -p $pg -k $work" -w start >/dev/null
redis-server --bind 127.0.0.1 --port "$redis" --save '' --appendonly no --requirepass "$password" >"$work/redis.log" 2>&1 &
redis_pid=$!
export CHAT_DB_DRIVER=postgresql CHAT_PG_HOST=127.0.0.1 CHAT_PG_PORT="$pg" CHAT_PG_USER=chat_fixture CHAT_PG_DATABASE=postgres
export CHAT_PG_CLIENT="$(command -v psql)" CHAT_PG_PASSWORD="$password"
"$work/source/node" "$work/source/schema/migrate.js" apply >/dev/null
shared="$CHAT_DEPLOY_HOME/shared"
cat > "$shared/runtime.env" <<EOF
CHAT_DB_PASSWORD=$password
CHAT_VARIFY_REDIS_PASSWORD=$password
CHAT_VARIFY_EMAIL_USER=sender@example.invalid
CHAT_VARIFY_BIND_ADDRESS=127.0.0.1:$varify
EOF
cat > "$shared/VarifyServer.json" <<EOF
{"email":{"host":"127.0.0.1","port":1,"secure":false,"auth":"none"},"redis":{"host":"127.0.0.1","port":$redis}}
EOF
for service in GateServer ChatServer ResourceServer; do
    cat > "$shared/$service.ini" <<EOF
[Database]
Driver=postgresql
Host=127.0.0.1
Port=$pg
User=chat_fixture
Schema=postgres
[Redis]
Host=127.0.0.1
Port=$redis
Password=$password
[GateServer]
Host=127.0.0.1
Port=$gate
[VarifyServer]
Host=127.0.0.1
Port=$varify
[ChatServers]
Name=UatChat
[UatChat]
Name=UatChat
Host=127.0.0.1
Port=$chat
[SelfServer]
Name=UatChat
Host=127.0.0.1
BindHost=127.0.0.1
Port=$chat
RPCPort=$rpc
[PeerServer]
Servers=
[ResourceServer]
Host=127.0.0.1
Port=$resource
StorageRoot=$shared/resources
[Log]
Name=$service
LogDir=$CHAT_DEPLOY_HOME/logs
MaxSizeMB=1
MaxTotalFiles=2
Level=info
FlushLevel=info
EOF
done
sha=$(cat "$work/source/SOURCE_SHA")
archive="$bundle/chat-jammy-x64.tar.gz"
digest=$(sha256sum "$archive" | cut -d' ' -f1)
receive() {
    python3 "$repo/tests/deploy/pull_fixture.py" "$3" "$sha" "$1" "$2" "${4:-$3}"
}
if receive "$(printf '0%.0s' {1..64})" "$archive" v0.0.1; then exit 1; fi
test ! -e "$CHAT_DEPLOY_HOME/current"
receive "$digest" "$archive" v0.0.1 latest
old=$(readlink "$CHAT_DEPLOY_HOME/current")
# 替换测试副本中的 Gate，以真实启动失败验证旧版本恢复。
printf '#!/bin/sh\nexit 1\n' > "$work/source/GateServer/GateServer"
tar -C "$work/source" -czf "$work/broken.tar.gz" .
if receive "$(sha256sum "$work/broken.tar.gz" | cut -d' ' -f1)" "$work/broken.tar.gz" v0.0.2; then exit 1; fi
test "$(readlink "$CHAT_DEPLOY_HOME/current")" = "$old"
curl -fsS "http://127.0.0.1:$resource/ready" >/dev/null
test "$(supervisorctl -c "$CHAT_DEPLOY_HOME/supervisord.conf" status 'chat:*' | grep -c RUNNING)" = 4
echo 'Jammy release, four services, dependency readiness, rejected checksum and failed-activation rollback passed.'
