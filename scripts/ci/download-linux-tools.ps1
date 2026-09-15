[CmdletBinding()]
param([Parameter(Mandatory = $true)][string]$DownloadRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/vcpkg-github-asset.ps1"
$assets = Get-Content -LiteralPath "$PSScriptRoot/linux-tool-assets.json" -Raw | ConvertFrom-Json
foreach ($name in @('cmake', 'ninja')) {
    $asset = $assets.$name
    $target = Join-Path $DownloadRoot $asset.file
    if (Test-Path -LiteralPath $target) { throw 'Tool download destination already exists.' }
    Invoke-GitHubAsset $asset.url $asset.sha512 $target
}
