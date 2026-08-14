[CmdletBinding()]
param(
    [ValidateSet('Check', 'RestoreServers', 'BuildServers', 'BuildClient', 'RestoreVarify', 'BuildAll')]
    [string]$Task = 'Check',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VcpkgBuildtreesRoot = $env:VCPKG_BUILDTREES_ROOT,
    [string]$VcpkgPackagesRoot = $env:VCPKG_PACKAGES_ROOT,
    [string]$ServerIntermediateRoot = $env:CHAT_SERVER_INTERMEDIATE_ROOT,
    [string]$ServerTriplet = 'x64-windows-chat',
    [string]$ServerHostTriplet = 'x64-windows',
    [string]$QtRoot = $env:QT_ROOT,
    [string]$MinGwRoot = $env:MINGW_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
$solution = Join-Path $repoRoot 'Chat.sln'
$manifest = Join-Path $repoRoot 'vcpkg.json'
$clientSource = Join-Path $repoRoot 'chat'
$clientBuild = Join-Path $repoRoot "build\windows-client\$Configuration"
$varifySource = Join-Path $repoRoot 'VarifyServer'
$overlayTriplets = Join-Path $repoRoot 'triplets'
$expectedQtVersion = '6.5.3'
$expectedVcpkgCommit = '4b3e4c276b5b87a649e66341e11553e8c577459c'

if ([string]::IsNullOrWhiteSpace($VcpkgBuildtreesRoot)) {
    $VcpkgBuildtreesRoot = 'D:\vcpkg-chat-temp\buildtrees'
}
if ([string]::IsNullOrWhiteSpace($VcpkgPackagesRoot)) {
    $VcpkgPackagesRoot = 'D:\vcpkg-chat-temp\packages'
}
if ([string]::IsNullOrWhiteSpace($ServerIntermediateRoot)) {
    $ServerIntermediateRoot = 'D:\vcpkg-chat-temp\msbuild'
}

function Require-File {
    param([string]$Path, [string]$Hint)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Hint Missing file: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Require-Command {
    param([string]$Name, [string]$Hint)

    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if (-not $command) {
        throw "$Hint Command not found: $Name"
    }
    return $command.Source
}

function Resolve-Vcpkg {
    if ([string]::IsNullOrWhiteSpace($VcpkgRoot)) {
        throw 'Set VCPKG_ROOT, or pass -VcpkgRoot, to a pinned vcpkg checkout.'
    }

    $resolvedRoot = (Resolve-Path -LiteralPath $VcpkgRoot).Path
    $exe = Require-File (Join-Path $resolvedRoot 'vcpkg.exe') 'Bootstrap the pinned vcpkg checkout first.'
    [void](Require-File (Join-Path $resolvedRoot 'scripts\buildsystems\msbuild\vcpkg.props') 'The vcpkg MSBuild integration is incomplete.')
    [void](Require-File (Join-Path $resolvedRoot 'scripts\buildsystems\msbuild\vcpkg.targets') 'The vcpkg MSBuild integration is incomplete.')

    $versionOutput = (& $exe version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch [regex]::Escape($expectedVcpkgCommit)) {
        throw "vcpkg checkout mismatch. Expected commit $expectedVcpkgCommit. Reported version: $versionOutput"
    }

    return @{
        Root = $resolvedRoot
        Exe = $exe
    }
}

function Resolve-MSBuild {
    $programFilesX86 = [Environment]::GetFolderPath('ProgramFilesX86')
    $vswhere = Join-Path $programFilesX86 'Microsoft Visual Studio\Installer\vswhere.exe'
    [void](Require-File $vswhere 'Install Visual Studio 2022 with the Desktop development with C++ workload.')
    $installPath = (& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -property installationPath).Trim()
    if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($installPath)) {
        throw 'Visual Studio 2022 with MSBuild was not found.'
    }
    return Require-File (Join-Path $installPath 'MSBuild\Current\Bin\MSBuild.exe') 'MSBuild was not found in the Visual Studio installation.'
}

function Resolve-QtToolchain {
    if ([string]::IsNullOrWhiteSpace($QtRoot)) {
        $qmakeOnPath = Get-Command qmake.exe -ErrorAction SilentlyContinue
        if ($qmakeOnPath) {
            $script:QtRoot = (& $qmakeOnPath.Source -query QT_INSTALL_PREFIX).Trim()
        }
    }
    if ([string]::IsNullOrWhiteSpace($QtRoot)) {
        throw 'Set QT_ROOT, or pass -QtRoot, to the Qt 6.5.3 MinGW 64-bit directory.'
    }

    $resolvedQt = (Resolve-Path -LiteralPath $QtRoot).Path
    $qmake = Require-File (Join-Path $resolvedQt 'bin\qmake.exe') 'QT_ROOT must point to the Qt MinGW kit directory.'
    $qtVersion = (& $qmake -query QT_VERSION).Trim()
    $qtSpec = (& $qmake -query QMAKE_XSPEC).Trim()
    if ($qtVersion -ne $expectedQtVersion -or $qtSpec -ne 'win32-g++') {
        throw "Expected Qt $expectedQtVersion MinGW (win32-g++), found Qt $qtVersion ($qtSpec)."
    }

    if ([string]::IsNullOrWhiteSpace($MinGwRoot)) {
        $qtInstallRoot = Split-Path -Parent (Split-Path -Parent $resolvedQt)
        $candidates = @(Get-ChildItem -LiteralPath (Join-Path $qtInstallRoot 'Tools') -Directory -Filter 'mingw*_64' -ErrorAction SilentlyContinue |
            Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'bin\g++.exe') })
        if ($candidates.Count -eq 1) {
            $script:MinGwRoot = $candidates[0].FullName
        }
    }
    if ([string]::IsNullOrWhiteSpace($MinGwRoot)) {
        throw 'Set MINGW_ROOT, or pass -MinGwRoot, to the compiler bundled with the selected Qt kit.'
    }

    $resolvedMinGw = (Resolve-Path -LiteralPath $MinGwRoot).Path
    $cxx = Require-File (Join-Path $resolvedMinGw 'bin\g++.exe') 'The Qt MinGW compiler was not found.'
    $cc = Require-File (Join-Path $resolvedMinGw 'bin\gcc.exe') 'The Qt MinGW compiler was not found.'

    $qtInstallRoot = Split-Path -Parent (Split-Path -Parent $resolvedQt)
    $bundledNinja = Join-Path $qtInstallRoot 'Tools\Ninja\ninja.exe'
    if (Test-Path -LiteralPath $bundledNinja) {
        $ninja = (Resolve-Path -LiteralPath $bundledNinja).Path
    } else {
        $ninja = Require-Command 'ninja.exe' 'Install Ninja or the Ninja component from the Qt installer.'
    }

    return @{
        Root = $resolvedQt
        QMake = $qmake
        CCompiler = $cc
        CxxCompiler = $cxx
        Ninja = $ninja
    }
}

