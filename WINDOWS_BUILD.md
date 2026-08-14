# Windows local build

This is the phase-one Windows baseline. It restores and builds the existing
projects without changing their build systems:

- GateServer, StatusServer and ChatServer: Visual Studio/MSBuild
- chat: CMake with the existing Qt 6.5.3 MinGW 64-bit kit
- VarifyServer: npm ci

Linux, packaging, windeployqt and GitHub Actions are intentionally outside
this phase.

## Toolchain baseline

- Visual Studio 2022, v143 toolset, Windows 10 SDK
- vcpkg checkout 4b3e4c276b5b87a649e66341e11553e8c577459c
- dependency baseline from the root vcpkg.json:
  fc3be1ebea7eaeb3071fe716ac65713af1f3a146
- Qt 6.5.3 MinGW 64-bit and its bundled MinGW compiler
- CMake 3.16 or newer and Ninja
- Node.js and npm with support for npm ci

The server triplet is the repository-owned `x64-windows-chat` overlay. The MSVC
runtime and third-party libraries are dynamic (/MDd for Debug and /MD for
Release), except `mysql-connector-cpp[jdbc]` and `libmysql`: the pinned vcpkg
registry supports that JDBC feature only as static libraries. vcpkg app-local
deployment copies required dependency DLLs beside each locally built server.
Each server has an independent output directory under
`build\windows-servers\<Configuration>\<ServerUnit>`; the matching
`config.ini` is copied there as well. ChatServer is built once; multiple server
instances are separate processes of that executable using different explicit
configuration files. Object and incremental-build files are isolated by server
unit. The helper script places them under
`D:\vcpkg-chat-temp\msbuild` by default.
The tracked legacy MySQL DLLs remain in the repository for now, but the projects
no longer copy or link against them.

## Environment

Set paths for your installation. Do not add personal paths to project files.

    $env:VCPKG_ROOT = 'D:\vcpkg\test-vcpkg'
    $env:QT_ROOT = 'D:\Qt\Qt\6.5.3\mingw_64'
    $env:MINGW_ROOT = 'D:\Qt\Qt\Tools\mingw1120_64'

QT_ROOT may be omitted when the correct qmake.exe is on PATH. MINGW_ROOT may
be omitted when exactly one Qt MinGW 64-bit toolchain exists under the same Qt
installation.

## Commands

Run all commands from the repository root:

    # Verify tool discovery and pinned versions without restoring dependencies.
    .\scripts\windows-local.ps1 -Task Check

    # Restore the root manifest into vcpkg_installed\x64-windows-chat.
    .\scripts\windows-local.ps1 -Task RestoreServers

    # Build the three C++ service executables.
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Debug
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Release

    # Configure and build the Qt client.
    .\scripts\windows-local.ps1 -Task BuildClient -Configuration Debug
    .\scripts\windows-local.ps1 -Task BuildClient -Configuration Release

    # Run the client model tests after each configuration.
    ctest --test-dir .\build\windows-client\Debug --output-on-failure
    ctest --test-dir .\build\windows-client\Release --output-on-failure

    # Recreate VarifyServer dependencies from package-lock.json.
    .\scripts\windows-local.ps1 -Task RestoreVarify

    # Run all of the above in order.
    .\scripts\windows-local.ps1 -Task BuildAll -Configuration Debug

## ChatServer instances

The build copies the two repository examples to
`build\windows-servers\<Configuration>\ChatServer\configs`. Run one instance by
passing its configuration explicitly:

    .\build\windows-servers\Release\ChatServer\ChatServer.exe `
      --config .\build\windows-servers\Release\ChatServer\configs\chat-01.ini

The no-argument form remains available and reads `config.ini` from the current
working directory. Prefer `--config` in scripts and deployments so startup does
not depend on an accidental working directory.

Use the management script for multiple local processes. It records only the
processes it starts, verifies executable path and process start time before
stopping them, and gives every config a separate working directory:

    .\scripts\chatserver-instances.ps1 -Task Start `
      -Executable .\build\windows-servers\Release\ChatServer\ChatServer.exe `
      -ConfigDirectory .\build\windows-servers\Release\ChatServer\configs

    .\scripts\chatserver-instances.ps1 -Task Status
    .\scripts\chatserver-instances.ps1 -Task Stop

You can pass selected files with repeated/array `-Config` values instead of
`-ConfigDirectory`. To add more instances, copy an example and manually update
the peer and StatusServer configuration. Every simultaneously running instance
must use a unique `[SelfServer].Name` and `[Log].Name`; every TCP/RPC listener
port must also be unique across all managed instances. Relative `LogDir` values
are isolated by the management script's per-instance working directories.

Malformed or missing configuration, an empty instance/log name, an invalid
local port, log initialization failure, or failure to bind a local TCP/gRPC port
causes that process to exit with failure. A configured peer that is temporarily
offline keeps the existing connection/retry behavior and does not create a
circular startup dependency.

If PowerShell script execution is disabled, use a process-scoped policy:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows-local.ps1 -Task Check

RestoreServers and RestoreVarify may access the network when their local
caches do not contain the locked dependencies.

The complete checkout at `D:\vcpkg\test-vcpkg` is the vcpkg root on the
current development machine. On other machines or in CI, point `VCPKG_ROOT`
at a full checkout at the same pinned commit. The older `D:\vcpkg\vcpkg`
checkout is not a valid manifest root because it is shallow; only its reusable
download cache is retained. The old global `installed` tree has been removed.
For example:

    $env:VCPKG_DOWNLOADS = 'D:\vcpkg\vcpkg\downloads'
    $env:VCPKG_BINARY_SOURCES = 'clear;files,C:\Users\Lenovo\AppData\Local\vcpkg\archives,readwrite'

`RestoreServers` and MSBuild place temporary build/package trees under
`D:\vcpkg-chat-temp` by default and remove each port's trees after installation.
MSBuild only uses those dependency scratch directories when it needs to restore
a missing port; `RestoreServers` is still the preferred explicit restore step.
Override them when another drive has more working space without moving the
final `vcpkg_installed` tree:

    $env:VCPKG_BUILDTREES_ROOT = 'D:\vcpkg-chat-temp\buildtrees'
    $env:VCPKG_PACKAGES_ROOT = 'D:\vcpkg-chat-temp\packages'
    .\scripts\windows-local.ps1 -Task RestoreServers

Server intermediate files can be redirected independently. This is useful when
the repository drive has limited free space:

    $env:CHAT_SERVER_INTERMEDIATE_ROOT = 'D:\vcpkg-chat-temp\msbuild'
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Debug

vcpkg versioning also requires the checkout to contain its full Git history.
If RestoreServers reports a shallow checkout, complete it and retry:

    git -C C:\path\to\vcpkg fetch --unshallow --tags
