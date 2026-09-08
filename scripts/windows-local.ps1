[CmdletBinding()]
param(
    [ValidateSet('Check', 'CheckTestStructure', 'CheckTestReports', 'GenerateProtocols', 'CheckProtocols', 'RestoreServers', 'BuildServers', 'BuildClient', 'RestoreVarify', 'RunServerTests', 'RunClientTests', 'RunScriptTests', 'RunVarifyTests', 'RunAllTests', 'TestPhase1', 'BuildAll')]
    [string]$Task = 'Check',

    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',

    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$VcpkgInstalledRoot,
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
$global:LASTEXITCODE = 0

$repoRoot = Split-Path -Parent $PSScriptRoot
$fixedVcpkgInstalledRoot = 'D:\git\Chat\vcpkg_installed'
if ([string]::IsNullOrWhiteSpace($VcpkgInstalledRoot)) {
    $VcpkgInstalledRoot = $fixedVcpkgInstalledRoot
}
$solution = Join-Path $repoRoot 'Chat.sln'
$manifest = Join-Path $repoRoot 'vcpkg.json'
$clientSource = Join-Path $repoRoot 'chat'
$clientBuild = Join-Path $repoRoot "build\windows-client\$Configuration"
$varifySource = Join-Path $repoRoot 'VarifyServer'
$gateServerExecutable = Join-Path $repoRoot "build\windows-servers\$Configuration\GateServer\GateServer.exe"
$statusServerExecutable = Join-Path $repoRoot "build\windows-servers\$Configuration\StatusServer\StatusServer.exe"
$chatServerExecutable = Join-Path $repoRoot "build\windows-servers\$Configuration\ChatServer\ChatServer.exe"
$serverTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\server_unit_tests.exe"
$serverComponentTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\server_component_tests.exe"
$serverIntegrationTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\server_integration_tests.exe"
$chatGrpcClientTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\chat_grpc_client_tests.exe"
$gateAsioTestProject = Join-Path $repoRoot 'tests\server\lifecycle\GateAsioPoolTests.vcxproj'
$statusAsioTestProject = Join-Path $repoRoot 'tests\server\lifecycle\StatusAsioPoolTests.vcxproj'
$gateAsioTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\gate_asio_pool_tests.exe"
$statusAsioTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\status_asio_pool_tests.exe"
$testResults = Join-Path $repoRoot 'build\test-results'
$clientTestGroups = @(
    [pscustomobject]@{ Level = 'unit'; Report = (Join-Path $testResults 'client_unit.xml'); ExpectedCount = 18 }
    [pscustomobject]@{ Level = 'component'; Report = (Join-Path $testResults 'client_component.xml'); ExpectedCount = 6 }
    [pscustomobject]@{ Level = 'integration'; Report = (Join-Path $testResults 'client_integration.xml'); ExpectedCount = 22 }
)
$scriptTestGroups = @(
    [pscustomobject]@{
        Level = 'component'
        Report = (Join-Path $testResults 'script_component.xml')
        Script = 'tests\scripts\validation\chatserver-instances.tests.ps1'
        TestIdPattern = '^A02-VAL-\d{2}$'
        ExpectedCount = 9
    }
    [pscustomobject]@{
        Level = 'integration'
        Report = (Join-Path $testResults 'script_integration.xml')
        Script = 'tests\scripts\lifecycle\chatserver-instances.tests.ps1'
        TestIdPattern = '^A02-LIFE-\d{2}$'
        ExpectedCount = 4
    }
)
$regressionReportGroups = @(
    [pscustomobject]@{ Lane = 'server'; Name = 'server_unit.xml'; ExpectedCount = 68 }
    [pscustomobject]@{ Lane = 'server'; Name = 'server_component.xml'; ExpectedCount = 56 }
    [pscustomobject]@{ Lane = 'server'; Name = 'server_integration.xml'; ExpectedCount = 93 }
    [pscustomobject]@{ Lane = 'server'; Name = 'server_chat_grpc_integration.xml'; ExpectedCount = 4 }
    [pscustomobject]@{ Lane = 'server'; Name = 'server_gate_unit.xml'; ExpectedCount = 2 }
    [pscustomobject]@{ Lane = 'server'; Name = 'server_status_unit.xml'; ExpectedCount = 2 }
    [pscustomobject]@{ Lane = 'client'; Name = 'client_unit.xml'; ExpectedCount = 18 }
    [pscustomobject]@{ Lane = 'client'; Name = 'client_component.xml'; ExpectedCount = 6 }
    [pscustomobject]@{ Lane = 'client'; Name = 'client_integration.xml'; ExpectedCount = 22 }
    [pscustomobject]@{ Lane = 'varify'; Name = 'varify_unit.xml'; ExpectedCount = 18 }
    [pscustomobject]@{ Lane = 'varify'; Name = 'varify_integration.xml'; ExpectedCount = 11 }
    [pscustomobject]@{ Lane = 'script'; Name = 'script_component.xml'; ExpectedCount = 9 }
    [pscustomobject]@{ Lane = 'script'; Name = 'script_integration.xml'; ExpectedCount = 4 }
)
$legacyRegressionReportGroups = @(
    [pscustomobject]@{ Name = 'server_unit.xml'; MinimumCount = 68 }
    [pscustomobject]@{ Name = 'server_component.xml'; MinimumCount = 56 }
    [pscustomobject]@{ Name = 'server_integration.xml'; MinimumCount = 34 }
    [pscustomobject]@{ Name = 'server_chat_grpc_integration.xml'; MinimumCount = 4 }
    [pscustomobject]@{ Name = 'server_gate_unit.xml'; MinimumCount = 2 }
    [pscustomobject]@{ Name = 'server_status_unit.xml'; MinimumCount = 2 }
    [pscustomobject]@{ Name = 'client_unit.xml'; MinimumCount = 18 }
    [pscustomobject]@{ Name = 'client_component.xml'; MinimumCount = 6 }
    [pscustomobject]@{ Name = 'varify_unit.xml'; MinimumCount = 18 }
    [pscustomobject]@{ Name = 'varify_integration.xml'; MinimumCount = 11 }
    [pscustomobject]@{ Name = 'script_component.xml'; MinimumCount = 9 }
    [pscustomobject]@{ Name = 'script_integration.xml'; MinimumCount = 4 }
)
$regressionResiduePrefixes = @(
    'chat-config-test-',
    'chat-startup-tests-',
    'gate-status-startup-tests-',
    'chat-process-harness-',
    'chat-instance-validation-',
    'chat-instance-lifecycle-',
    'varify-config-',
    'varify-startup-',
    'chat-proto-mutation-'
)
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
    $ServerIntermediateRoot = Join-Path $repoRoot 'build\windows-msbuild-obj'
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

function Resolve-CMake {
    $cmake = Require-Command 'cmake.exe' 'Install CMake 3.24 or newer and add it to PATH.'
    $versionText = (& $cmake --version 2>&1 | Select-Object -First 1)
    $versionMatch = [regex]::Match([string]$versionText, '(\d+\.\d+(?:\.\d+)?)')
    if ($LASTEXITCODE -ne 0 -or -not $versionMatch.Success) {
        throw "Unable to determine the CMake version: $versionText"
    }
    $cmakeVersion = $versionMatch.Groups[1].Value
    if ([version]$cmakeVersion -lt [version]'3.24') {
        throw "CMake 3.24 or newer is required for safe --fresh recovery; found $cmakeVersion."
    }
    return $cmake
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
        } elseif ($candidates.Count -gt 1) {
            throw "Multiple Qt MinGW toolchains were found; pass -MinGwRoot explicitly: $($candidates.FullName -join ', ')"
        }
    }
    if ([string]::IsNullOrWhiteSpace($MinGwRoot)) {
        throw 'Set MINGW_ROOT, or pass -MinGwRoot, to the compiler bundled with the selected Qt kit.'
    }

    $resolvedMinGw = (Resolve-Path -LiteralPath $MinGwRoot).Path
    $cxx = Require-File (Join-Path $resolvedMinGw 'bin\g++.exe') 'The Qt MinGW compiler was not found.'
    $cc = Require-File (Join-Path $resolvedMinGw 'bin\gcc.exe') 'The Qt MinGW compiler was not found.'
    $compilerVersion = (& $cxx -dumpfullversion -dumpversion 2>&1 | Select-Object -First 1).Trim()
    if ($LASTEXITCODE -ne 0 -or $compilerVersion -ne '11.2.0') {
        throw "Expected the Qt MinGW 11.2.0 compiler; found '$compilerVersion' at $cxx."
    }
    $compilerTarget = (& $cxx -dumpmachine 2>&1 | Select-Object -First 1).Trim()
    if ($LASTEXITCODE -ne 0 -or $compilerTarget -ne 'x86_64-w64-mingw32') {
        throw "Expected a 64-bit Windows MinGW compiler; found '$compilerTarget' at $cxx."
    }

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
        "--x-install-root=$VcpkgInstalledRoot"
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
        '/p:VcpkgManifestInstall=false'
        "/p:VcpkgInstalledDir=$VcpkgInstalledRoot\"
    )
    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Server build failed with exit code $LASTEXITCODE."
    }
}

