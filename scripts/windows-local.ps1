[CmdletBinding()]
param(
    [ValidateSet('Check', 'RestoreServers', 'BuildServers', 'BuildClient', 'RestoreVarify', 'RunServerTests', 'RunClientTests', 'RunScriptTests', 'RunVarifyTests', 'TestPhase1', 'BuildAll')]
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
$chatServerExecutable = Join-Path $repoRoot "build\windows-servers\$Configuration\ChatServer\ChatServer.exe"
$serverTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\server_unit_tests.exe"
$gateAsioTestProject = Join-Path $repoRoot 'tests\server\lifecycle\GateAsioPoolTests.vcxproj'
$statusAsioTestProject = Join-Path $repoRoot 'tests\server\lifecycle\StatusAsioPoolTests.vcxproj'
$gateAsioTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\gate_asio_pool_tests.exe"
$statusAsioTestExecutable = Join-Path $repoRoot "build\windows-tests\$Configuration\status_asio_pool_tests.exe"
$testResults = Join-Path $repoRoot 'build\test-results'
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

function Resolve-CMake {
    $cmake = Require-Command 'cmake.exe' 'Install CMake 3.21 or newer and add it to PATH.'
    $versionText = (& $cmake --version 2>&1 | Select-Object -First 1)
    $versionMatch = [regex]::Match([string]$versionText, '(\d+\.\d+(?:\.\d+)?)')
    if ($LASTEXITCODE -ne 0 -or -not $versionMatch.Success) {
        throw "Unable to determine the CMake version: $versionText"
    }
    $cmakeVersion = $versionMatch.Groups[1].Value
    if ([version]$cmakeVersion -lt [version]'3.21') {
        throw "CMake 3.21 or newer is required; found $cmakeVersion."
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

function Run-ServerTests {
    $vcpkg = Resolve-Vcpkg
    $msbuild = Resolve-MSBuild
    $arguments = @(
        $solution
        '/m'
        '/t:ChatServer;ServerUnitTests'
        "/p:Configuration=$Configuration"
        '/p:Platform=x64'
        "/p:VcpkgRoot=$($vcpkg.Root)"
        "/p:VcpkgTriplet=$ServerTriplet"
        "/p:VcpkgHostTriplet=$ServerHostTriplet"
        "/p:ServerIntermediateRoot=$ServerIntermediateRoot"
    )
    $reports = @(
        (Join-Path $testResults 'server_unit.xml')
        (Join-Path $testResults 'server_gate_asio.xml')
        (Join-Path $testResults 'server_status_asio.xml')
    )
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    foreach ($report in $reports) {
        if (Test-Path -LiteralPath $report) {
            Remove-Item -LiteralPath $report -Force
        }
    }

    & $msbuild @arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Server unit test build failed with exit code $LASTEXITCODE."
    }

    $installedRoot = Join-Path $repoRoot 'vcpkg_installed'
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

    $installedBin = Join-Path $repoRoot "vcpkg_installed\$ServerTriplet\bin"
    if ($Configuration -eq 'Debug') {
        $installedBin = Join-Path $repoRoot "vcpkg_installed\$ServerTriplet\debug\bin"
    }
    if (-not (Test-Path -LiteralPath $installedBin -PathType Container)) {
        throw "The vcpkg app-local dependency directory is missing: $installedBin"
    }
    $chatBinary = Require-File $chatServerExecutable 'Build the ChatServer target first.'
    & $vcpkg.Exe z-applocal "--target-binary=$chatBinary" "--installed-bin-dir=$installedBin"
    if ($LASTEXITCODE -ne 0) {
        throw "ChatServer app-local deployment failed with exit code $LASTEXITCODE."
    }

    $testBinary = Require-File $serverTestExecutable 'Build the ServerUnitTests target first.'

    $executions = @(
        @{ Binary = $testBinary; Report = $reports[0] }
        @{ Binary = (Require-File $gateAsioTestExecutable 'Build the Gate Asio lifecycle test target first.'); Report = $reports[1] }
        @{ Binary = (Require-File $statusAsioTestExecutable 'Build the Status Asio lifecycle test target first.'); Report = $reports[2] }
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
        if ($exitCode -ne 0) {
            $failures += "$($execution.Binary) (exit $exitCode)"
        }
    }
    if ($failures.Count -gt 0) {
        throw "Server tests failed: $($failures -join '; ')"
    }
}

function Build-Client {
    $cmake = Resolve-CMake
    $qt = Resolve-QtToolchain
    Write-Host "Qt root: $($qt.Root)"
    Write-Host "Qt C++ compiler: $($qt.CxxCompiler)"
    Write-Host "Ninja: $($qt.Ninja)"
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

function Run-ClientTests {
    Build-Client
    $ctest = Require-Command 'ctest.exe' 'Install CMake 3.21 or newer and add it to PATH.'
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $report = Join-Path $testResults 'client_unit.xml'
    if (Test-Path -LiteralPath $report) {
        Remove-Item -LiteralPath $report -Force
    }
    & $ctest --test-dir $clientBuild --output-on-failure --output-junit $report
    $testExitCode = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "Qt client test report was not created: $report"
    }
    if ($testExitCode -ne 0) {
        throw "Qt client tests failed with exit code $testExitCode. Report: $report"
    }
}

function Run-ScriptTests {
    $windowsPowerShell = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $windowsPowerShell = Require-File $windowsPowerShell 'Windows PowerShell 5.1 is required for script tests.'
    $testScripts = @(
        'tests\scripts\validation\chatserver-instances.tests.ps1'
        'tests\scripts\lifecycle\chatserver-instances.tests.ps1'
    )
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $report = Join-Path $testResults 'script_unit.xml'
    if (Test-Path -LiteralPath $report) {
        Remove-Item -LiteralPath $report -Force
    }
    $results = @()
    foreach ($relativePath in $testScripts) {
        $testScript = Require-File (Join-Path $repoRoot $relativePath) 'A ChatServer instance script test is missing.'
        $started = [DateTime]::UtcNow
        $output = (& $windowsPowerShell -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File $testScript 2>&1 | Out-String)
        $results += [pscustomobject]@{
            Name = $relativePath
            ExitCode = $LASTEXITCODE
            Duration = ([DateTime]::UtcNow - $started).TotalSeconds
            Output = $output
        }
        Write-Host $output
    }
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($report, $settings)
    try {
        $writer.WriteStartDocument()
        $writer.WriteStartElement('testsuites')
        $writer.WriteAttributeString('tests', [string]$results.Count)
        $writer.WriteAttributeString('failures', [string]@($results | Where-Object { $_.ExitCode -ne 0 }).Count)
        $writer.WriteStartElement('testsuite')
        $writer.WriteAttributeString('name', 'PowerShellScriptTests')
        $writer.WriteAttributeString('tests', [string]$results.Count)
        $writer.WriteAttributeString('failures', [string]@($results | Where-Object { $_.ExitCode -ne 0 }).Count)
        foreach ($result in $results) {
            $writer.WriteStartElement('testcase')
            $writer.WriteAttributeString('classname', 'scripts.chatserver-instances')
            $writer.WriteAttributeString('name', $result.Name)
            $writer.WriteAttributeString('time', $result.Duration.ToString('0.000', [Globalization.CultureInfo]::InvariantCulture))
            if ($result.ExitCode -ne 0) {
                $writer.WriteStartElement('failure')
                $writer.WriteAttributeString('message', "exit code $($result.ExitCode)")
                $writer.WriteString($result.Output)
                $writer.WriteEndElement()
            }
            $writer.WriteStartElement('system-out')
            $writer.WriteString($result.Output)
            $writer.WriteEndElement()
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement()
        $writer.WriteEndElement()
        $writer.WriteEndDocument()
    } finally {
        $writer.Dispose()
    }
    $failedScripts = @($results | Where-Object { $_.ExitCode -ne 0 })
    if ($failedScripts.Count -gt 0) {
        throw "ChatServer instance script tests failed. Report: $report"
    }
}

function Run-VarifyTests {
    $node = Require-Command 'node.exe' 'Install Node.js before running VarifyServer tests.'
    [void](Require-File (Join-Path $varifySource 'node_modules\@grpc\grpc-js\package.json') `
        'Restore VarifyServer dependencies with RestoreVarify or npm ci first.')
    $testFiles = @(
        Require-File (Join-Path $varifySource 'test\config\config.test.js') `
            'The VarifyServer configuration unit tests are missing.'
        Require-File (Join-Path $varifySource 'test\protocol\protocol.test.js') `
            'The VarifyServer protocol unit tests are missing.'
        Require-File (Join-Path $varifySource 'test\handler\handler.test.js') `
            'The VarifyServer handler unit tests are missing.'
        Require-File (Join-Path $varifySource 'test\rpc\rpc-routing.test.js') `
            'The VarifyServer RPC routing component tests are missing.'
        Require-File (Join-Path $varifySource 'test\startup\startup.test.js') `
            'The VarifyServer startup lifecycle tests are missing.'
    )
    [void](New-Item -ItemType Directory -Path $testResults -Force)
    $report = Join-Path $testResults 'varify_unit.xml'
    if (Test-Path -LiteralPath $report) {
        Remove-Item -LiteralPath $report -Force
    }
    Push-Location $varifySource
    try {
        & $node --test --test-reporter=junit --test-reporter-destination=$report @testFiles
        if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
            throw "VarifyServer unit test report was not created: $report"
        }
        if ($LASTEXITCODE -ne 0) {
            throw "VarifyServer unit tests failed with exit code $LASTEXITCODE. Report: $report"
        }
    } finally {
        Pop-Location
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
    'RestoreServers' { Restore-Servers }
    'BuildServers' { Build-Servers }
    'BuildClient' { Build-Client }
    'RestoreVarify' { Restore-Varify }
    'RunServerTests' { Run-ServerTests }
    'RunClientTests' { Run-ClientTests }
    'RunScriptTests' { Run-ScriptTests }
    'RunVarifyTests' { Run-VarifyTests }
    'TestPhase1' {
        Run-ScriptTests
        Run-ServerTests
        Run-ClientTests
        Run-VarifyTests
    }
    'BuildAll' {
        Check-Toolchains
        Restore-Servers
        Build-Servers
        Build-Client
        Restore-Varify
    }
}
