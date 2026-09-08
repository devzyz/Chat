param([string]$Url, [string]$Sha512, [string]$Destination)

# CI-only vcpkg x-script provider. A miss falls back to vcpkg's original URL.
# Bypass the GitHub archive redirect endpoint; retain the port's locked hash.
function Invoke-GitHubAsset([string]$Url, [string]$Sha512, [string]$Destination) {
    if ($Url -cnotmatch '^https://github\.com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/archive/([A-Za-z0-9_./%-]+)\.tar\.gz$') {
        throw 'Unsupported asset URL; use the original vcpkg download.'
    }
    $downloadUrl = "https://codeload.github.com/$($Matches[1])/$($Matches[2])/tar.gz/$($Matches[3])"
    if ($Sha512 -notmatch '^[a-fA-F0-9]{128}$') { throw 'Invalid SHA512.' }
    try {
        Invoke-WebRequest -UseBasicParsing -Uri $downloadUrl -OutFile $Destination -TimeoutSec 300 -ErrorAction Stop
        if ((Get-FileHash -LiteralPath $Destination -Algorithm SHA512).Hash -ne $Sha512) {
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