function Run-ServerTests {
    Confirm-TestStructure
    [void](Require-File (Join-Path $varifySource 'node_modules\@grpc\grpc-js\package.json') `
        'Restore VarifyServer dependencies with RestoreVarify or npm ci before running the C++ to Node loopback contract.')
    $residuePrefixes = @('chat-config-test-', 'chat-startup-tests-', 'gate-status-startup-tests-')
    $residueBefore = @(Get-RegressionResidueSnapshot -Prefixes $residuePrefixes)
    $vcpkg = Resolve-Vcpkg
    $msbuild = Resolve-MSBuild
    $arguments = @(
        $solution
        '/m'
        '/t:GateServer;StatusServer;ChatServer;ServerUnitTests;ServerComponentTests;ServerIntegrationTests;ChatGrpcClientTests'
        "/p:Configuration=$Configuration"
        '/p:Platform=x64'
        "/p:VcpkgRoot=$($vcpkg.Root)"
        "/p:VcpkgTriplet=$ServerTriplet"
        "/p:VcpkgHostTriplet=$ServerHostTriplet"
        "/p:ServerIntermediateRoot=$ServerIntermediateRoot"
        '/p:VcpkgManifestInstall=false'
        "/p:VcpkgInstalledDir=$VcpkgInstalledRoot\"
    )
    $reports = @(
        (Join-Path $testResults 'server_unit.xml')
        (Join-Path $testResults 'server_component.xml')
        (Join-Path $testResults 'server_integration.xml')
        (Join-Path $testResults 'server_chat_grpc_integration.xml')
        (Join-Path $testResults 'server_gate_unit.xml')
        (Join-Path $testResults 'server_status_unit.xml')
    )
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    foreach ($report in $reports) {
        if (Test-Path -LiteralPath $report) {
            Remove-Item -LiteralPath $report -Force
        }
    }

    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Server test build failed with exit code $LASTEXITCODE."
    }
    Invoke-ProtocolCompatibility 'check'

    $installedRoot = $VcpkgInstalledRoot
    foreach ($project in @($gateAsioTestProject, $statusAsioTestProject)) {
        $poolArguments = @(
            $project
            '/m'
            "/p:Configuration=$Configuration"
            '/p:Platform=x64'
            "/p:VcpkgRoot=$($vcpkg.Root)"
            "/p:VcpkgTriplet=$ServerTriplet"
            "/p:VcpkgHostTriplet=$ServerHostTriplet"
            "/p:ServerIntermediateRoot=$ServerIntermediateRoot"
            '/p:VcpkgManifestInstall=false'
            "/p:VcpkgInstalledDir=$installedRoot\"
        )
        & $msbuild @poolArguments
        if ($LASTEXITCODE -ne 0) {
            throw "Server Asio lifecycle test build failed with exit code $LASTEXITCODE`: $project"
        }
    }

    $installedBin = Join-Path $VcpkgInstalledRoot "$ServerTriplet\bin"
    if ($Configuration -eq 'Debug') {
        $installedBin = Join-Path $VcpkgInstalledRoot "$ServerTriplet\debug\bin"
    }
    if (-not (Test-Path -LiteralPath $installedBin -PathType Container)) {
        throw "The vcpkg app-local dependency directory is missing: $installedBin"
    }
    foreach ($productionBinary in @(
        (Require-File $gateServerExecutable 'Build the GateServer target first.')
        (Require-File $statusServerExecutable 'Build the StatusServer target first.')
        (Require-File $chatServerExecutable 'Build the ChatServer target first.')
    )) {
        & $vcpkg.Exe z-applocal "--target-binary=$productionBinary" "--installed-bin-dir=$installedBin"
        if ($LASTEXITCODE -ne 0) {
            throw "Server app-local deployment failed with exit code $LASTEXITCODE`: $productionBinary"
        }
    }

    $testBinary = Require-File $serverTestExecutable 'Build the ServerUnitTests target first.'
    $componentBinary = Require-File $serverComponentTestExecutable 'Build the ServerComponentTests target first.'
    $integrationBinary = Require-File $serverIntegrationTestExecutable 'Build the ServerIntegrationTests target first.'
    $chatGrpcClientBinary = Require-File $chatGrpcClientTestExecutable 'Build the ChatGrpcClientTests target first.'

    $executions = @(
        @{ Binary = $testBinary; Report = $reports[0]; ExpectedCount = 68 }
        @{ Binary = $componentBinary; Report = $reports[1]; ExpectedCount = 56 }
        @{ Binary = $integrationBinary; Report = $reports[2]; ExpectedCount = 93 }
        @{ Binary = $chatGrpcClientBinary; Report = $reports[3]; ExpectedCount = 4 }
        @{ Binary = (Require-File $gateAsioTestExecutable 'Build the Gate Asio lifecycle test target first.'); Report = $reports[4]; ExpectedCount = 2 }
        @{ Binary = (Require-File $statusAsioTestExecutable 'Build the Status Asio lifecycle test target first.'); Report = $reports[5]; ExpectedCount = 2 }
    )
    $failures = @()
    foreach ($execution in $executions) {
        & $vcpkg.Exe z-applocal "--target-binary=$($execution.Binary)" "--installed-bin-dir=$installedBin"
        if ($LASTEXITCODE -ne 0) {
            throw "Server test app-local deployment failed with exit code $LASTEXITCODE`: $($execution.Binary)"
        }
        Push-Location (Split-Path -Parent $execution.Binary)
        try {
            & $execution.Binary "--gtest_output=xml:$($execution.Report)"
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        if (-not (Test-Path -LiteralPath $execution.Report -PathType Leaf)) {
            throw "Server test report was not created: $($execution.Report)"
        }
        [void](Assert-RegressionReport -Path $execution.Report -ExpectedCount $execution.ExpectedCount)
        if ($exitCode -ne 0) {
            $failures += "$($execution.Binary) (exit $exitCode)"
        }
    }
    if ($failures.Count -gt 0) {
        throw "Server tests failed: $($failures -join '; ')"
    }
    Assert-NoNewRegressionResidue -Before $residueBefore -Prefixes $residuePrefixes -Lane 'Server'
}

