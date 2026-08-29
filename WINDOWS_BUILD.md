# Windows local build and CI

This document describes the shared Windows baseline used by local development
and the phase-two GitHub Actions workflow. It builds the existing projects
without changing their build systems:

- GateServer, StatusServer and ChatServer: Visual Studio/MSBuild
- chat: CMake with the existing Qt 6.5.3 MinGW 64-bit kit
- VarifyServer: npm ci

Linux remains outside the current phase. Windows CI validates the configuration
from a clean runner, builds and tests the deliverables, and creates independent
ZIP packages suitable for release testing.

## Toolchain baseline

- Visual Studio 2022, v143 toolset, Windows 10 SDK
- vcpkg dependency registry checkout/baseline from the root `vcpkg.json`:
  `fc3be1ebea7eaeb3071fe716ac65713af1f3a146`
- vcpkg tool version:
  `4b3e4c276b5b87a649e66341e11553e8c577459c`
- Qt 6.5.3 MinGW 64-bit and its bundled MinGW compiler
- CMake 3.24 or newer and Ninja (required for JUnit-capable CTest and guarded
  `--fresh` recovery of an incomplete generated cache)
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

CI uses `x64-windows-chat-release` for both target and host tools. It keeps the
same dynamic CRT/library policy while setting `VCPKG_BUILD_TYPE=release`, so a
clean runner does not store unused Debug libraries. Using the same release-only
triplet for host tools also avoids a second full `x64-windows` install tree.
Local development keeps `x64-windows-chat` plus the normal `x64-windows` host
triplet so Debug builds remain available.

## GitHub Actions phase-two baseline

`.github/workflows/windows-ci.yml` runs four Windows Server 2022 jobs. The
three build/package jobs depend on the static configuration check, but are
otherwise independent:

1. `static-check` validates the solution and MSBuild XML, the pinned manifest
   baseline, the repository triplets, and the absence of legacy ChatServer or
   static-triplet references in active build inputs.
2. `servers-release` checks out the pinned vcpkg source baseline, restores both
   target and host dependencies with `x64-windows-chat-release`, and builds
   GateServer, StatusServer and ChatServer in Release. It verifies that each
   app-local directory contains its executable, configuration and required
   DLLs, then uploads three independent ZIP files in the
   `windows-servers-release` artifact.
3. `client-release` installs Qt 6.5.3 with its MinGW toolchain, invokes the same
   `BuildClient -Configuration Release` entry point used locally, runs CTest,
   and stages `chat.exe`, `config.ini` and `static`. `windeployqt` auto-detects
   the Release executable and adds the Qt and compiler runtime DLLs; CI verifies
   the core Qt and Windows platform plugin before uploading `chat-client.zip`.
4. `varify-release` uses Node.js 22 and `npm ci --ignore-scripts` from the
   committed lockfile. It syntax-checks the project JavaScript, validates the
   production dependency tree, and packages the tracked JavaScript, JSON and
   proto files together with `node_modules` as `VarifyServer.zip`.

GitHub Actions run `31803503805` proved that the server job can restore and
build successfully on a clean runner with the vcpkg binary cache disabled. Its
cache-free `RestoreServers` step completed in 68 minutes 37 seconds. The
workflow now caches only vcpkg binary archives. It does not cache
`vcpkg_installed`, buildtrees, packages, MSBuild intermediates or final release
directories: those are derived state and are recreated and verified by every
run. After dependency restoration, the archive directory is saved on a new-key
miss or reported by a separate step on an exact-key hit, then removed before
MSBuild to reduce runner disk use. Acceptance of the cache change requires one
new-key miss-and-save run followed by one same-key exact-hit run; those two
outcomes have not yet been verified. The Node setup step may cache npm's
download cache keyed by `package-lock.json`; it does not cache `node_modules`,
which is always recreated by `npm ci`.

## Environment

Set paths for your installation. Do not add personal paths to project files.

    $env:VCPKG_ROOT = 'D:\vcpkg\test-vcpkg'
    $env:QT_ROOT = 'D:\Qt\Qt\6.5.3\mingw_64'
    $env:MINGW_ROOT = 'D:\Qt\Qt\Tools\mingw1120_64'

QT_ROOT may be omitted when the correct qmake.exe is on PATH. MINGW_ROOT may
be omitted when exactly one Qt MinGW 64-bit toolchain exists under the same Qt
installation.

VarifyServer keeps only non-secret hosts and ports in `VarifyServer\config.json`.
The following credentials are required at runtime and must be supplied through
the process environment, a Windows user environment, or CI/deployment secrets:

    CHAT_VARIFY_EMAIL_USER
    CHAT_VARIFY_EMAIL_PASS
    CHAT_VARIFY_MYSQL_PASSWORD
    CHAT_VARIFY_REDIS_PASSWORD

Do not add these values to JSON, source files, command history, or committed
`.env` files. Values that were previously committed must be rotated at their
providers; moving them to environment variables does not erase them from Git
history. `config.js` fails immediately when a required variable is missing and
rejects the former plaintext JSON fields. After setting user-level values, open
a new terminal before starting VarifyServer. To refresh an already-open
PowerShell process without displaying values:

    $names = @(
      'CHAT_VARIFY_EMAIL_USER',
      'CHAT_VARIFY_EMAIL_PASS',
      'CHAT_VARIFY_MYSQL_PASSWORD',
      'CHAT_VARIFY_REDIS_PASSWORD'
    )
    foreach ($name in $names) {
      Set-Item -Path "Env:$name" -Value ([Environment]::GetEnvironmentVariable($name, 'User'))
    }

