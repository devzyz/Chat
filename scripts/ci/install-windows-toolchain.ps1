[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$VcpkgRoot, [switch]$Refresh)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
if ($env:GITHUB_ACTIONS -ne 'true' -or $env:RUNNER_OS -ne 'Windows') {
    throw 'Toolchain installation is restricted to a GitHub Windows runner.'
}
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$expectedRoot = [IO.Path]::GetFullPath((Join-Path $repoRoot '.ci/vcpkg'))
if ([IO.Path]::GetFullPath($VcpkgRoot) -ne $expectedRoot) { throw 'Only the run-owned vcpkg checkout may be configured.' }
$lock = $env:CHAT_TOOLCHAIN_LOCK | ConvertFrom-Json
$utf8 = New-Object Text.UTF8Encoding($false)
$lockPath = Join-Path $repoRoot '.ci/windows-toolchain.json'
[IO.File]::WriteAllText($lockPath, ($lock | ConvertTo-Json -Depth 10) + "`n", $utf8)
& node (Join-Path $PSScriptRoot 'toolchain.js') validate $lockPath
if ($LASTEXITCODE -ne 0) { throw 'Invalid input toolchain.' }
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs = (& $vswhere -latest -version '[17.0,18.0)' -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ($LASTEXITCODE -ne 0 -or -not $vs) { throw 'Visual Studio 2022 C++ tools are required.' }
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
if ($Refresh) {
    $lock.msvc.toolset = (Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory |
        Where-Object <# 筛选 MSVC 版本目录。 #> { $_.Name -match '^14\.\d+\.\d+$' } | Sort-Object <# 按 MSVC 版本数值排序。 #> { [version]$_.Name } -Descending | Select-Object -First 1).Name
    $lock.msvc.sdk = (Get-ChildItem -LiteralPath $sdkRoot -Directory |
        Where-Object <# 筛选 Windows SDK 版本目录。 #> { $_.Name -match '^10\.0\.\d+\.0$' } | Sort-Object <# 按 SDK 版本数值排序。 #> { [version]$_.Name } -Descending | Select-Object -First 1).Name
}
$compiler = Join-Path $vs "VC/Tools/MSVC/$($lock.msvc.toolset)/bin/Hostx64/x64/cl.exe"
if (-not (Test-Path -LiteralPath $compiler -PathType Leaf) -or
    -not (Test-Path -LiteralPath (Join-Path $sdkRoot "$($lock.msvc.sdk)/um/Windows.h") -PathType Leaf)) {
    $toolsets = (Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Tools/MSVC') -Directory).Name -join ', '
    $sdks = (Get-ChildItem -LiteralPath $sdkRoot -Directory).Name -join ', '
    throw "Locked MSVC $($lock.msvc.toolset)/SDK $($lock.msvc.sdk) unavailable. Available toolsets: $toolsets; SDKs: $sdks. Run the weekly refresh; refusing an implicit tool upgrade."
}
# Read numeric PE version fields; shell quoting and localized banners differ
# between Windows PowerShell and the hosted runner's PowerShell 7.
$compilerInfo = [Diagnostics.FileVersionInfo]::GetVersionInfo($compiler)
$version = '{0}.{1}.{2}.{3}' -f $compilerInfo.FileMajorPart, $compilerInfo.FileMinorPart,
    $compilerInfo.FileBuildPart, $compilerInfo.FilePrivatePart
$compilerHash = (Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant()
if ($Refresh) {
    $lock.msvc.compilerVersion = $version
    $lock.msvc | Add-Member -NotePropertyName compilerSha256 -NotePropertyValue $compilerHash -Force
} elseif ($version -ne $lock.msvc.compilerVersion -or
    ($lock.msvc.PSObject.Properties.Name -contains 'compilerSha256' -and $lock.msvc.compilerSha256 -ne $compilerHash)) {
    throw "MSVC drift: expected $($lock.msvc.compilerVersion), got $version. Refusing dependency rebuild."
}

if ($Refresh) {
    $repos = @{ cmake = 'Kitware/CMake'; ninja = 'ninja-build/ninja'; 'powershell-core' = 'PowerShell/PowerShell' }
    foreach ($tool in $lock.tools) {
        $releaseJson = & gh api "repos/$($repos[$tool.name])/releases/latest"
        if ($LASTEXITCODE -ne 0) { throw "Cannot discover latest $($tool.name)." }
        $release = $releaseJson | ConvertFrom-Json
        $version = $release.tag_name -replace '^v', ''
        if ($version -notmatch '^\d+\.\d+\.\d+$' -or $release.prerelease -or $release.draft) { throw 'Expected a stable release.' }
        $assetName = switch ($tool.name) {
            'cmake' { "cmake-$version-windows-x86_64.zip" }
            'ninja' { 'ninja-win.zip' }
            'powershell-core' { "PowerShell-$version-win-x64.zip" }
        }
        $assets = @($release.assets | Where-Object <# 匹配所需下载资源名称。 #> { $_.name -ceq $assetName })
        if ($assets.Count -ne 1 -or $assets[0].digest -notmatch '^sha256:[a-f0-9]{64}$') {
            throw "Latest $($tool.name) has no unique SHA256-verified asset."
        }
        $asset = $assets[0]
        $download = Join-Path $env:RUNNER_TEMP $assetName
        Invoke-WebRequest -Uri "https://api.github.com/repos/$($repos[$tool.name])/releases/assets/$($asset.id)" `
            -Headers @{ Accept = 'application/octet-stream'; 'User-Agent' = 'Chat-CI' } -OutFile $download -TimeoutSec 300
        if ((Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant() -ne $asset.digest.Substring(7)) {
            throw 'Latest tool download digest mismatch.'
        }
        $tool.version = $version
        $tool.url = $asset.browser_download_url
        $tool.archive = if ($tool.name -eq 'ninja') { "ninja-win-$version.zip" } else { $assetName }
        $tool.sha512 = (Get-FileHash -LiteralPath $download -Algorithm SHA512).Hash.ToLowerInvariant()
        if ($tool.name -eq 'cmake') { $tool.executable = "cmake-$version-windows-x86_64/bin/cmake.exe" }
    }
}
[IO.File]::WriteAllText($lockPath, ($lock | ConvertTo-Json -Depth 10) + "`n", $utf8)
& node (Join-Path $PSScriptRoot 'toolchain.js') validate $lockPath
if ($LASTEXITCODE -ne 0) { throw 'Invalid resolved toolchain.' }

# vcpkg's tool catalog supplies versions and integrity hashes. Force-download also
# prevents a newer system pwsh/cmake/ninja from winning over these exact entries.
$catalogPath = Join-Path $VcpkgRoot 'scripts/vcpkg-tools.json'
$catalog = Get-Content -LiteralPath $catalogPath -Raw | ConvertFrom-Json
foreach ($tool in $lock.tools) {
    $catalog.tools = @($catalog.tools | Where-Object <# 筛除将被替换的相同 Windows 工具条目。 #> {
        -not ($_.name -eq $tool.name -and $_.os -eq 'windows' -and
            $_.PSObject.Properties.Name -contains 'arch' -and $_.arch -eq $tool.arch)
    }) + @($tool)
}
[IO.File]::WriteAllText($catalogPath, ($catalog | ConvertTo-Json -Depth 20) + "`n", $utf8)
$env:VCPKG_FORCE_DOWNLOADED_BINARIES = '1'
$assetProvider = Join-Path $PSScriptRoot 'vcpkg-github-asset.ps1'
$env:X_VCPKG_ASSET_SOURCES = 'clear;x-script,powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' + $assetProvider + '" "{url}" "{sha512}" "{dst}"'
foreach ($tool in $lock.tools) {
    $fetched = @(& (Join-Path $VcpkgRoot 'vcpkg.exe') fetch $tool.name --x-stderr-status)
    if ($LASTEXITCODE -ne 0 -or $fetched.Count -eq 0) { throw "Cannot acquire locked $($tool.name)." }
    $exe = $fetched[-1].Trim()
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing tool executable: $($tool.name)" }
    $actualVersion = if ($tool.name -eq 'powershell-core') {
        & $exe -NoLogo -NoProfile -Command '$PSVersionTable.PSVersion.ToString()'
    } else { (& $exe --version | Select-Object -First 1) -replace '^cmake version ', '' }
    if ($LASTEXITCODE -ne 0 -or $actualVersion -ne $tool.version) {
        throw "Tool drift: $($tool.name) expected $($tool.version), got $actualVersion."
    }
}
$triplet = Join-Path $repoRoot 'triplets/x64-windows-chat-release.cmake'
[IO.File]::AppendAllText($triplet, "`nset(VCPKG_PLATFORM_TOOLSET_VERSION $($lock.msvc.toolset))`nset(VCPKG_CMAKE_SYSTEM_VERSION $($lock.msvc.sdk))`n", $utf8)
@(
    'VCPKG_FORCE_DOWNLOADED_BINARIES=1'
    "VCToolsVersion=$($lock.msvc.toolset)"
    "WindowsTargetPlatformVersion=$($lock.msvc.sdk)"
    "CHAT_WINDOWS_TOOLCHAIN=$lockPath"
) | Out-File -LiteralPath $env:GITHUB_ENV -Encoding utf8 -Append
Write-Host "Selected MSVC $($lock.msvc.compilerVersion), SDK $($lock.msvc.sdk); locked tools:"
$lock.tools | Select-Object name, version | Format-Table
