$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/vcpkg-github-asset.ps1"

$script:calls = 0
function Invoke-WebRequest {
    param($Uri, $OutFile, $TimeoutSec, $ErrorAction, [switch]$UseBasicParsing)
    $script:calls++
    $script:downloadUrl = $Uri
    if ($script:downloadFailure) { throw 'download failed: HTTP 504' }
    [System.IO.File]::WriteAllText($OutFile, 'fixture')
}
$scratch = Join-Path ([System.IO.Path]::GetTempPath()) ([guid]::NewGuid().ToString())
[void](New-Item -ItemType Directory -Path $scratch)
try {
    $destination = Join-Path $scratch 'archive.tar.gz'
    [System.IO.File]::WriteAllText($destination, 'fixture')
    $hash = (Get-FileHash -LiteralPath $destination -Algorithm SHA512).Hash
    Remove-Item -LiteralPath $destination
    $script:downloadFailure = $false
    Invoke-GitHubAsset 'https://github.com/fmtlib/fmt/archive/12.1.0.tar.gz' $hash $destination
    if ($script:downloadUrl -ne 'https://codeload.github.com/fmtlib/fmt/tar.gz/12.1.0') { throw 'Archive was not mapped to codeload.' }
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
    Write-Host 'PASS: codeload mapping, unsupported URL, hash mismatch, download failure; no retries.'
} finally {
    Get-ChildItem -LiteralPath $scratch -File | Remove-Item -Force
    Remove-Item -LiteralPath $scratch
}