function Build-Client {
    $cmake = Resolve-CMake
    $qt = Resolve-QtToolchain
    Write-Host "Qt root: $($qt.Root)"
    Write-Host "Qt C++ compiler: $($qt.CxxCompiler)"
    Write-Host "Ninja: $($qt.Ninja)"
    $cache = Join-Path $clientBuild 'CMakeCache.txt'
    $freshConfigure = $false
    if (Test-Path -LiteralPath $cache) {
        $cacheText = Get-Content -LiteralPath $cache -Raw
        $compilerMatch = [regex]::Match($cacheText, '(?m)^CMAKE_CXX_COMPILER:[^=]*=(.+)$')
        $ninjaMatch = [regex]::Match($cacheText, '(?m)^CMAKE_MAKE_PROGRAM:[^=]*=(.+)$')
        $cachedCompiler = $compilerMatch.Groups[1].Value.Trim().Replace('/', '\')
        $cachedNinja = $ninjaMatch.Groups[1].Value.Trim().Replace('/', '\')
        $incompleteCache = -not $compilerMatch.Success -or -not $ninjaMatch.Success -or
            $cachedCompiler.EndsWith('-NOTFOUND') -or $cachedNinja.EndsWith('-NOTFOUND')
        if (-not $incompleteCache -and
            ($cachedCompiler -ne $qt.CxxCompiler -or $cachedNinja -ne $qt.Ninja)) {
            throw "Client build cache uses a different compiler or Ninja. Remove the generated directory and retry: $clientBuild"
        }
        if ($incompleteCache) {
            Write-Warning "Client build cache is incomplete; using CMake --fresh with the resolved Qt toolchain."
            $freshConfigure = $true
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
    if ($freshConfigure) {
        $configureArguments = @('--fresh') + $configureArguments
    }
    & $cmake @configureArguments
    if ($LASTEXITCODE -ne 0) {
        throw "Client configure failed with exit code $LASTEXITCODE."
    }
    & $cmake --build $clientBuild
    if ($LASTEXITCODE -ne 0) {
        throw "Client build failed with exit code $LASTEXITCODE."
    }
}

function Run-ClientTests {
    Confirm-TestStructure
    Build-Client
    $ctest = Require-Command 'ctest.exe' 'Install CMake 3.24 or newer and add it to PATH.'
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $failures = @()
    foreach ($group in $clientTestGroups) {
        if (Test-Path -LiteralPath $group.Report) {
            Remove-Item -LiteralPath $group.Report -Force
        }
        & $ctest --test-dir $clientBuild -L $group.Level --output-on-failure --output-junit $group.Report
        $testExitCode = $LASTEXITCODE
        if (-not (Test-Path -LiteralPath $group.Report -PathType Leaf)) {
            throw "Qt client $($group.Level) test report was not created: $($group.Report)"
        }
        [void](Assert-RegressionReport -Path $group.Report -ExpectedCount $group.ExpectedCount)
        if ($testExitCode -ne 0) {
            $failures += "$($group.Level) (exit $testExitCode)"
        }
    }
    if ($failures.Count -gt 0) {
        throw "Qt client tests failed: $($failures -join '; ')"
    }
}

function Assert-RegressionReport {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][int]$ExpectedCount
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Required regression report was not created: $Path"
    }
    try {
        [xml]$reportXml = Get-Content -LiteralPath $Path -Raw
    } catch {
        throw "Regression report is not valid XML: $Path. $($_.Exception.Message)"
    }
    $reportText = Get-Content -LiteralPath $Path -Raw
    $testcases = @($reportXml.SelectNodes('//testcase'))
    $failures = @($reportXml.SelectNodes('//failure'))
    $errors = @($reportXml.SelectNodes('//error'))
    $skipped = @($reportXml.SelectNodes('//skipped'))
    $unavailable = @(
        $testcases | Where-Object {
            $_.GetAttribute('status') -match '^(?i:notrun|disabled|skipped|unavailable|timeout)$' -or
            $_.GetAttribute('result') -match '^(?i:notrun|disabled|skipped|unavailable|timeout|suppressed)$'
        }
    )
    if ($testcases.Count -ne $ExpectedCount) {
        throw "Regression report count mismatch for ${Path}: expected $ExpectedCount, found $($testcases.Count)."
    }
    if ($failures.Count -ne 0 -or $errors.Count -ne 0) {
        throw "Regression report contains failures for ${Path}: failures=$($failures.Count), errors=$($errors.Count)."
    }
    if ($skipped.Count -ne 0 -or $unavailable.Count -ne 0) {
        throw "Regression report contains skipped, disabled, unavailable, or timed-out cases: $Path."
    }
    if ($reportText -match '(?i)(?:password|passwd|secret|token|verification[-_ ]?code|email)\s*[:=]\s*[^\s<]{3,}') {
        throw "Regression report contains a credential-shaped assignment: $Path."
    }
    return $testcases.Count
}

function Assert-RegressionCleanupEvidence {
    $requiredEvidence = @(
        [pscustomobject]@{ Report = 'server_integration.xml'; Name = 'TeardownIsReverseOrderedAndPreservesPrimaryAndCleanupFailures' }
        [pscustomobject]@{ Report = 'server_integration.xml'; Name = 'StopUsesGracefulSignalAndClosesPipesByDeadline' }
        [pscustomobject]@{ Report = 'server_integration.xml'; Name = 'StopReleasesSessionsThreadsSocketsAndServerOwnership' }
        [pscustomobject]@{ Report = 'server_integration.xml'; Name = 'ChatRealDependencyBoundaryIsExplicitBoundedAndResidueFree' }
        [pscustomobject]@{ Report = 'client_integration.xml'; Name = 'http_transport.deletingTransportReleasesReplyAndLoopbackSocket' }
        [pscustomobject]@{ Report = 'client_integration.xml'; Name = 'tcp_transport.closeAndDeleteReleaseOwnedResources' }
    )
    foreach ($evidence in $requiredEvidence) {
        $reportPath = Join-Path $testResults $evidence.Report
        [xml]$reportXml = Get-Content -LiteralPath $reportPath -Raw
        $matchingCase = @($reportXml.SelectNodes('//testcase') | Where-Object { $_.name -eq $evidence.Name })
        if ($matchingCase.Count -ne 1) {
            throw "Regression cleanup evidence is missing or duplicated in $($evidence.Report): $($evidence.Name)."
        }
    }
}

function Confirm-RegressionReports {
    $total = 0
    foreach ($group in $regressionReportGroups) {
        $total += Assert-RegressionReport `
            -Path (Join-Path $testResults $group.Name) `
            -ExpectedCount $group.ExpectedCount
    }
    if ($regressionReportGroups.Count -ne 13 -or $total -ne 313) {
        throw "Regression report baseline mismatch: expected 13 reports and 313 testcases; found $($regressionReportGroups.Count) reports and $total testcases."
    }
    $legacyTotal = 0
    foreach ($legacyGroup in $legacyRegressionReportGroups) {
        $registeredGroup = @($regressionReportGroups | Where-Object { $_.Name -eq $legacyGroup.Name })
        if ($registeredGroup.Count -ne 1 -or $registeredGroup[0].ExpectedCount -lt $legacyGroup.MinimumCount) {
            throw "Legacy regression floor is not preserved for $($legacyGroup.Name)."
        }
        $legacyTotal += $legacyGroup.MinimumCount
    }
    if ($legacyRegressionReportGroups.Count -ne 12 -or $legacyTotal -ne 232) {
        throw 'Legacy regression floor must remain exactly 12 reports and 232 testcases.'
    }
    Assert-RegressionCleanupEvidence
    Write-Host "Regression report audit passed: $total testcases across $($regressionReportGroups.Count) reports."
}

function Get-RegressionResidueSnapshot {
    param([Parameter(Mandatory = $true)][string[]]$Prefixes)

    $temporaryRoot = [System.IO.Path]::GetFullPath([System.IO.Path]::GetTempPath())
    return @(
        foreach ($prefix in $Prefixes) {
            Get-ChildItem -LiteralPath $temporaryRoot -Filter "$prefix*" -Force -ErrorAction SilentlyContinue |
                ForEach-Object { $_.FullName }
        }
    )
}

function Assert-NoNewRegressionResidue {
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Before,
        [Parameter(Mandatory = $true)][string[]]$Prefixes,
        [Parameter(Mandatory = $true)][string]$Lane
    )

    $after = @(Get-RegressionResidueSnapshot -Prefixes $Prefixes)
    $newResidue = @($after | Where-Object { $_ -notin $Before })
    if ($newResidue.Count -gt 0) {
        throw "$Lane test cleanup left owned temporary state: $($newResidue -join '; ')"
    }
}

function Invoke-ProtocolCompatibility {
    param([ValidateSet('generate', 'check')][string]$Mode)

    $node = Require-Command 'node.exe' 'Node.js is required for protocol generation and compatibility checks.'
    $protocolTool = Require-File (Join-Path $repoRoot 'scripts\protocol-compatibility.js') `
        'The protocol compatibility tool is missing.'
    $previousInstalledRoot = $env:CHAT_VCPKG_INSTALLED_ROOT
    try {
        $env:CHAT_VCPKG_INSTALLED_ROOT = $VcpkgInstalledRoot
        & $node $protocolTool $Mode
        $exitCode = $LASTEXITCODE
    } finally {
        if ($null -eq $previousInstalledRoot) {
            Remove-Item Env:CHAT_VCPKG_INSTALLED_ROOT -ErrorAction SilentlyContinue
        } else {
            $env:CHAT_VCPKG_INSTALLED_ROOT = $previousInstalledRoot
        }
    }
    if ($exitCode -ne 0) {
        throw "Protocol $Mode failed with exit code $exitCode."
    }
}

function Get-RepositoryRelativePath {
    param([string]$Path)

    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $rootPrefix = $repoRoot.TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Path is outside the repository: $resolved"
    }
    return $resolved.Substring($rootPrefix.Length).Replace('\', '/')
}

function Confirm-TestStructure {
    function Assert-ModuleReadme {
        param([System.IO.FileInfo[]]$TestFiles, [string]$Toolchain)

        foreach ($directory in @($TestFiles | ForEach-Object { $_.DirectoryName } | Sort-Object -Unique)) {
            $readme = Join-Path $directory 'README.md'
            if (-not (Test-Path -LiteralPath $readme -PathType Leaf)) {
                throw "$Toolchain test Module is missing README.md: $(Get-RepositoryRelativePath $directory)"
            }
        }
    }

    $serverTests = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests\server') `
        -Recurse -File -Filter '*.cpp')
    $serverProjects = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests\server') `
        -Recurse -File -Filter '*.vcxproj')
    $registeredServerSources = @()
    foreach ($project in $serverProjects) {
        $projectXml = [xml](Get-Content -LiteralPath $project.FullName -Raw)
        $namespace = New-Object System.Xml.XmlNamespaceManager($projectXml.NameTable)
        $namespace.AddNamespace('msb', 'http://schemas.microsoft.com/developer/msbuild/2003')
        foreach ($compile in $projectXml.SelectNodes('//msb:ItemGroup/msb:ClCompile[@Include]', $namespace)) {
            $registeredServerSources += [IO.Path]::GetFullPath(
                (Join-Path $project.DirectoryName $compile.GetAttribute('Include'))
            )
        }
    }
    # Linux-only process contracts are explicitly owned by CMake, not MSBuild.
    # Still require a real executable and CTest command: this is not a skip list.
    $linuxProcessContracts = @(
        @{ Source = 'tests/server/process-harness/posix_process_tests.cpp';
           CMake = 'tests/server/process-harness/CMakeLists.txt'; Target = 'posix_process_tests' },
        @{ Source = 'tests/server/process-harness/process_harness_posix_tests.cpp';
           CMake = 'CMakeLists.txt'; Target = 'process_harness_posix_tests' }
    )
    foreach ($contract in $linuxProcessContracts) {
        $cmakeText = Get-Content -LiteralPath (Join-Path $repoRoot $contract.CMake) -Raw
        $sourceName = [regex]::Escape([IO.Path]::GetFileName($contract.Source))
        $targetName = [regex]::Escape($contract.Target)
        if ($cmakeText -notmatch "(?s)add_executable\($targetName\s+[^)]*$sourceName" -or
            $cmakeText -notmatch "COMMAND\s+$targetName(?:\s|\))") {
            throw "Linux process contract has no executable/CTest registration: $($contract.Source)"
        }
        $registeredServerSources += [IO.Path]::GetFullPath((Join-Path $repoRoot $contract.Source))
    }
    foreach ($test in $serverTests) {
        if ($registeredServerSources -notcontains $test.FullName) {
            throw "Unregistered Server test source: $(Get-RepositoryRelativePath $test.FullName)"
        }
    }
    Assert-ModuleReadme -TestFiles $serverTests -Toolchain 'Server'
    $asioContracts = @($serverTests | Where-Object { $_.Name -eq 'asio_pool_contract_tests.cpp' })
    if ($asioContracts.Count -ne 1 -or $asioContracts[0].Directory.Name -ne 'lifecycle') {
        throw 'The shared Asio pool contract source must have one owner: tests/server/lifecycle.'
    }

    $gateProject = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateServer.vcxproj') -Raw
    $gateGrpcProject = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateGrpcClients.vcxproj') -Raw
    $gateRequestProject = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateRequest.vcxproj') -Raw
    $gateRequestFilters = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateRequest.vcxproj.filters') -Raw
    $gateTransportProject = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateTransport.vcxproj') -Raw
    $chatProject = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatServer.vcxproj') -Raw
    $chatTransportProject = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatTransport.vcxproj') -Raw
    $chatGrpcProject = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatGrpcClients.vcxproj') -Raw
    $logicDispatcherProject = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\LogicDispatcher.vcxproj') -Raw
    $statusProject = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusServer.vcxproj') -Raw
    $statusRoutingProject = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusRouting.vcxproj') -Raw
    $statusTransportProject = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusTransport.vcxproj') -Raw
    $chatSessionStateProject = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatSessionState.vcxproj') -Raw
    $chatSessionStateFilters = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatSessionState.vcxproj.filters') -Raw
    $solutionRegistration = Get-Content -LiteralPath (Join-Path $repoRoot 'Chat.sln') -Raw
    $unitProject = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\ServerUnitTests.vcxproj') -Raw
    $componentProject = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\ServerComponentTests.vcxproj') -Raw
    $integrationProject = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\ServerIntegrationTests.vcxproj') -Raw
    $chatGrpcTestProject = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\ChatGrpcClientTests.vcxproj') -Raw
    foreach ($registration in @(
        @{ Text = $gateTransportProject; Pattern = 'ClCompile Include="GateResponse\.cpp"'; Owner = 'Gate transport library' }
        @{ Text = $gateTransportProject; Pattern = 'ClCompile Include="CServer\.cpp"'; Owner = 'Gate transport library' }
        @{ Text = $gateTransportProject; Pattern = 'ClCompile Include="HttpConnection\.cpp"'; Owner = 'Gate transport library' }
        @{ Text = $gateTransportProject; Pattern = 'ClCompile Include="LogicSystem\.cpp"'; Owner = 'Gate transport library' }
        @{ Text = $gateProject; Pattern = 'ProjectReference Include="GateTransport\.vcxproj"'; Owner = 'GateServer' }
        @{ Text = $componentProject; Pattern = 'gate-response\\gate_response_tests\.cpp'; Owner = 'Server Component tests' }
        @{ Text = $componentProject; Pattern = 'GateServer\\GateServer\\GateTransport\.vcxproj'; Owner = 'Server Component tests' }
        @{ Text = $integrationProject; Pattern = 'GateServer\\GateServer\\GateTransport\.vcxproj'; Owner = 'Server Integration tests' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share the production GateTransport target."
        }
    }
    foreach ($consumer in @(
        @{ Text = $gateProject; Owner = 'GateServer' }
        @{ Text = $componentProject; Owner = 'Server Component tests' }
        @{ Text = $integrationProject; Owner = 'Server Integration tests' }
    )) {
        if ($consumer.Text -match 'ClCompile Include="[^\"]*GateServer\\GateServer\\(?:CServer|GateResponse|HttpConnection|LogicSystem)\.cpp"' -or
            $consumer.Text -match 'ClCompile Include="(?:CServer|GateResponse|HttpConnection|LogicSystem)\.cpp"') {
            throw "$($consumer.Owner) must not compile GateTransport production sources directly."
        }
    }
    $logicDispatcherSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\LogicDispatcher.cpp') -Raw
    $logicSystemHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\LogicSystem.h') -Raw
    $logicSystemSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\LogicSystem.cpp') -Raw
    $chatSessionSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\CSession.cpp') -Raw
    $logicDispatcherTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\logic-dispatcher\logic_dispatcher_tests.cpp') -Raw
    if (@([regex]::Matches($logicDispatcherTests, 'TEST\(LogicDispatcherTests,')).Count -ne 7 -or
        @([regex]::Matches($logicDispatcherTests, 'T08-LOGIC-0[1-7]')).Count -ne 7) {
        throw 'Logic dispatcher tests must register exactly T08-LOGIC-01..07 as seven Server Unit testcases.'
    }
    if ($logicDispatcherSource -notmatch 'messages\.size\(\)\s*>=\s*MAX_DEALQUE' -or
        $logicDispatcherSource -notmatch 'stopping\s*&&\s*messages\.empty\(\)' -or
        $logicDispatcherSource -notmatch 'std::call_once' -or
        $logicDispatcherSource -match 'message\.body[^;]*SPDLOG') {
        throw 'LogicDispatcher must own exact capacity, drain, idempotent stop, and body-free diagnostics.'
    }
    if ($logicSystemHeader -notmatch 'public\s+LogicDispatcher' -or
        $logicSystemHeader -match '_msg_que|_worker_thread|condition_variable|PostMsgToQue|DealMsg' -or
        $logicSystemSource -match '_msg_que|_worker_thread|condition_variable|PostMsgToQue|DealMsg') {
        throw 'LogicSystem must retain handler registration while queue/worker ownership stays in LogicDispatcher.'
    }
    foreach ($submitResult in @('Accepted', 'Full', 'Closed')) {
        if ($chatSessionSource -notmatch "LogicSubmitResult::$submitResult") {
            throw "CSession must handle LogicDispatcher result $submitResult."
        }
    }
    if ($chatSessionSource -notmatch '_dispatcher->Submit\s*\(' -or
        $chatSessionSource -match 'PostMsgToQue') {
        throw 'CSession must submit through the production LogicDispatcher Interface.'
    }
    $chatTcpTransportTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\integration-host\chat_tcp_transport_tests.cpp') -Raw
    foreach ($registration in @(
        @{ Text = $chatTransportProject; Pattern = 'ClCompile Include="ChatFrameCodec\.cpp"'; Owner = 'Chat transport library' }
        @{ Text = $chatTransportProject; Pattern = 'ClCompile Include="CServer\.cpp"'; Owner = 'Chat transport library' }
        @{ Text = $chatTransportProject; Pattern = 'ClCompile Include="CSession\.cpp"'; Owner = 'Chat transport library' }
        @{ Text = $chatTransportProject; Pattern = 'ClCompile Include="MsgNode\.cpp"'; Owner = 'Chat transport library' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="ChatTransport\.vcxproj"'; Owner = 'ChatServer' }
        @{ Text = $unitProject; Pattern = 'ChatServer\\ChatServer\\ChatTransport\.vcxproj'; Owner = 'Server Unit tests' }
        @{ Text = $integrationProject; Pattern = 'ChatServer\\ChatServer\\ChatTransport\.vcxproj'; Owner = 'Server Integration tests' }
        @{ Text = $solutionRegistration; Pattern = '"ChatTransport", "ChatServer\\ChatServer\\ChatTransport\.vcxproj"'; Owner = 'Chat solution' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share and register the production ChatTransport target."
        }
    }
    foreach ($consumer in @(
        @{ Text = $chatProject; Owner = 'ChatServer' }
        @{ Text = $unitProject; Owner = 'Server Unit tests' }
        @{ Text = $integrationProject; Owner = 'Server Integration tests' }
    )) {
        if ($consumer.Text -match 'ClCompile Include="[^\"]*ChatServer\\ChatServer\\(?:ChatFrameCodec|CServer|CSession|MsgNode)\.cpp"' -or
            $consumer.Text -match 'ClCompile Include="(?:ChatFrameCodec|CServer|CSession|MsgNode)\.cpp"') {
            throw "$($consumer.Owner) must not compile ChatTransport production sources directly."
        }
    }
    if (@([regex]::Matches($chatTcpTransportTests, 'TEST_F\(T09_CTCP_Stream,')).Count -ne 16 -or
        @([regex]::Matches($chatTcpTransportTests, 'T09-CTCP-(?:0[1-9]|1[0-6])')).Count -ne 16) {
        throw 'Chat TCP transport tests must register exactly T09-CTCP-01..16 as sixteen Server Integration testcases.'
    }
    if ($chatTransportProject -match '(?i)RedisMgr|MysqlMgr|StatusGrpcClient|ChatGrpcClient') {
        throw 'ChatTransport must remain isolated from Redis, MySQL, Status, and peer-RPC dependency managers.'
    }
    $statusRoutingHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusRouting.h') -Raw
    $statusServiceSource = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusServiceImpl.cpp') -Raw
    $statusUnitTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\status-routing\status_routing_unit_tests.cpp') -Raw
    $statusComponentTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\status-routing\status_routing_component_tests.cpp') -Raw
    $statusGrpcTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\integration-host\status_grpc_transport_tests.cpp') -Raw
    if (@([regex]::Matches($statusUnitTests, 'TEST\(StatusRoutingUnitTests,')).Count -ne 8 -or
        @([regex]::Matches($statusComponentTests, 'TEST\(StatusRoutingComponentTests,')).Count -ne 6 -or
        @([regex]::Matches($statusUnitTests + $statusComponentTests, 'T08-STATUS-(?:0[1-9]|1[0-4])')).Count -ne 14) {
        throw 'Status routing tests must register exactly T08-STATUS-01..14 as eight Unit and six Component testcases.'
    }
    foreach ($registration in @(
        @{ Text = $statusProject; Pattern = 'ProjectReference Include="StatusRouting\.vcxproj"'; Owner = 'StatusServer' }
        @{ Text = $unitProject; Pattern = 'status-routing\\status_routing_unit_tests\.cpp'; Owner = 'Server Unit tests' }
        @{ Text = $unitProject; Pattern = 'StatusRouting\.vcxproj'; Owner = 'Server Unit tests' }
        @{ Text = $componentProject; Pattern = 'status-routing\\status_routing_component_tests\.cpp'; Owner = 'Server Component tests' }
        @{ Text = $componentProject; Pattern = 'StatusRouting\.vcxproj'; Owner = 'Server Component tests' }
        @{ Text = $statusRoutingProject; Pattern = 'ClCompile Include="StatusRouting\.cpp"'; Owner = 'Status routing library' }
        @{ Text = $statusRoutingProject; Pattern = 'ClCompile Include="StatusRoutingProduction\.cpp"'; Owner = 'Status routing production Adapter' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share the production StatusRouting Module with its tests."
        }
    }
    foreach ($registration in @(
        @{ Text = $statusTransportProject; Pattern = 'ClCompile Include="StatusGrpcServer\.cpp"'; Owner = 'Status transport library' }
        @{ Text = $statusTransportProject; Pattern = 'ClCompile Include="StatusServiceImpl\.cpp"'; Owner = 'Status transport library' }
        @{ Text = $statusTransportProject; Pattern = 'ClCompile Include="\$\(ProtocolGeneratedDir\)\\status\.grpc\.pb\.cc"'; Owner = 'Status gRPC generated transport' }
        @{ Text = $statusTransportProject; Pattern = 'ClCompile Include="\$\(ProtocolGeneratedDir\)\\status\.pb\.cc"'; Owner = 'Status protobuf generated transport' }
        @{ Text = $statusProject; Pattern = 'ProjectReference Include="StatusTransport\.vcxproj"'; Owner = 'StatusServer' }
        @{ Text = $integrationProject; Pattern = 'StatusServer\\StatusServer\\StatusTransport\.vcxproj'; Owner = 'Server Integration tests' }
        @{ Text = $solutionRegistration; Pattern = '"StatusTransport", "StatusServer\\StatusServer\\StatusTransport\.vcxproj"'; Owner = 'Chat solution' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share and register the production StatusTransport target."
        }
    }
    foreach ($consumer in @(
        @{ Text = $statusProject; Owner = 'StatusServer' }
        @{ Text = $integrationProject; Owner = 'Server Integration tests' }
    )) {
        if ($consumer.Text -match 'ClCompile Include="[^\"]*StatusServer\\StatusServer\\(?:StatusGrpcServer|StatusServiceImpl)\.cpp"' -or
            $consumer.Text -match 'ClCompile Include="(?:StatusGrpcServer|StatusServiceImpl)\.cpp"') {
            throw "$($consumer.Owner) must not compile StatusTransport production sources directly."
        }
    }
    if (@([regex]::Matches($statusGrpcTests, 'TEST(?:_F)?\s*\(\s*T09_SGRPC_')).Count -ne 12 -or
        @([regex]::Matches($statusGrpcTests, 'T09-SGRPC-(?:0[1-9]|1[0-2])')).Count -ne 12) {
        throw 'Status gRPC transport tests must register exactly T09-SGRPC-01..12 as twelve Server Integration testcases.'
    }
    if ($statusRoutingHeader -notmatch 'AssignmentResult\s+Assign\s*\(int uid\)' -or
        $statusRoutingHeader -notmatch 'LoginResult\s+Validate\s*\(int uid, const std::string& token\)' -or
        $statusServiceSource -notmatch 'routing_\.Assign\s*\(' -or
        $statusServiceSource -notmatch 'routing_\.Validate\s*\(' -or
        $statusServiceSource -match 'RedisMgr|_servers|\.begin\(\)') {
        throw 'StatusServiceImpl must call only the two-method production StatusRouting Interface.'
    }
    $chatSessionStateHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatSessionState.h') -Raw
    $chatSessionStateSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatSessionState.cpp') -Raw
    $chatSessionTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\chat-session-state\chat_session_state_tests.cpp') -Raw
    $chatServerSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\CServer.cpp') -Raw
    $chatSessionHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\CSession.h') -Raw
    $userManagerSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\UserMgr.cpp') -Raw
    if (@([regex]::Matches($chatSessionTests, 'TEST\(ChatSessionStateTests,')).Count -ne 10 -or
        @([regex]::Matches($chatSessionTests, 'T08-SESSION-(?:0[1-9]|10)')).Count -ne 10) {
        throw 'Chat session state tests must register exactly T08-SESSION-01..10 as ten Server Component testcases.'
    }
    foreach ($registration in @(
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="ChatSessionState\.vcxproj"'; Owner = 'ChatServer' }
        @{ Text = $componentProject; Pattern = 'chat-session-state\\chat_session_state_tests\.cpp'; Owner = 'Server Component tests' }
        @{ Text = $componentProject; Pattern = 'ChatSessionState\.vcxproj'; Owner = 'Server Component tests' }
        @{ Text = $chatSessionStateProject; Pattern = 'ClCompile Include="ChatSessionState\.cpp"'; Owner = 'Chat session state library' }
        @{ Text = $chatSessionStateProject; Pattern = 'ClCompile Include="ChatSessionStateProduction\.cpp"'; Owner = 'Chat session production Adapters' }
        @{ Text = $chatSessionStateFilters; Pattern = 'ChatSessionStateProduction\.cpp'; Owner = 'Chat session state filters' }
        @{ Text = $solutionRegistration; Pattern = '"ChatSessionState", "ChatServer\\ChatServer\\ChatSessionState\.vcxproj"'; Owner = 'Chat solution' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share and register the production ChatSessionState Module."
        }
    }
    if ($chatSessionStateHeader -notmatch 'Handle Create\s*\(' -or
        $chatSessionStateHeader -notmatch 'RegisterCurrent\s*\(' -or
        $chatSessionStateHeader -notmatch 'Handle FindCurrent\s*\(' -or
        $chatSessionStateHeader -notmatch 'void Close\s*\(' -or
        $chatSessionStateHeader -notmatch 'SessionSendResult Send\s*\(' -or
        $chatSessionStateHeader -match 'clearForTest|unordered_map|deque<') {
        throw 'ChatSessionState must expose its opaque-handle production Interface without private state or test helpers.'
    }
    if ($chatSessionStateSource -notmatch 'frames\.size\(\)\s*>=\s*MAX_SENDQUE' -or
        $chatSessionStateSource -notmatch 'found->second\s*!=\s*session' -or
        $chatSessionStateSource -notmatch 'write_generation\s*!=\s*generation') {
        throw 'ChatSessionState must own exact capacity, matching delete, and exactly-once writer completion.'
    }
    if ($chatServerSource -notmatch '_session_state->Close\s*\(' -or
        $chatSessionHeader -match '_send_que|_send_mutex|HandleWrite' -or
        $chatSessionSource -notmatch '_session_state->Send\s*\(' -or
        $userManagerSource -notmatch 'MakeProductionChatSessionState' -or
        $userManagerSource -match '_uid_to_session|RemoveUserSession|SetUserSession') {
        throw 'CServer, CSession, and UserMgr must delegate session identity/send state to one ChatSessionState Interface.'
    }
    $gateLogic = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\LogicSystem.cpp') -Raw
    $gateRequestHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateRequest.h') -Raw
    $gateRequestTests = Get-Content -LiteralPath (Join-Path $repoRoot 'tests\server\gate-request\gate_request_component_tests.cpp') -Raw
    if (@([regex]::Matches($gateRequestTests, 'TEST_F\(GateRequestComponentTests,')).Count -ne 16) {
        throw 'Gate request orchestration tests must register exactly sixteen Server Component testcases.'
    }
    foreach ($gateRequestId in 1..16) {
        $testId = 'T08-GATE-{0:D2}' -f $gateRequestId
        if (@([regex]::Matches($gateRequestTests, [regex]::Escape($testId))).Count -ne 1) {
            throw "Gate request orchestration tests must register $testId exactly once."
        }
    }
    foreach ($registration in @(
        @{ Text = $gateProject; Pattern = 'ProjectReference Include="GateRequest\.vcxproj"'; Owner = 'GateServer' }
        @{ Text = $componentProject; Pattern = 'gate-request\\gate_request_component_tests\.cpp'; Owner = 'Server Component tests' }
        @{ Text = $componentProject; Pattern = 'GateRequest\.vcxproj'; Owner = 'Server Component tests' }
        @{ Text = $gateRequestProject; Pattern = 'ClCompile Include="GateRequest\.cpp"'; Owner = 'Gate request library' }
        @{ Text = $gateRequestProject; Pattern = 'ClCompile Include="GateRequestProduction\.cpp"'; Owner = 'Gate request production Adapters' }
        @{ Text = $gateRequestFilters; Pattern = 'GateRequestProduction\.cpp'; Owner = 'Gate request filters' }
        @{ Text = $solutionRegistration; Pattern = '"GateRequest", "GateServer\\GateServer\\GateRequest\.vcxproj"'; Owner = 'Chat solution' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share and register the production GateRequest Module."
        }
    }
    if ($gateRequestHeader -notmatch 'Result\s+Handle\s*\(Endpoint endpoint, const Json::Value& request\)' -or
        @([regex]::Matches($gateRequestHeader, '\bHandle\s*\(')).Count -ne 1 -or
        $gateRequestHeader -match 'clearForTest|unordered_map|vector<|deque<') {
        throw 'GateRequest must expose only its production Handle Interface without private state or test helpers.'
    }
    $gateRoutes = @{
        '/get_varifycode' = 'GetVarifyCode'
        '/user_register' = 'UserRegister'
        '/reset_pwd' = 'ResetPassword'
        '/user_login' = 'UserLogin'
    }
    foreach ($route in $gateRoutes.GetEnumerator()) {
        $registration = 'RegPost\("{0}", \[this, write_gate_response\]' -f [regex]::Escape($route.Key)
        $shapingCall = 'write_gate_response\(connection, gate::Endpoint::{0}' -f [regex]::Escape($route.Value)
        $requestCall = '_gate_request\.Handle\(gate::Endpoint::{0}, request\)' -f [regex]::Escape($route.Value)
        if ($gateLogic -notmatch $registration -or $gateLogic -notmatch $shapingCall -or $gateLogic -notmatch $requestCall) {
            throw "Gate route '$($route.Key)' must call the shared GateRequest Module through the GateResponse Interface with endpoint $($route.Value)."
        }
    }
    if ($gateLogic -match 'VerifyGrpcClient|RedisMgr|MysqlMgr|StatusGrpcClient') {
        throw 'LogicSystem routes must not bypass the GateRequest production Adapters.'
    }
    if ($integrationProject -notmatch 'startup\\gate_status_startup_tests\.cpp') {
        throw 'Server Integration tests must register the Gate/Status production process contracts.'
    }
    $integrationHostTests = Get-Content -LiteralPath `
        (Join-Path $repoRoot 'tests\server\integration-host\integration_host_contract_tests.cpp') -Raw
    if (@([regex]::Matches($integrationHostTests, 'TEST\(T09_HOST_Contract,')).Count -ne 6) {
        throw 'Integration host tests must register exactly six T09-HOST contract testcases.'
    }
    foreach ($hostId in 1..6) {
        $testId = 'T09-HOST-{0:D2}' -f $hostId
        if (@([regex]::Matches($integrationHostTests, [regex]::Escape($testId))).Count -ne 1) {
            throw "Integration host tests must register $testId exactly once."
        }
    }
    foreach ($registration in @(
        @{ Pattern = 'integration-host\\integration_host_contract_tests\.cpp'; Owner = 'T09-HOST contract source' }
        @{ Pattern = '\.\.\\support\\IntegrationHostFactory\.cpp'; Owner = 'IntegrationHostFactory composition source' }
        @{ Pattern = 'LogicDispatcher\.vcxproj'; Owner = 'LogicDispatcher shared target' }
        @{ Pattern = 'ChatSessionState\.vcxproj'; Owner = 'ChatSessionState shared target' }
        @{ Pattern = 'GateRequest\.vcxproj'; Owner = 'GateRequest shared target' }
        @{ Pattern = 'StatusRouting\.vcxproj'; Owner = 'StatusRouting shared target' }
    )) {
        if ($integrationProject -notmatch $registration.Pattern) {
            throw "Server Integration tests must register the $($registration.Owner)."
        }
    }
    if ($integrationProject -match 'ClCompile Include="[^"]*(?:LogicDispatcher|ChatSessionState|GateRequest|StatusRouting)(?:Production)?\.cpp"') {
        throw 'Server Integration tests must link shared Phase 3A targets instead of compiling production implementations directly.'
    }
    $formalCompositionTests = Get-Content -LiteralPath `
        (Join-Path $repoRoot 'tests\server\integration-host\production_composition_tests.cpp') -Raw
    if (@([regex]::Matches($formalCompositionTests, 'TEST\(T09_COMP_Formal,')).Count -ne 6) {
        throw 'Formal composition tests must register exactly six actual T09-COMP Server Integration testcases.'
    }
    foreach ($compositionId in 1..6) {
        $testId = 'T09-COMP-{0:D2}' -f $compositionId
        if (@([regex]::Matches($formalCompositionTests, [regex]::Escape($testId))).Count -ne 1) {
            throw "Formal composition tests must register $testId exactly once."
        }
    }
    foreach ($registration in @(
        @{ Text = $integrationProject; Pattern = [regex]::Escape('integration-host\production_composition_tests.cpp'); Owner = 'T09-COMP runtime source' }
        @{ Text = $gateProject; Pattern = 'ProjectReference Include="GateTransport\.vcxproj"'; Owner = 'formal Gate transport' }
        @{ Text = $gateProject; Pattern = 'ProjectReference Include="GateRequest\.vcxproj"'; Owner = 'formal Gate business Module' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('GateServer\GateServer\GateTransport.vcxproj'); Owner = 'Gate Integration transport' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('GateServer\GateServer\GateRequest.vcxproj'); Owner = 'Gate Integration business Module' }
        @{ Text = $statusProject; Pattern = 'ProjectReference Include="StatusTransport\.vcxproj"'; Owner = 'formal Status transport' }
        @{ Text = $statusProject; Pattern = 'ProjectReference Include="StatusRouting\.vcxproj"'; Owner = 'formal Status business Module' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('StatusServer\StatusServer\StatusTransport.vcxproj'); Owner = 'Status Integration transport' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('StatusServer\StatusServer\StatusRouting.vcxproj'); Owner = 'Status Integration business Module' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="ChatTransport\.vcxproj"'; Owner = 'formal Chat transport' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="LogicDispatcher\.vcxproj"'; Owner = 'formal Chat dispatcher Module' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="ChatSessionState\.vcxproj"'; Owner = 'formal Chat session Module' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('ChatServer\ChatServer\ChatTransport.vcxproj'); Owner = 'Chat Integration transport' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('ChatServer\ChatServer\LogicDispatcher.vcxproj'); Owner = 'Chat Integration dispatcher Module' }
        @{ Text = $integrationProject; Pattern = [regex]::Escape('ChatServer\ChatServer\ChatSessionState.vcxproj'); Owner = 'Chat Integration session Module' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share the production target used by the formal executable and Integration tests."
        }
    }
    $gateFormalSource = Get-Content -LiteralPath (Join-Path $repoRoot 'GateServer\GateServer\GateServer.cpp') -Raw
    $statusFormalSource = Get-Content -LiteralPath (Join-Path $repoRoot 'StatusServer\StatusServer\StatusServer.cpp') -Raw
    $chatFormalSource = Get-Content -LiteralPath (Join-Path $repoRoot 'ChatServer\ChatServer\ChatServer.cpp') -Raw
    if ($gateFormalSource -notmatch 'gate::CreateProductionGateRequest\s*\(' -or
        $statusFormalSource -notmatch 'CreateProductionStatusRouting\s*\(' -or
        $chatFormalSource -notmatch 'LogicSystem::GetInstance\s*\(' -or
        $chatFormalSource -notmatch 'UserMgr::GetInstance\s*\(\)->Sessions\s*\(' -or
        $chatFormalSource -notmatch 'RedisMgr::GetInstance\s*\(') {
        throw 'Formal Gate, Status, and Chat composition roots must select their real production Adapters and Modules.'
    }
    $formalCompositionSurface = @(
        $gateFormalSource, $statusFormalSource, $chatFormalSource,
        $gateProject, $statusProject, $chatProject
    ) -join "`n"
    if ($formalCompositionSurface -match '(?i)--fake-dependenc(?:y|ies)|\b(?:CHAT_)?FAKE_DEPENDENC(?:Y|IES)\b|\bTEST_ONLY\b|#\s*if(?:def)?\s+[^\r\n]*\bTEST(?:ING)?\b') {
        throw 'Formal production composition must not expose a fake-dependency mode, test macro, or test-only Interface.'
    }
    foreach ($registration in @(
        @{ Text = $gateProject; Pattern = 'ProjectReference Include="GateGrpcClients\.vcxproj"'; Owner = 'GateServer' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="ChatGrpcClients\.vcxproj"'; Owner = 'ChatServer' }
        @{ Text = $chatProject; Pattern = 'ProjectReference Include="LogicDispatcher\.vcxproj"'; Owner = 'ChatServer' }
        @{ Text = $unitProject; Pattern = 'logic-dispatcher\\logic_dispatcher_tests\.cpp'; Owner = 'Server Unit tests' }
        @{ Text = $unitProject; Pattern = 'LogicDispatcher\.vcxproj'; Owner = 'Server Unit tests' }
        @{ Text = $logicDispatcherProject; Pattern = 'ClCompile Include="LogicDispatcher\.cpp"'; Owner = 'Logic dispatcher library' }
        @{ Text = $logicDispatcherProject; Pattern = 'ClInclude Include="LogicDispatcher\.h"'; Owner = 'Logic dispatcher library' }
        @{ Text = $integrationProject; Pattern = 'GateGrpcClients\.vcxproj'; Owner = 'Server Integration tests' }
        @{ Text = $chatGrpcTestProject; Pattern = 'ChatGrpcClients\.vcxproj'; Owner = 'Chat gRPC Integration tests' }
        @{ Text = $gateGrpcProject; Pattern = 'GrpcClientRuntime\.h'; Owner = 'Gate gRPC client library' }
        @{ Text = $chatGrpcProject; Pattern = 'GrpcClientRuntime\.h'; Owner = 'Chat gRPC client library' }
    )) {
        if ($registration.Text -notmatch $registration.Pattern) {
            throw "$($registration.Owner) must share the production gRPC client library and runtime Interface with its tests."
        }
    }
    foreach ($source in @(
        'GateServer\GateServer\VerifyGrpcClient.cpp'
        'GateServer\GateServer\StatusGrpcClient.cpp'
        'ChatServer\ChatServer\StatusGrpcClient.cpp'
        'ChatServer\ChatServer\ChatGrpcClient.cpp'
    )) {
        $grpcClientSource = Get-Content -LiteralPath (Join-Path $repoRoot $source) -Raw
        if ($grpcClientSource -notmatch 'rpc::InvokeUnary') {
            throw "$source must route every unary RPC through the finite-deadline production Interface."
        }
    }
    $grpcRuntime = Get-Content -LiteralPath (Join-Path $repoRoot 'common\grpc\GrpcClientRuntime.h') -Raw
    if ($grpcRuntime -notmatch 'context\.set_deadline') {
        throw 'The shared gRPC client runtime must set a finite ClientContext deadline.'
    }

    $clientTests = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'chat\tests') `
        -Recurse -File -Filter '*.cpp')
    $clientCMake = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\CMakeLists.txt') -Raw
    $normalizedClientCMake = $clientCMake.Replace('\', '/')
    foreach ($test in $clientTests) {
        $relative = (Get-RepositoryRelativePath $test.FullName).Substring('chat/'.Length)
        if ($normalizedClientCMake -notmatch [regex]::Escape($relative)) {
            throw "Unregistered Qt client test source: $(Get-RepositoryRelativePath $test.FullName)"
        }
    }
    Assert-ModuleReadme -TestFiles $clientTests -Toolchain 'Qt client'
    $ctestTargets = @([regex]::Matches(
        $clientCMake,
        'add_test\s*\(\s*NAME\s+([A-Za-z0-9_.-]+)',
        [Text.RegularExpressions.RegexOptions]::IgnoreCase
    ) | ForEach-Object { $_.Groups[1].Value } | Where-Object { $_ -notin @('http_transport.', 'tcp_transport.') })
    $expectedHttpTransportCases = @(
        'successPreservesRequestAndFlowIdentity'
        'refusedConnectionHasOneBoundedOutcome'
        'finiteDeadlineAbortsAnUnresponsivePeer'
        'malformedJsonIsRejectedWithoutLeakingItsBody'
        'maximumResponseIsAccepted'
        'oneByteOverMaximumIsRejected'
        'peerClosingMidResponseHasOneNetworkOutcome'
        'explicitCancelHasExactlyOneTerminalOutcome'
        'lateReplyFromAnOldFlowCannotCompleteTheNewFlow'
        'deletingTransportReleasesReplyAndLoopbackSocket'
    )
    $httpTransportBlock = [regex]::Match(
        $clientCMake,
        '(?ms)foreach\s*\(\s*HTTP_TRANSPORT_CASE\s+IN\s+ITEMS(?<cases>.*?)\)\s*add_test.*?set_tests_properties\s*\(\s*http_transport\.\$\{HTTP_TRANSPORT_CASE\}\s+PROPERTIES(?<properties>.*?)\)\s*endforeach'
    )
    $registeredHttpTransportCases = @(
        [regex]::Matches($httpTransportBlock.Groups['cases'].Value, '(?m)^\s*(?<case>[A-Za-z][A-Za-z0-9]+)\s*$') |
            ForEach-Object { $_.Groups['case'].Value }
    )
    if (-not $httpTransportBlock.Success -or
        ($registeredHttpTransportCases -join ',') -ne ($expectedHttpTransportCases -join ',') -or
        $httpTransportBlock.Groups['properties'].Value -notmatch 'LABELS\s+"?integration"?') {
        throw 'Qt HTTP transport must register exactly ten frozen Integration CTest cases through the production target.'
    }
    $expectedTcpTransportCases = @(
        'connectPreservesGenerationAndFlowIdentity'
        'sendWritesProductionFrame'
        'fragmentedFrameDecodedOnce'
        'coalescedFramesStayOrdered'
        'maximumFrameIsAccepted'
        'malformedOversizedFrameTerminates'
        'refusedConnectHasOneBoundedOutcome'
        'writeDeadlineAbortsSilentPeer'
        'peerCloseMidWriteHasOneTerminalOutcome'
        'resetDiscardsHalfFrame'
        'lateOldGenerationCannotCompleteRetry'
        'closeAndDeleteReleaseOwnedResources'
    )
    $tcpTransportBlock = [regex]::Match(
        $clientCMake,
        '(?ms)foreach\s*\(\s*TCP_TRANSPORT_CASE\s+IN\s+ITEMS(?<cases>.*?)\)\s*add_test.*?set_tests_properties\s*\(\s*tcp_transport\.\$\{TCP_TRANSPORT_CASE\}\s+PROPERTIES(?<properties>.*?)\)\s*endforeach'
    )
    $registeredTcpTransportCases = @(
        [regex]::Matches($tcpTransportBlock.Groups['cases'].Value, '(?m)^\s*(?<case>[A-Za-z][A-Za-z0-9]+)\s*$') |
            ForEach-Object { $_.Groups['case'].Value }
    )
    if (-not $tcpTransportBlock.Success -or
        ($registeredTcpTransportCases -join ',') -ne ($expectedTcpTransportCases -join ',') -or
        $tcpTransportBlock.Groups['properties'].Value -notmatch 'LABELS\s+"?integration"?') {
        throw 'Qt TCP transport must register exactly twelve frozen Integration CTest cases through the production target.'
    }
    $expectedClientLevels = @{
        'network_state_tests' = 'unit'
        'message_model.append_ack_status_removal' = 'unit'
        'message_model.unknown_ids' = 'unit'
        'message_model.history_order' = 'unit'
        'message_model.multiple_history_pages' = 'unit'
        'message_model.shifted_indexes' = 'unit'
        'message_model.text_and_chat_identity' = 'unit'
        'message_model.store_pagination_state' = 'component'
        'message_model.delegate_reflow' = 'component'
        'session_reset.account_state' = 'component'
        'session_reset.owned_ui_and_idempotence' = 'component'
        'session_reset.pending_batch' = 'component'
        'auth_flow.register_network_error' = 'unit'
        'auth_flow.reset_network_error' = 'unit'
        'auth_flow.login_network_error' = 'unit'
        'auth_flow.unknown_outcome' = 'unit'
        'auth_flow.malformed_json' = 'unit'
        'auth_flow.business_error' = 'unit'
        'auth_flow.login_http_success' = 'unit'
        'auth_flow.tcp_failure' = 'unit'
        'auth_flow.chat_login_failure' = 'unit'
        'auth_flow.chat_login_success' = 'unit'
        'auth_flow.duplicate_and_late' = 'unit'
        'auth_flow.abnormal_disconnect_reset' = 'component'
    }
    foreach ($target in $ctestTargets) {
        $properties = [regex]::Match(
            $clientCMake,
            "set_tests_properties\s*\(\s*$([regex]::Escape($target))\s+PROPERTIES(?<body>.*?)\)",
            [Text.RegularExpressions.RegexOptions]::IgnoreCase -bor
                [Text.RegularExpressions.RegexOptions]::Singleline
        )
        $label = [regex]::Match(
            $properties.Groups['body'].Value,
            'LABELS\s+"?(?<level>unit|component|integration|e2e)"?',
            [Text.RegularExpressions.RegexOptions]::IgnoreCase
        )
        if (-not $properties.Success -or -not $label.Success) {
            throw "CTest target '$target' must declare a Unit/Component/Integration/E2E label."
        }
        if ($expectedClientLevels.ContainsKey($target) -and
            $label.Groups['level'].Value -ne $expectedClientLevels[$target]) {
            throw "CTest target '$target' must be $($expectedClientLevels[$target]); found $($label.Groups['level'].Value)."
        }
    }
    foreach ($target in $expectedClientLevels.Keys) {
        if ($ctestTargets -notcontains $target) {
            throw "Required Qt CTest target is missing: $target"
        }
    }
    $expectedClientReports = @{
        unit = 'client_unit.xml'
        component = 'client_component.xml'
        integration = 'client_integration.xml'
    }
    if ($clientTestGroups.Count -ne $expectedClientReports.Count) {
        throw 'RunClientTests must define exactly the Unit, Component, and Integration report groups.'
    }
    foreach ($group in $clientTestGroups) {
        if (-not $expectedClientReports.ContainsKey($group.Level) -or
            (Split-Path -Leaf $group.Report) -ne $expectedClientReports[$group.Level]) {
            throw "Invalid Qt report mapping: $($group.Level) -> $($group.Report)"
        }
    }
    $expectedClientCounts = @{ unit = 18; component = 6; integration = 22 }
    foreach ($group in $clientTestGroups) {
        if ($group.ExpectedCount -ne $expectedClientCounts[$group.Level]) {
            throw "Qt $($group.Level) report must require exactly $($expectedClientCounts[$group.Level]) testcases."
        }
    }

    foreach ($guard in @(
        @{ Pattern = 'add_library\s*\(\s*chat_session_core'; Message = 'Qt session production sources must be owned by chat_session_core.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*chat[\s\S]*?chat_session_core'; Message = 'The Qt executable must link the production session Module.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*session_reset_tests[\s\S]*?chat_session_core'; Message = 'Session tests must link the same production session Module.' }
        @{ Pattern = 'add_library\s*\(\s*chat_auth_flow'; Message = 'Qt auth production sources must be owned by chat_auth_flow.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*chat[\s\S]*?chat_auth_flow'; Message = 'The Qt executable must link the production auth-flow Module.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*auth_flow_tests[\s\S]*?chat_auth_flow'; Message = 'Auth Unit tests must link the same production auth-flow Module.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*auth_flow_component_tests[\s\S]*?chat_auth_flow[\s\S]*?chat_session_core'; Message = 'Auth Component tests must link auth flow and the existing session Module.' }
        @{ Pattern = 'add_library\s*\(\s*chat_gate_http_transport[\s\S]*?gatehttptransport\.cpp'; Message = 'Qt HTTP production sources must be owned by chat_gate_http_transport.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*chat[\s\S]*?chat_gate_http_transport'; Message = 'The Qt executable must link the production Gate HTTP transport Module.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*http_transport_tests[\s\S]*?chat_gate_http_transport'; Message = 'Qt HTTP Integration tests must link the same production transport Module.' }
        @{ Pattern = 'add_library\s*\(\s*chat_tcp_transport[\s\S]*?chattcptransport\.cpp'; Message = 'Qt TCP production sources must be owned by chat_tcp_transport.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*chat[\s\S]*?chat_tcp_transport'; Message = 'The Qt executable must link the production Chat TCP transport Module.' }
        @{ Pattern = 'target_link_libraries\s*\(\s*tcp_transport_tests[\s\S]*?chat_tcp_transport'; Message = 'Qt TCP Integration tests must link the same production transport Module.' }
    )) {
        if ($clientCMake -notmatch $guard.Pattern) {
            throw $guard.Message
        }
    }
    $decoderSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\tcpframedecoder.cpp') -Raw
    $tcpTransportSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\chattcptransport.cpp') -Raw
    $tcpMgrHeader = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\tcpmgr.h') -Raw
    $tcpMgrSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\tcpmgr.cpp') -Raw
    $clientSessionSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\clientsession.cpp') -Raw
    $mainWindowSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\mainwindow.cpp') -Raw
    if ($decoderSource -notmatch 'void\s+TcpFrameDecoder::reset\s*\(' -or
        $tcpTransportSource -notmatch '(?m)^\s*decoder\.reset\s*\(' -or
        $tcpMgrSource -notmatch '_transport\.reset\s*\(' -or
        $tcpMgrSource -notmatch '_pendingTextBatches\.clear\s*\(' -or
        $tcpMgrSource -notmatch 'slot_tcp_connect[\s\S]*?resetConnection\s*\(') {
        throw 'TcpMgr connection reset must clear decoder/pending state and run before reconnect.'
    }
    if ($tcpMgrHeader -match 'QTcpSocket|TcpFrameDecoder') {
        throw 'TcpMgr must not expose transport socket or decoder ownership.'
    }
    if ($clientSessionSource -notmatch 'UserMgr::GetInstance\(\)->resetSession\s*\(' -or
        $clientSessionSource -notmatch 'TcpMgr::GetInstance\(\)->resetConnection\s*\(' -or
        $clientSessionSource -notmatch 'delete\s+ownedSessionRoot') {
        throw 'ClientSession reset must clear real user/connection state and destroy the owned session UI.'
    }
    if ($mainWindowSource -notmatch '_session\.beginSession\s*\(\s*_chat_dlg\s*\)' -or
        $mainWindowSource -notmatch 'bool\s+MainWindow::resetSession[\s\S]*?_session\.resetSession\s*\(\s*reason\s*\)' -or
        $mainWindowSource -notmatch 'resetSession\s*\(\s*SessionResetReason::Kicked\s*\)' -or
        $mainWindowSource -notmatch 'resetSession\s*\(\s*SessionResetReason::UnexpectedDisconnect\s*\)' -or
        $mainWindowSource -notmatch 'if\s*\(\s*expectedClose') {
        throw 'MainWindow must route authenticated UI, kicked, expected-close, and abnormal-close paths through ClientSession.'
    }
    $authFlowSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\authflowcoordinator.cpp') -Raw
    $loginDialogSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\logindialog.cpp') -Raw
    $httpMgrSource = Get-Content -LiteralPath (Join-Path $repoRoot 'chat\httpmgr.cpp') -Raw
    if ($authFlowSource -notmatch 'AuthFlowCoordinator::Reduce' -or
        $loginDialogSource -notmatch '_authFlow\.Reduce' -or
        $httpMgrSource -notmatch 'sig_http_finish\s*\(\s*static_cast<AuthFlowId>\s*\(\s*result\.flowId\s*\)' -or
        $mainWindowSource -notmatch 'AbnormalDisconnect[\s\S]*?AuthActionKind::ShowLogin[\s\S]*?_session\.resetSession\s*\(\s*reason\s*\)') {
        throw 'Qt auth outcomes and abnormal disconnect must route through AuthFlowCoordinator and existing ClientSession reset.'
    }
    if ($clientCMake -match 'clearForTest|SESSION_TEST|TEST_SESSION') {
        throw 'Qt session reset must not use a test-only switch or clearForTest Interface.'
    }

    $varifyTests = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'VarifyServer\test') `
        -Recurse -File -Filter '*.test.js')
    Assert-ModuleReadme -TestFiles $varifyTests -Toolchain 'VarifyServer'
    $varifyPackage = Get-Content -LiteralPath (Join-Path $repoRoot 'VarifyServer\package.json') `
        -Raw | ConvertFrom-Json
    $runnerRegistration = Get-Content -LiteralPath $PSCommandPath -Raw
    $normalizedRunnerRegistration = $runnerRegistration.Replace('\', '/')
    $lastExitInitialization = $runnerRegistration.IndexOf('$global:LASTEXITCODE = 0')
    $firstFunction = $runnerRegistration.IndexOf('function ')
    if ($lastExitInitialization -lt 0 -or
        $firstFunction -lt 0 -or
        $lastExitInitialization -gt $firstFunction) {
        throw 'The public runner must initialize LASTEXITCODE before StrictMode functions read it on a fresh CI process.'
    }
    $runServerTests = [regex]::Match(
        $runnerRegistration,
        '(?ms)^function\s+Run-ServerTests\s*\{(?<body>.*?)(?=^function\s+|\z)'
    )
    if (-not $runServerTests.Success -or
        $runServerTests.Groups['body'].Value -notmatch "(?m)^\s*Invoke-ProtocolCompatibility\s+'check'\s*$") {
        throw "RunServerTests must execute Invoke-ProtocolCompatibility 'check' so generated and descriptor drift block develop."
    }
    foreach ($requiredProductionTarget in @('GateServer', 'StatusServer')) {
        if ($runServerTests.Groups['body'].Value -notmatch [regex]::Escape($requiredProductionTarget)) {
            throw "RunServerTests must build and deploy $requiredProductionTarget for its process Integration contracts."
        }
    }
    foreach ($requiredCount in @(68, 56, 93, 4)) {
        if ($runServerTests.Groups['body'].Value -notmatch "ExpectedCount\s*=\s*$requiredCount") {
            throw "RunServerTests is missing the exact current Server testcase count $requiredCount."
        }
    }
    foreach ($requiredProperty in @('VcpkgManifestInstall=false', 'VcpkgInstalledDir=')) {
        if ($runServerTests.Groups['body'].Value -notmatch [regex]::Escape($requiredProperty)) {
            throw "RunServerTests must enforce DG-25 property $requiredProperty on its solution build."
        }
    }
    if ($runnerRegistration -notmatch [regex]::Escape('$fixedVcpkgInstalledRoot = ''D:\git\Chat\vcpkg_installed''') -or
        $runnerRegistration -notmatch '\$VcpkgInstalledRoot\s*=\s*\$fixedVcpkgInstalledRoot' -or
        $runServerTests.Groups['body'].Value -notmatch 'VcpkgInstalledRoot' -or
        $runnerRegistration -notmatch 'CHAT_VCPKG_INSTALLED_ROOT\s*=\s*\$VcpkgInstalledRoot') {
        throw 'Server tasks must default to the exact DG-25 fixed installed tree while permitting an explicit CI-owned root.'
    }
    if ($runServerTests.Groups['body'].Value -notmatch 'ChatGrpcClientTests' -or
        $runServerTests.Groups['body'].Value -notmatch 'server_chat_grpc_integration\.xml') {
        throw 'RunServerTests must build and report the Chat gRPC client Integration executable.'
    }
    $runAllTests = [regex]::Match(
        $runnerRegistration,
        '(?ms)^function\s+Run-AllTests\s*\{(?<body>.*?)(?=^function\s+|\z)'
    )
    if (-not $runAllTests.Success) {
        throw 'RunAllTests is missing from the public runner.'
    }
    foreach ($requiredRunner in @('Run-ScriptTests', 'Run-ServerTests', 'Run-ClientTests', 'Run-VarifyTests')) {
        if ($runAllTests.Groups['body'].Value -notmatch "(?m)^\s*$([regex]::Escape($requiredRunner))\s*$") {
            throw "RunAllTests must execute $requiredRunner so the aggregate lane cannot omit a toolchain."
        }
    }
    if ($runAllTests.Groups['body'].Value -notmatch '(?m)^\s*Confirm-RegressionReports\s*$') {
        throw 'RunAllTests must audit the exact thirteen-report/313-testcase baseline.'
    }
    if ($regressionReportGroups.Count -ne 13 -or
        ($regressionReportGroups | Measure-Object -Property ExpectedCount -Sum).Sum -ne 313) {
        throw 'The registered regression baseline must remain exactly 13 reports and 313 testcases.'
    }
    if ($legacyRegressionReportGroups.Count -ne 12 -or
        ($legacyRegressionReportGroups | Measure-Object -Property MinimumCount -Sum).Sum -ne 232) {
        throw 'The original regression floor must remain exactly 12 reports and 232 testcases.'
    }
    foreach ($residuePrefix in @(
        'chat-config-test-', 'chat-startup-tests-', 'gate-status-startup-tests-',
        'chat-process-harness-', 'chat-instance-validation-', 'chat-instance-lifecycle-',
        'varify-config-', 'varify-startup-', 'chat-proto-mutation-'
    )) {
        if ($runnerRegistration -notmatch [regex]::Escape("'$residuePrefix'")) {
            throw "The aggregate cleanup gate is missing the owned residue prefix $residuePrefix."
        }
    }
    if ($runAllTests.Groups['body'].Value -notmatch '(?m)^\s*Assert-NoNewRegressionResidue\b' -or
        $runnerRegistration -notmatch '(?ms)^function\s+Assert-RegressionCleanupEvidence\b.*?^function\s+Confirm-RegressionReports\b.*?^\s*Assert-RegressionCleanupEvidence\s*$' -or
        $runnerRegistration -notmatch '(?ms)^function\s+Assert-RegressionReport\b.*?credential-shaped assignment') {
        throw 'RunAllTests must enforce aggregate residue, cleanup-evidence, and secret/report-integrity gates.'
    }
    $addedDiff = (& git -C $repoRoot diff --unified=0 --no-ext-diff 2>$null | Where-Object { $_ -match '^\+(?!\+\+)' }) -join "`n"
    if ($LASTEXITCODE -ne 0) {
        throw 'Unable to scan the working diff for credential-shaped assignments.'
    }
    if ($addedDiff -match '(?i)(?:password|passwd|secret|token|verification[-_ ]?code|email)\s*[:=]\s*[^\s<]{3,}') {
        throw 'The working diff contains a credential-shaped assignment.'
    }

    $workflowPath = Require-File (Join-Path $repoRoot '.github\workflows\windows-ci.yml') `
        'The develop Windows CI workflow is missing.'
    $workflow = Get-Content -LiteralPath $workflowPath -Raw
    foreach ($trigger in @('push', 'pull_request')) {
        $triggerBlock = [regex]::Match(
            $workflow,
            "(?ms)^  $([regex]::Escape($trigger)):\s*\r?\n(?<body>(?:^    .*?(?:\r?\n|\z))*)"
        )
        if (-not $triggerBlock.Success -or
            $triggerBlock.Groups['body'].Value -notmatch '(?m)^\s+-\s+develop\s*$') {
            throw "Windows CI must run for $trigger events targeting develop."
        }
    }
    foreach ($requiredTask in @('CheckTestStructure', 'RunScriptTests', 'RunServerTests', 'RunClientTests', 'RunVarifyTests')) {
        if ($workflow -notmatch "(?m)-Task\s+$([regex]::Escape($requiredTask))(?:\s|$)") {
            throw "Windows CI must invoke the public $requiredTask entry so develop cannot use a second test path."
        }
    }
    if ($workflow -match '(?m)^\s*continue-on-error\s*:') {
        throw 'Windows CI must not weaken a required test or package step with continue-on-error.'
    }
    $serverJob = [regex]::Match(
        $workflow,
        '(?ms)^  servers-release:\s*$(?<body>.*?)(?=^  [A-Za-z0-9_-]+:\s*$|\z)'
    )
    if (-not $serverJob.Success -or
        $serverJob.Groups['body'].Value -notmatch 'actions/setup-node@' -or
        $serverJob.Groups['body'].Value -notmatch '(?m)^\s*run:\s+npm ci --ignore-scripts\s*$') {
        throw 'The Server CI job must restore locked VarifyServer Node dependencies for the C++ to Node loopback contract.'
    }
    $ciInstalledRootPattern = '-VcpkgInstalledRoot\s+\(Join-Path\s+\$env:GITHUB_WORKSPACE\s+''\.ci\\vcpkg_installed''\)'
    foreach ($serverTask in @('RestoreServers', 'BuildServers', 'RunServerTests')) {
        $serverTaskPattern = "(?ms)-Task\s+$serverTask\s+``(?:(?!-Task\s+).)*?$ciInstalledRootPattern"
        if ($serverJob.Groups['body'].Value -notmatch $serverTaskPattern) {
            throw "Server CI task $serverTask must pass the job-owned .ci\\vcpkg_installed root explicitly."
        }
    }
    foreach ($upload in @(
        @{ Artifact = 'windows-script-test-results'; Path = 'build/test-results/script_\*\.xml' }
        @{ Artifact = 'windows-server-test-results'; Path = 'build/test-results/server_\*\.xml' }
        @{ Artifact = 'windows-client-test-results'; Path = 'build/test-results/client_\*\.xml' }
        @{ Artifact = 'windows-varify-test-results'; Path = 'build/test-results/varify_\*\.xml' }
    )) {
        $uploadPattern = "(?ms)-\s+name:\s+Upload[^\r\n]*test reports\s*\r?\n\s+if:\s+always\(\).*?name:\s+$([regex]::Escape($upload.Artifact)).*?path:\s+$($upload.Path).*?if-no-files-found:\s+error"
        if ($workflow -notmatch $uploadPattern) {
            throw "Windows CI must always upload $($upload.Artifact) and fail when its required reports are missing."
        }
    }
    $npmRegisteredFiles = @(
        @($varifyPackage.scripts.'test:unit', $varifyPackage.scripts.'test:integration') |
            ForEach-Object {
                [regex]::Matches([string]$_, 'test/[A-Za-z0-9_./-]+\.test\.js') |
                    ForEach-Object { $_.Value }
            }
    )
    $runnerRegisteredFiles = @(
        [regex]::Matches($normalizedRunnerRegistration, 'test/[A-Za-z0-9_./-]+\.test\.js') |
            ForEach-Object { $_.Value } | Sort-Object -Unique
    )
    foreach ($test in $varifyTests) {
        $relative = (Get-RepositoryRelativePath $test.FullName).Substring('VarifyServer/'.Length)
        if (@($npmRegisteredFiles | Where-Object { $_ -eq $relative }).Count -ne 1) {
            throw "VarifyServer test is missing from an npm level script: $relative"
        }
        if ($runnerRegisteredFiles -notcontains $relative) {
            throw "VarifyServer test is missing from RunVarifyTests: $relative"
        }
    }
    if ($npmRegisteredFiles.Count -ne $varifyTests.Count -or
        $runnerRegisteredFiles.Count -ne $varifyTests.Count) {
        throw 'VarifyServer npm and RunVarifyTests file lists must exactly match discovered test files.'
    }

    $scriptTests = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot 'tests\scripts') `
        -Recurse -File -Filter '*.tests.ps1')
    Assert-ModuleReadme -TestFiles $scriptTests -Toolchain 'PowerShell'
    foreach ($test in $scriptTests) {
        $relative = Get-RepositoryRelativePath $test.FullName
        if ($normalizedRunnerRegistration -notmatch [regex]::Escape($relative)) {
            throw "PowerShell test is missing from RunScriptTests: $relative"
        }
    }
    $expectedScriptReports = @{ component = 'script_component.xml'; integration = 'script_integration.xml' }
    if ($scriptTestGroups.Count -ne $scriptTests.Count) {
        throw 'RunScriptTests must register every PowerShell test source exactly once.'
    }
    foreach ($group in $scriptTestGroups) {
        if (-not $expectedScriptReports.ContainsKey($group.Level) -or
            (Split-Path -Leaf $group.Report) -ne $expectedScriptReports[$group.Level]) {
            throw "Invalid PowerShell report mapping: $($group.Level) -> $($group.Report)"
        }
        $scriptPath = Require-File (Join-Path $repoRoot $group.Script) 'A registered PowerShell test is missing.'
        $scriptText = Get-Content -LiteralPath $scriptPath -Raw
        $ids = @(
            [regex]::Matches(
                $scriptText,
                "Invoke-(?:ExpectedValidationFailure|TestCase)\s+'(?<id>A02-(?:VAL|LIFE)-\d{2})'"
            ) | ForEach-Object { $_.Groups['id'].Value }
        )
        if ($ids.Count -ne $group.ExpectedCount -or @($ids | Sort-Object -Unique).Count -ne $ids.Count) {
            throw "PowerShell test registration count mismatch for $($group.Script): expected $($group.ExpectedCount), found $($ids.Count)."
        }
        foreach ($id in $ids) {
            if ($id -notmatch $group.TestIdPattern) {
                throw "PowerShell Test ID '$id' is registered in the wrong report group."
            }
        }
    }

    Write-Host "Test registration and report grouping verified: $($serverTests.Count) Server, $($clientTests.Count) Qt, $($varifyTests.Count) VarifyServer, $($scriptTests.Count) PowerShell source files."
}

function Run-ScriptTests {
    Confirm-TestStructure
    $residuePrefixes = @('chat-instance-validation-', 'chat-instance-lifecycle-')
    $residueBefore = @(Get-RegressionResidueSnapshot -Prefixes $residuePrefixes)
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $windowsPowerShell = Require-File $windowsPowerShell 'Windows PowerShell 5.1 is required for script tests.'
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $failedScripts = @()
    foreach ($group in $scriptTestGroups) {
        if (Test-Path -LiteralPath $group.Report) {
            Remove-Item -LiteralPath $group.Report -Force
        }
        $testScript = Require-File (Join-Path $repoRoot $group.Script) 'A ChatServer instance script test is missing.'
        $output = (& $windowsPowerShell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass `
            -File $testScript -JUnitPath $group.Report 2>&1 | Out-String)
        $testExitCode = $LASTEXITCODE
        Write-Host $output
        if (-not (Test-Path -LiteralPath $group.Report -PathType Leaf)) {
            throw "PowerShell $($group.Level) test report was not created: $($group.Report)"
        }
        [void](Assert-RegressionReport -Path $group.Report -ExpectedCount $group.ExpectedCount)
        $reportXml = [xml](Get-Content -LiteralPath $group.Report -Raw)
        $testcases = @($reportXml.SelectNodes('//testcase'))
        foreach ($testcase in $testcases) {
            if ($testcase.name -notmatch $group.TestIdPattern) {
                throw "PowerShell $($group.Level) report contains an unexpected Test ID: $($testcase.name)"
            }
        }
        if ($testExitCode -ne 0) {
            $failedScripts += "$($group.Script) (exit $testExitCode)"
        }
    }
    if ($failedScripts.Count -gt 0) {
        throw "ChatServer instance script tests failed: $($failedScripts -join '; ')"
    }
    Assert-NoNewRegressionResidue -Before $residueBefore -Prefixes $residuePrefixes -Lane 'PowerShell'
}

function Run-VarifyTests {
    Confirm-TestStructure
    $residuePrefixes = @('varify-config-', 'varify-startup-', 'chat-proto-mutation-')
    $residueBefore = @(Get-RegressionResidueSnapshot -Prefixes $residuePrefixes)
    $node = Require-Command 'node.exe' 'Install Node.js before running VarifyServer tests.'
    [void](Require-File (Join-Path $varifySource 'node_modules\@grpc\grpc-js\package.json') `
        'Restore VarifyServer dependencies with RestoreVarify or npm ci first.')
    $groups = @(
        @{
            Level = 'unit'
            Report = (Join-Path $testResults 'varify_unit.xml')
            ExpectedCount = 18
            Files = @(
                Require-File (Join-Path $varifySource 'test\protocol\protocol.test.js') `
                    'The VarifyServer protocol unit tests are missing.'
                Require-File (Join-Path $varifySource 'test\handler\handler.test.js') `
                    'The VarifyServer handler unit tests are missing.'
                Require-File (Join-Path $varifySource 'test\startup\startup-unit.test.js') `
                    'The VarifyServer startup unit tests are missing.'
            )
        }
        @{
            Level = 'integration'
            Report = (Join-Path $testResults 'varify_integration.xml')
            ExpectedCount = 11
            Files = @(
                Require-File (Join-Path $varifySource 'test\config\config.test.js') `
                    'The VarifyServer configuration process tests are missing.'
                Require-File (Join-Path $varifySource 'test\rpc\rpc-routing.test.js') `
                    'The VarifyServer RPC routing integration tests are missing.'
                Require-File (Join-Path $varifySource 'test\startup\startup-integration.test.js') `
                    'The VarifyServer startup integration tests are missing.'
            )
        }
    )
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $failures = @()
    Push-Location $varifySource
    try {
        foreach ($group in $groups) {
            if (Test-Path -LiteralPath $group.Report) {
                Remove-Item -LiteralPath $group.Report -Force
            }
            & $node --test --test-reporter=junit `
                "--test-reporter-destination=$($group.Report)" @($group.Files)
            $testExitCode = $LASTEXITCODE
            if (-not (Test-Path -LiteralPath $group.Report -PathType Leaf)) {
                throw "VarifyServer $($group.Level) test report was not created: $($group.Report)"
            }
            [void](Assert-RegressionReport -Path $group.Report -ExpectedCount $group.ExpectedCount)
            if ($testExitCode -ne 0) {
                $failures += "$($group.Level) (exit $testExitCode)"
            }
        }
    } finally {
        Pop-Location
    }
    if ($failures.Count -gt 0) {
        throw "VarifyServer tests failed: $($failures -join '; ')"
    }
    Assert-NoNewRegressionResidue -Before $residueBefore -Prefixes $residuePrefixes -Lane 'VarifyServer'
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

function Run-AllTests {
    $residueBefore = @(Get-RegressionResidueSnapshot -Prefixes $regressionResiduePrefixes)
    Run-ScriptTests
    Run-ServerTests
    Run-ClientTests
    Run-VarifyTests
    Confirm-RegressionReports
    Assert-NoNewRegressionResidue -Before $residueBefore -Prefixes $regressionResiduePrefixes -Lane 'Aggregate'
}

function Check-Toolchains {
    $vcpkg = Resolve-Vcpkg
    $msbuild = Resolve-MSBuild
    $cmake = Resolve-CMake
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
    'CheckTestStructure' { Confirm-TestStructure }
    'CheckTestReports' { Confirm-RegressionReports }
    'GenerateProtocols' { Invoke-ProtocolCompatibility 'generate' }
    'CheckProtocols' { Invoke-ProtocolCompatibility 'check' }
    'RestoreServers' { Restore-Servers }
    'BuildServers' { Build-Servers }
    'BuildClient' { Build-Client }
    'RestoreVarify' { Restore-Varify }
    'RunServerTests' { Run-ServerTests }
    'RunClientTests' { Run-ClientTests }
    'RunScriptTests' { Run-ScriptTests }
    'RunVarifyTests' { Run-VarifyTests }
    'RunAllTests' { Run-AllTests }
    'TestPhase1' {
        Write-Warning 'TestPhase1 is a compatibility alias; use RunAllTests.'
        Run-AllTests
    }
    'BuildAll' {
        Check-Toolchains
        Restore-Servers
        Build-Servers
        Build-Client
        Restore-Varify
    }
}
