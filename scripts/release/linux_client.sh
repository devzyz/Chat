#!/usr/bin/env bash
# 打包正式桌面程序及 Qt 插件，保留 Linux 桌面提供的 X11、图形驱动和系统库。
set -euo pipefail
bin=$(realpath "$1")
qt=$(realpath "$2")
out=$(realpath -m "$3")
mkdir -p "$out"
stage=$(mktemp -d)
trap 'rm -rf -- "$stage"' EXIT
mkdir -p "$stage/bin" "$stage/lib" "$stage/plugins"
cp "$bin/chat" "$stage/bin/"
cp -R "$bin/static" "$stage/bin/"
printf '[GateServer]\nhost=127.0.0.1\nport=8080\n[ResourceServer]\nUrl=http://127.0.0.1:8090\n' > "$stage/bin/config.ini"
for plugin in platforms/libqxcb.so platforms/libqoffscreen.so sqldrivers/libqsqlite.so tls/libqopensslbackend.so imageformats/libqjpeg.so imageformats/libqgif.so imageformats/libqico.so; do
    install -D -m 755 "$qt/plugins/$plugin" "$stage/plugins/$plugin"
done
for file in "$stage/bin/chat" "$stage"/plugins/*/*.so; do
    deps=$(LD_LIBRARY_PATH="$qt/lib" ldd "$file")
    if grep -q 'not found' <<< "$deps"; then echo "$deps" >&2; exit 1; fi
    while read -r name path; do
        [[ "$path" == "$qt/lib/"* ]] || continue
        cp -Lf "$path" "$stage/lib/$name"
    done < <(awk '$2 == "=>" {print $1, $3}' <<< "$deps")
done
printf '[Paths]\nPrefix=..\nPlugins=plugins\nLibraries=lib\n' > "$stage/bin/qt.conf"
cat > "$stage/chat" <<'LAUNCH'
#!/bin/sh
set -eu
root=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd -P)
export LD_LIBRARY_PATH="$root/lib"
export QT_PLUGIN_PATH="$root/plugins"
exec "$root/bin/chat" "$@"
LAUNCH
chmod 755 "$stage/chat"
git rev-parse HEAD > "$stage/SOURCE_SHA"
printf 'Run ./chat from a graphical Linux desktop. Edit bin/config.ini to select the server.\n' > "$stage/README.txt"
tar -C "$stage" -czf "$out/Chat-client-linux-x64.tar.gz" .
