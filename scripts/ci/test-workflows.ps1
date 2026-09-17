param([Parameter(Mandatory = $true)][string]$ToolRoot)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
. "$PSScriptRoot/vcpkg-github-asset.ps1"

# Only this standalone validator is downloaded; no project dependencies are restored.
$version = '1.7.12'
$name = "actionlint_${version}_windows_amd64.zip"
$url = "https://github.com/rhysd/actionlint/releases/download/v$version/$name"
# Verified against the official Release asset SHA-256 before recording this SHA-512.
$sha512 = 'b8e0c31a21e9aa5224acff89618b827ba0921bdab4b385651860e68b3a570d3007e8055c4e35f4e028f52adc3f0ce7f01d3c38be22838d1ed566f978183f9c46'
$root = [System.IO.Path]::GetFullPath($ToolRoot)
[void](New-Item -ItemType Directory -Path $root -Force)
$archive = Join-Path $root $name
if (-not (Test-Path -LiteralPath $archive -PathType Leaf)) {
    Invoke-GitHubAsset $url $sha512 $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA512).Hash -ne $sha512) {
    throw 'Pinned actionlint archive digest mismatch.'
}
$bin = Join-Path $root 'bin'
Expand-Archive -LiteralPath $archive -DestinationPath $bin -Force
$exe = Join-Path $bin 'actionlint.exe'
$actualVersion = @(& $exe -version)
if ($LASTEXITCODE -ne 0 -or $actualVersion.Count -eq 0 -or $actualVersion[0].Trim() -ne $version) {
    throw 'Pinned actionlint executable version mismatch.'
}

# A YAML parser accepts this, but GitHub rejects runner context at job env scope.
$invalid = @'
name: invalid-context-regression
on: push
jobs:
  check:
    runs-on: ubuntu-24.04
    env:
      CACHE_PATH: ${{ runner.temp }}
    steps:
      - run: echo checked
'@
$diagnostic = @($invalid | & $exe -shellcheck= -pyflakes= - 2>&1)
if ($LASTEXITCODE -eq 0 -or ($diagnostic -join "`n") -notmatch 'context "runner" is not allowed here') {
    throw 'Workflow validator did not reject invalid job-level runner context.'
}

$repoRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$workflows = @(Get-ChildItem -LiteralPath (Join-Path $repoRoot '.github/workflows') -File |
    Where-Object { $_.Extension -in @('.yml', '.yaml') } | Sort-Object Name |
    ForEach-Object { $_.FullName })
if ($workflows.Count -eq 0) { throw 'No workflows found to validate.' }
# Keep Actions semantic checking independent of optional shellcheck/pyflakes installations.
& $exe -shellcheck= -pyflakes= @workflows
if ($LASTEXITCODE -ne 0) { throw "Workflow semantic validation failed: $LASTEXITCODE" }
Write-Host "Validated $($workflows.Count) workflows with actionlint $version; invalid-context regression rejected."
