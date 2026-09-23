$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/vcpkg-github-asset.ps1"

$script:calls = 0
$script:downloadFailure = $false
$script:releaseMode = 'valid'
$originalToken = $env:GH_TOKEN
<# .SYNOPSIS 模拟下载并记录请求头，禁止测试访问公网。 #>
function Invoke-WebRequest {
    param($Uri, $OutFile, $TimeoutSec, $ErrorAction, $Headers, [switch]$UseBasicParsing)
    $script:calls++
    $script:downloadUrl = $Uri
    $script:downloadHeaders = $Headers
    if ($script:downloadFailure) { throw 'download failed: HTTP 504' }
    [System.IO.File]::WriteAllText($OutFile, 'fixture')
}
<# .SYNOPSIS 模拟 Release 元数据并记录认证头。 #>
function Invoke-RestMethod {
    param($Uri, $Headers, $TimeoutSec, $ErrorAction)
    $script:metadataUrl = $Uri
    $script:metadataHeaders = $Headers
    $assets = @(@{ id = 123; name = 'ninja-win.zip';
        browser_download_url = 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' })
    if ($script:releaseMode -eq 'missing') { $assets = @() }
    if ($script:releaseMode -eq 'duplicate') { $assets += $assets[0] }
    if ($script:releaseMode -eq 'wrong-url') { $assets[0].browser_download_url = 'https://example.com/ninja-win.zip' }
    return @{ assets = $assets }
}
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
[void](New-Item -ItemType Directory -Path $scratch)
try {
    $env:GH_TOKEN = 'fixture-token'
    $destination = Join-Path $scratch 'archive.tar.gz'
    [System.IO.File]::WriteAllText($destination, 'fixture')
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA512).Hash
    # vcpkg's sanitized child environment may not auto-load the Utility module.
    # A valid download must still be checked without that optional cmdlet.
    function Get-FileHash { throw 'Get-FileHash unavailable in vcpkg child process.' }
    Remove-Item -LiteralPath $destination
    Invoke-GitHubAsset 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' $hash $destination
    if ($script:metadataHeaders['Authorization'] -ne 'Bearer fixture-token' -or
        $script:downloadHeaders['Authorization'] -ne 'Bearer fixture-token') {
        throw 'Release API requests must authenticate with GH_TOKEN.'
    }
    if ($script:metadataUrl -ne 'https://api.github.com/repos/ninja-build/ninja/releases/tags/v1.13.2' -or
        $script:downloadUrl -ne 'https://api.github.com/repos/ninja-build/ninja/releases/assets/123' -or
        $script:downloadHeaders.Accept -ne 'application/octet-stream') {
        throw 'Release asset was not bound to its official API identity.'
    }
    Remove-Item -LiteralPath $destination
    foreach ($mode in @('missing', 'duplicate', 'wrong-url')) {
        $script:releaseMode = $mode
        $before = $script:calls
        try {
            Invoke-GitHubAsset 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' $hash $destination
            throw 'Invalid release identity accepted.'
        } catch { if ($_.Exception.Message -notmatch 'identity missing or ambiguous') { throw } }
        if ($script:calls -ne $before) { throw 'Unbound release asset downloaded.' }
    }
    $script:releaseMode = 'valid'
    $script:downloadFailure = $false
    Invoke-GitHubAsset 'https://github.com/fmtlib/fmt/archive/12.1.0.tar.gz' $hash $destination
    if ($script:downloadUrl -ne 'https://codeload.github.com/fmtlib/fmt/tar.gz/12.1.0') { throw 'Archive was not mapped to codeload.' }
    if ($script:downloadHeaders.ContainsKey('Authorization')) { throw 'Token sent to codeload.' }
    Remove-Item -LiteralPath $destination
    $env:GH_TOKEN = $null
    Invoke-GitHubAsset 'https://github.com/ninja-build/ninja/releases/download/v1.13.2/ninja-win.zip' $hash $destination
    if ($script:metadataHeaders.ContainsKey('Authorization') -or
        $script:downloadHeaders.ContainsKey('Authorization')) { throw 'Anonymous requests contain authorization.' }
    Remove-Item -LiteralPath $destination
    $before = $script:calls
    try {
        Invoke-GitHubAsset 'https://example.com/archive/v1.tar.gz' $hash $destination
        throw 'Unsupported URL accepted.'
    } catch { if ($_.Exception.Message -notmatch 'Unsupported asset URL') { throw } }
    if ($script:calls -ne $before) { throw 'Unsupported URL triggered a download.' }
    try {
        Invoke-GitHubAsset 'https://github.com/fmtlib/fmt/archive/12.1.0.tar.gz' ('0' * 128) $destination
        throw 'Bad hash accepted.'
    } catch { if ($_.Exception.Message -notmatch 'SHA512 mismatch') { throw } }
    if (Test-Path -LiteralPath $destination) { throw 'Bad hash left an asset behind.' }
    $script:downloadFailure = $true
    $before = $script:calls
    try {
        Invoke-GitHubAsset 'https://github.com/boostorg/property_tree/archive/boost-1.90.0.tar.gz' $hash $destination
        throw 'Download error accepted.'
    } catch { if ($_.Exception.Message -notmatch 'download failed') { throw } }
    if ($script:calls -ne ($before + 1)) { throw 'Download was retried.' }
    Write-Host 'PASS: API authentication, anonymous access, codeload isolation, asset identity, hashes and failures; no retries.'
} finally {
    $env:GH_TOKEN = $originalToken
    Get-ChildItem -LiteralPath $scratch -File | Remove-Item -Force
    Remove-Item -LiteralPath $scratch
}
