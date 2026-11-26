#!/usr/bin/env bash
# 在干净的桌面会话中从压缩包启动正式客户端并验证窗口及私有 Qt 运行库。
set -euo pipefail
archive=$(realpath "$1")
stage=$(mktemp -d)
pid=''
cleanup() {
    if [[ -n "$pid" ]]; then kill "$pid" 2>/dev/null || true; wait "$pid" || true; fi
    rm -rf -- "$stage"
}
trap cleanup EXIT
tar -xzf "$archive" -C "$stage"
test "$(cat "$stage/SOURCE_SHA")" = "$2"
for file in "$stage/bin/chat" "$stage"/plugins/*/*.so; do
    deps=$(LD_LIBRARY_PATH="$stage/lib" ldd "$file")
    if grep -q 'not found' <<< "$deps"; then echo "$deps" >&2; exit 1; fi
    awk -v root="$stage/lib/" '$1 ~ /^libQt6/ && index($3,root)!=1 {exit 1}' <<< "$deps"
done
cd "$stage"
unset QT_ROOT_DIR QTDIR QT_PLUGIN_PATH LD_PRELOAD
export HOME="$stage/home" QT_QPA_PLATFORM=xcb
mkdir "$HOME"
./chat > "$stage/client.log" 2>&1 & pid=$!
for _ in $(seq 1 100); do
    if ! kill -0 "$pid" 2>/dev/null; then cat "$stage/client.log" >&2; exit 1; fi
    if xdotool search --onlyvisible --pid "$pid" >/dev/null 2>&1; then
        grep -Fq "$stage/lib/libQt6Widgets.so" "/proc/$pid/maps"
        echo 'Packaged Linux desktop window and bundled Qt runtime verified.'
        exit 0
    fi
    sleep .2
done
cat "$stage/client.log" >&2
exit 1
