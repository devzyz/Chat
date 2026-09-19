[CmdletBinding()]
param(
    [ValidateSet('Build', 'Test')][string]$Task = 'Build',
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [string]$InstalledDir,
    [string]$QtRoot = $env:QT_ROOT,
    [string]$MinGwRoot = $env:MINGW_ROOT
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $InstalledDir) { $InstalledDir = Join-Path $repo 'vcpkg_installed' }
$InstalledDir = (Resolve-Path -LiteralPath $InstalledDir).Path + '\'
if (-not $VcpkgRoot -or -not (Test-Path -LiteralPath (Join-Path $VcpkgRoot 'scripts/buildsystems/msbuild/vcpkg.targets'))) {
    throw 'Pass -VcpkgRoot for the existing vcpkg checkout; this command does not restore dependencies.'
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$visualStudio = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
$msbuild = Join-Path $visualStudio 'MSBuild/Current/Bin/MSBuild.exe'
$output = Join-Path $repo 'build/resource'
New-Item -ItemType Directory -Force -Path $output | Out-Null
function Build-ResourceProject([string]$Project) {
    $name = [IO.Path]::GetFileNameWithoutExtension($Project)
    & $msbuild (Join-Path $repo $Project) /p:Configuration=Release /p:Platform=x64 `
        "/p:VcpkgRoot=$VcpkgRoot" "/p:VcpkgInstalledDir=$InstalledDir" `
        /p:VcpkgManifestInstall=false /p:PreferredToolArchitecture=x64 /nologo /v:minimal `
        *> (Join-Path $output "$name-build.log")
    if ($LASTEXITCODE -ne 0) { throw "$name build failed; see build/resource/$name-build.log" }
}
Push-Location $repo
try {
    Build-ResourceProject 'ResourceServer/ResourceServer/ResourceServer/ResourceServer.vcxproj'
    Build-ResourceProject 'ChatServer/ChatServer/ChatServer.vcxproj'
    $config = Join-Path $repo 'build/windows-servers/Release/ResourceServer/config.example.ini'
    Copy-Item -LiteralPath 'ResourceServer/ResourceServer/ResourceServer/config.ini' -Destination $config
    if ($Task -eq 'Build') { return }
    Build-ResourceProject 'tests/server/resource/ResourceTests.vcxproj'
    & (Join-Path $output 'ResourceTests.exe') '--gtest_filter=StoreTest.*' "--gtest_output=xml:$output/resource-unit.xml"
    if ($LASTEXITCODE -ne 0) { throw 'Resource unit tests failed' }
    python -B tests/server/resource/stream_integration.py
    if ($LASTEXITCODE -ne 0) { throw 'HTTP/image/video integration failed' }
    python -B tests/server/resource/catalog_integration.py
    if ($LASTEXITCODE -ne 0) { throw 'MySQL/Chat flow integration failed' }
    $previousMysqlBin = $env:CHAT_MYSQL_BIN
    try {
        if (-not $env:CHAT_MYSQL_BIN) {
            $env:CHAT_MYSQL_BIN = Split-Path -Parent (Get-Command mysqld -ErrorAction Stop).Source
        }
        node tests/server/resource/schema_upgrade.js
        if ($LASTEXITCODE -ne 0) { throw 'Resource schema upgrade regression failed' }
    } finally { $env:CHAT_MYSQL_BIN = $previousMysqlBin }
    foreach ($owner in @('Gate', 'Status')) {
        Build-ResourceProject "tests/server/lifecycle/${owner}AsioPoolTests.vcxproj"
        & "build/windows-tests/Release/$($owner.ToLower())_asio_pool_tests.exe"
        if ($LASTEXITCODE -ne 0) { throw "$owner Asio regression failed" }
    }
    Build-ResourceProject 'tests/server/ServerUnitTests.vcxproj'
    Push-Location (Join-Path $repo 'build/windows-tests/Release')
    try {
        & ./server_unit_tests.exe "--gtest_output=xml:$output/server-unit-regression.xml"
        if ($LASTEXITCODE -ne 0) { throw 'Server unit regression failed' }
    } finally { Pop-Location }
    if ($QtRoot -and $MinGwRoot) {
        $qtBuild = Join-Path $repo 'build/resource-client'
        $ninja = Join-Path (Split-Path -Parent $MinGwRoot) 'Ninja/ninja.exe'
        cmake -S chat -B $qtBuild -G Ninja -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_PREFIX_PATH=$QtRoot" `
            "-DCMAKE_CXX_COMPILER=$MinGwRoot/bin/g++.exe" "-DCMAKE_MAKE_PROGRAM=$ninja" -DBUILD_TESTING=ON
        if ($LASTEXITCODE -ne 0) { throw 'Qt configure failed' }
        cmake --build $qtBuild --target chat resource_transfer_tests message_model_tests session_reset_tests -j 4
        if ($LASTEXITCODE -ne 0) { throw 'Qt build failed' }
        $previousPath = $env:PATH
        $previousHost = $env:RESOURCE_TEST_HOST
        try {
            $env:PATH = "$QtRoot/bin;$MinGwRoot/bin;$env:PATH"
            $env:RESOURCE_TEST_HOST = Join-Path $output 'ResourceTests.exe'
            $process = Start-Process -FilePath (Join-Path $qtBuild 'resource_transfer_tests.exe') `
                -ArgumentList @('-platform', 'minimal', '-style', 'Fusion', '-o', ((Join-Path $output 'qt-resource.xml') + ',junitxml')) `
                -WindowStyle Hidden -Wait -PassThru
            if ($process.ExitCode -ne 0) { throw 'Qt resource transfer tests failed' }
            ctest --test-dir $qtBuild -R 'message_model|session_reset' --output-on-failure
            if ($LASTEXITCODE -ne 0) { throw 'Qt message/session regression failed' }
        } finally { $env:PATH = $previousPath; $env:RESOURCE_TEST_HOST = $previousHost }
    } else {
        Write-Host 'Qt tests not run: pass both -QtRoot and -MinGwRoot.'
    }
} finally { Pop-Location }
