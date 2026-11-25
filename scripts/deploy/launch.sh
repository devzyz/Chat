#!/usr/bin/env bash
# 从同一不可变发行目录启动服务，配置和数据由部署账号独立持有。
set -euo pipefail
release=$(cd "$(dirname "$0")" && pwd -P)
cd "${CHAT_DEPLOY_HOME:?}/shared"
set -a
source ./runtime.env
set +a
case "${1:?service}" in
    GateServer|ChatServer|ResourceServer)
        export LD_LIBRARY_PATH="$release/$1"
        exec "$release/$1/$1" --config "$PWD/$1.ini" ;;
    VarifyServer)
        export CHAT_CONFIG="$PWD/VarifyServer.json"
        exec "$release/node" "$release/VarifyServer/server.js" ;;
    *) exit 2 ;;
esac