function Restore-Servers {
    $vcpkg = Resolve-Vcpkg
    $shallowMarker = Join-Path $vcpkg.Root '.git\shallow'
    if (Test-Path -LiteralPath $shallowMarker) {
        throw "The vcpkg checkout is shallow, so it cannot resolve the pinned manifest baseline. Run: git -C $($vcpkg.Root) fetch --unshallow --tags"
    }
    $arguments = @(
        'install'
        '--triplet', $ServerTriplet
        '--host-triplet', $ServerHostTriplet
        "--overlay-triplets=$overlayTriplets"
        "--x-manifest-root=$repoRoot"
        "--x-install-root=$(Join-Path $repoRoot 'vcpkg_installed')"
        '--clean-buildtrees-after-build'
        '--clean-packages-after-build'
    )
    if (-not [string]::IsNullOrWhiteSpace($VcpkgBuildtreesRoot)) {
        $arguments += "--x-buildtrees-root=$VcpkgBuildtreesRoot"
    }
    if (-not [string]::IsNullOrWhiteSpace($VcpkgPackagesRoot)) {
        $arguments += "--x-packages-root=$VcpkgPackagesRoot"
    }
    & $vcpkg.Exe @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "vcpkg restore failed with exit code $LASTEXITCODE."
    }
}

