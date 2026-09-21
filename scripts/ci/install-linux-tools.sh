#!/usr/bin/env bash
set -euo pipefail

[[ "${GITHUB_ACTIONS:-}" == true && "${RUNNER_OS:-}" == Linux ]] || {
    echo 'Locked Linux tools can only be installed in a GitHub Linux job.' >&2
    exit 1
}
script_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
tool_root=$(mktemp -d "${RUNNER_TEMP:?}/chat-ci-tools.XXXXXXXX")
# These are job-owned tools, never the persistent vcpkg install tree.
timeout 240s pwsh -NoLogo -NoProfile -NonInteractive -File "$script_root/download-linux-tools.ps1" -DownloadRoot "$tool_root"
mkdir "$tool_root/cmake" "$tool_root/ninja"
tar -xzf "$tool_root/cmake.tar.gz" --strip-components=1 -C "$tool_root/cmake"
unzip -q "$tool_root/ninja.zip" -d "$tool_root/ninja"
chmod +x "$tool_root/ninja/ninja"
[[ "$("$tool_root/cmake/bin/cmake" --version | head -n 1)" == 'cmake version 3.28.3' ]]
[[ "$("$tool_root/ninja/ninja" --version)" == '1.12.1' ]]
printf '%s\n' "$tool_root/cmake/bin" "$tool_root/ninja" >> "${GITHUB_PATH:?}"
echo 'Verified locked CMake 3.28.3 and Ninja 1.12.1.'
