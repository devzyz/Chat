#!/usr/bin/env bash
# 从同次构建收集四个服务，校验目标 ABI 后生成可独立分发的包。
set -euo pipefail
bin=$(realpath "${1:?native binary directory}")
out=$(realpath -m "${2:?new output directory}")
mkdir "$out"
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
export stage bin
node - <<'NODE'
const path = require('node:path');
const { pack } = require('./tests/services/messageRuntime');
for (const name of ['GateServer', 'ChatServer', 'ResourceServer']) {
    pack(path.join(process.env.bin, name), path.join(process.env.stage, name), '.ci/vcpkg_installed', name, null);
}
NODE
mkdir "$stage/VarifyServer" "$stage/proto"
cp VarifyServer/*.js VarifyServer/package*.json VarifyServer/tls.config.example.json "$stage/VarifyServer/"
cp -RL VarifyServer/node_modules "$stage/VarifyServer/"
cp proto/varify.proto "$stage/proto/"
cp -R schema "$stage/"
cp "$(command -v node)" "$stage/node"
cp scripts/deploy/launch.sh "$stage/"
git rev-parse HEAD > "$stage/SOURCE_SHA"
python3 scripts/deploy/abi.py "$stage"
(cd "$stage" && tar -czf "$out/chat-jammy-x64.tar.gz" .)
(cd "$out" && sha256sum chat-jammy-x64.tar.gz > SHA256SUMS)
