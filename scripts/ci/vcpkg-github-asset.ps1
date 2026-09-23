param([string]$Url, [string]$Sha512, [string]$Destination)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# CI-only vcpkg x-script provider. A miss falls back to vcpkg's original URL.
# Bypass the GitHub archive redirect endpoint; retain the port's locked hash.
<#
.SYNOPSIS
下载并校验固定摘要的 GitHub 资产；仅为 Release API 请求附加 GH_TOKEN，失败向调用方传播。
#>
function Invoke-GitHubAsset([string]$Url, [string]$Sha512, [string]$Destination) {
    if ($Sha512 -notmatch '^[a-fA-F0-9]{128}$') { throw 'Invalid SHA512.' }
    $headers = @{ Accept = 'application/octet-stream'; 'User-Agent' = 'Chat-CI' }
    if ($Url -cmatch '^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/archive/([A-Za-z0-9_./%-]+)\.tar\.gz$') {
        $downloadUrl = "https://codeload.github.com/$($Matches[1])/$($Matches[2])/tar.gz/$($Matches[3])"
    } elseif ($Url -cmatch '^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/releases/download/([A-Za-z0-9_.+-]+)/([A-Za-z0-9_.+-]+)$') {
        $repository = "$($Matches[1])/$($Matches[2])"
        $tag = [Uri]::EscapeDataString($Matches[3])
        $name = $Matches[4]
        $apiHeaders = @{ Accept = 'application/vnd.github+json'; 'User-Agent' = 'Chat-CI' }
        if (-not [string]::IsNullOrWhiteSpace($env:GH_TOKEN)) {
            $apiHeaders.Authorization = "Bearer $env:GH_TOKEN"
            $headers.Authorization = $apiHeaders.Authorization
        }
        $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repository/releases/tags/$tag" `
            -Headers $apiHeaders -TimeoutSec 60 -ErrorAction Stop
        $assets = @($release.assets | Where-Object { $_.name -ceq $name -and $_.browser_download_url -ceq $Url })
        if ($assets.Count -ne 1 -or [string]$assets[0].id -notmatch '^[1-9][0-9]*$') {
            throw 'Release asset identity missing or ambiguous.'
        }
        $downloadUrl = "https://api.github.com/repos/$repository/releases/assets/$($assets[0].id)"
    } else {
        throw 'Unsupported asset URL; use the original vcpkg download.'
    }
    try {
        # Keep the default stripping of Authorization on HTTP redirects.
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -Headers $headers -OutFile $Destination -TimeoutSec 300 -ErrorAction Stop
        # vcpkg can sanitize PSModulePath; Get-FileHash then cannot auto-load.
        # Stream through .NET so verification does not depend on module discovery.
        $hasher = [System.Security.Cryptography.SHA512]::Create()
        try {
            $stream = [System.IO.File]::OpenRead($Destination)
            try { $actualHash = [System.BitConverter]::ToString($hasher.ComputeHash($stream)).Replace('-', '') }
            finally { $stream.Dispose() }
        } finally { $hasher.Dispose() }
        if ($actualHash -ne $Sha512) {
            throw 'GitHub asset SHA512 mismatch.'
        }
    } catch {
        if (Test-Path -LiteralPath $Destination -PathType Leaf) { Remove-Item -LiteralPath $Destination -Force }
        throw
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    Invoke-GitHubAsset $Url $Sha512 $Destination
}