## Commands

Run all commands from the repository root:

    # Verify tool discovery and pinned versions without restoring dependencies.
    .\scripts\windows-local.ps1 -Task Check

    # Restore the root manifest into vcpkg_installed\x64-windows-chat.
    .\scripts\windows-local.ps1 -Task RestoreServers

    # Build the three C++ service executables.
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Debug
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Release

    # CI-equivalent Release-only dependency layout (normally used on a clean tree).
    .\scripts\windows-local.ps1 -Task RestoreServers `
      -ServerTriplet x64-windows-chat-release `
      -ServerHostTriplet x64-windows-chat-release
    .\scripts\windows-local.ps1 -Task BuildServers -Configuration Release `
      -ServerTriplet x64-windows-chat-release `
      -ServerHostTriplet x64-windows-chat-release

    # Configure and build the Qt client.
    .\scripts\windows-local.ps1 -Task BuildClient -Configuration Debug
    .\scripts\windows-local.ps1 -Task BuildClient -Configuration Release

    # Run the client model tests after each configuration.
    ctest --test-dir .\build\windows-client\Debug --output-on-failure
    ctest --test-dir .\build\windows-client\Release --output-on-failure

    # Recreate VarifyServer dependencies from package-lock.json.
    .\scripts\windows-local.ps1 -Task RestoreVarify

    # Run each phase-one behavior test group.
    .\scripts\windows-local.ps1 -Task CheckTestStructure
    .\scripts\windows-local.ps1 -Task CheckTestReports
    .\scripts\windows-local.ps1 -Task RunScriptTests
    .\scripts\windows-local.ps1 -Task RunServerTests -Configuration Release
    .\scripts\windows-local.ps1 -Task RunClientTests -Configuration Release
    .\scripts\windows-local.ps1 -Task RunVarifyTests

    # Run all four test groups with the configured toolchains and restored dependencies.
    .\scripts\windows-local.ps1 -Task RunAllTests -Configuration Release

    # Run all of the above in order.
    .\scripts\windows-local.ps1 -Task BuildAll -Configuration Debug

Test reports are grouped by execution Level rather than by source directory:

- Server: `server_unit.xml`, `server_component.xml`,
  `server_integration.xml`, `server_chat_grpc_integration.xml`,
  `server_gate_unit.xml`, `server_status_unit.xml`.
- Qt client: `client_unit.xml`, `client_component.xml`.
- VarifyServer: `varify_unit.xml`, `varify_integration.xml`.
- PowerShell: `script_component.xml`, `script_integration.xml`.

All reports are written under `build\test-results`. Test directories continue
to mirror production modules; see `tests\README.md` for the independent Domain
and Level classification rules. `tests\REGRESSION.md` defines the permanent
baseline and admission contract for future modules; phase numbers do not retire
older regression tests.

The current develop baseline is exactly 173 testcases in 12 reports.
`CheckTestReports` performs the no-build integrity audit; `RunAllTests` invokes
the same audit after all four toolchain runners. A missing report, count drift,
failure/error node, test exit failure, or newly-created known test temporary
directory returns a nonzero status.

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

## Local runnable environment retention

Cleanup on a development machine must preserve the local runnable baseline.
The following paths are derived, but they are not disposable cleanup residue:

- `vcpkg_installed`, because MSBuild needs the manifest dependency tree.
- `build\windows-servers\Debug` and `build\windows-servers\Release`, including
  each service EXE, `config.ini`, ChatServer example configs and app-local DLLs.
- `build\windows-client\Debug` and `build\windows-client\Release`, including
  the `windeployqt` runtime and plugin directories beside `chat.exe`.
- `VarifyServer\node_modules`, so the Node service remains directly runnable.
- `D:\vcpkg-chat-temp\msbuild` when incremental Server development is expected.
- The selected Qt installation, the pinned vcpkg checkout, vcpkg downloads and
  vcpkg binary archives.

Routine cleanup may remove only confirmed, reproducible scratch data, such as
failed-operation quarantine directories, `build\local-smoke`, stale temporary
logs, and completed vcpkg `buildtrees` or `packages` staging directories. It
must first verify that no matching build or application process is active.
Never use a broad `git clean -fdx` or delete the whole `build` directory as a
normal cleanup operation. Before deleting any protected path above, list the
exact target, explain that local build or direct-run capability will be lost,
and obtain explicit approval for that cold reset.

If PowerShell script execution is disabled, use a process-scoped policy:

    powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\windows-local.ps1 -Task Check

RestoreServers and RestoreVarify may access the network when their local
caches do not contain the locked dependencies.

The complete vcpkg source tree at `D:\vcpkg\test-vcpkg` is the vcpkg root on
the current development machine. On other machines or in CI, point
`VCPKG_ROOT` at a full source tree that can resolve the registry baseline and
whose bootstrapped executable reports the expected vcpkg tool version. The
older `D:\vcpkg\vcpkg` tree is not a valid manifest root because its Git
history is shallow; only its reusable download cache is retained. The old
global `installed` tree has been removed. For example:

    $env:VCPKG_DOWNLOADS = 'D:\vcpkg\vcpkg\downloads'
    $env:VCPKG_BINARY_SOURCES = "clear;files,$env:LOCALAPPDATA\vcpkg\archives,readwrite"

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
