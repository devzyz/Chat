param([Parameter(Mandatory = $true)][string]$Stage)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$installation = & $vswhere -latest -products '*' -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $installation) { throw 'Visual Studio runtime was not found.' }
$runtime = Get-ChildItem -LiteralPath (Join-Path $installation 'VC/Redist/MSVC') -Directory |
    Where-Object { $_.Name -match '^\d+\.\d+\.\d+(\.\d+)?$' } |
    Sort-Object { [version]$_.Name } -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container } |
    Select-Object -First 1
if (-not $runtime) { throw 'MSVC x64 redistributable was not found.' }
Get-ChildItem -LiteralPath $runtime -Filter '*.dll' -File | Copy-Item -Destination $Stage
foreach ($name in @('msvcp140.dll', 'vcruntime140.dll', 'vcruntime140_1.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $Stage $name) -PathType Leaf)) {
        throw "Missing runtime: $name"
    }
}