function Build-Servers {
    $vcpkg = Resolve-Vcpkg
    $msbuild = Resolve-MSBuild
    $arguments = @(
        $solution
        '/m'
        '/t:GateServer;StatusServer;ChatServer'
        "/p:Configuration=$Configuration"
        '/p:Platform=x64'
        "/p:VcpkgRoot=$($vcpkg.Root)"
        "/p:VcpkgTriplet=$ServerTriplet"
        "/p:VcpkgHostTriplet=$ServerHostTriplet"
        "/p:ServerIntermediateRoot=$ServerIntermediateRoot"
    )
    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Server build failed with exit code $LASTEXITCODE."
    }
}

function Build-Client {
    $cmake = Require-Command 'cmake.exe' 'Install CMake 3.16 or newer and add it to PATH.'
    $qt = Resolve-QtToolchain
    $cache = Join-Path $clientBuild 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $cacheText = Get-Content -LiteralPath $cache -Raw
        $compilerMatch = [regex]::Match($cacheText, '(?m)^CMAKE_CXX_COMPILER:[^=]*=(.+)$')
        $ninjaMatch = [regex]::Match($cacheText, '(?m)^CMAKE_MAKE_PROGRAM:[^=]*=(.+)$')
        $cachedCompiler = $compilerMatch.Groups[1].Value.Trim().Replace('/', '\')
        $cachedNinja = $ninjaMatch.Groups[1].Value.Trim().Replace('/', '\')
        if (-not $compilerMatch.Success -or -not $ninjaMatch.Success -or
            $cachedCompiler -ne $qt.CxxCompiler -or $cachedNinja -ne $qt.Ninja) {
            throw "Client build cache uses a different compiler or Ninja. Remove the generated directory and retry: $clientBuild"
        }
    }
    $configureArguments = @(
        '-S', $clientSource
        '-B', $clientBuild
        '-G', 'Ninja'
        "-DCMAKE_BUILD_TYPE=$Configuration"
        "-DCMAKE_PREFIX_PATH=$($qt.Root)"
        "-DCMAKE_MAKE_PROGRAM=$($qt.Ninja)"
        "-DCMAKE_C_COMPILER=$($qt.CCompiler)"
        "-DCMAKE_CXX_COMPILER=$($qt.CxxCompiler)"
    )
    & $cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Client configure failed with exit code $LASTEXITCODE."
    }
    & $cmake --build $clientBuild
    if ($LASTEXITCODE -ne 0) {
        throw "Client build failed with exit code $LASTEXITCODE."
    }
}

function Restore-Varify {
    [void](Require-Command 'node.exe' 'Install Node.js before restoring VarifyServer.')
    $npm = Require-Command 'npm.cmd' 'Install npm before restoring VarifyServer.'
    Push-Location $varifySource
    try {
        & $npm ci
        if ($LASTEXITCODE -ne 0) {
            throw "npm ci failed with exit code $LASTEXITCODE."
        }
    } finally {
        Pop-Location
    }
}

function Check-Toolchains {
    $vcpkg = Resolve-Vcpkg
    $msbuild = Resolve-MSBuild
    $cmake = Require-Command 'cmake.exe' 'Install CMake 3.16 or newer and add it to PATH.'
    $qt = Resolve-QtToolchain
    $node = Require-Command 'node.exe' 'Install Node.js before restoring VarifyServer.'
    $npm = Require-Command 'npm.cmd' 'Install npm before restoring VarifyServer.'

    Write-Host "Repository: $repoRoot"
    Write-Host "MSBuild:   $msbuild"
    Write-Host "vcpkg:    $($vcpkg.Root) ($expectedVcpkgCommit)"
    Write-Host "CMake:    $cmake"
    Write-Host "Qt:       $($qt.Root) ($expectedQtVersion MinGW 64-bit)"
    Write-Host "MinGW:    $($qt.CxxCompiler)"
    Write-Host "Ninja:    $($qt.Ninja)"
    Write-Host "Node:     $node"
    Write-Host "npm:      $npm"
    Write-Host "Manifest: $manifest"
    Write-Host "Triplet:  $serverTriplet ($overlayTriplets)"
}

switch ($Task) {
    'Check' { Check-Toolchains }
    'RestoreServers' { Restore-Servers }
    'BuildServers' { Build-Servers }
    'BuildClient' { Build-Client }
    'RestoreVarify' { Restore-Varify }
    'BuildAll' {
        Check-Toolchains
        Restore-Servers
        Build-Servers
        Build-Client
        Restore-Varify
    }
}
