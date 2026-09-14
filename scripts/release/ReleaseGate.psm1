Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Invoke-ReleaseGate {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Task,
        [string]$InputFile,
        [string]$OutputFile,
        [string]$PlanTask,
        [string]$Version,
        [long]$AdmissionRunId,
        [string]$CiOptions
    )
    $arguments = @((Join-Path $PSScriptRoot 'release_cli.py'), $Task)
    if ($InputFile) { $arguments += @('--input', $InputFile) }
    if ($OutputFile) { $arguments += @('--output', $OutputFile) }
    if ($PlanTask) { $arguments += @('--plan-task', $PlanTask) }
    if ($Version) { $arguments += @('--version', $Version) }
    if ($AdmissionRunId) { $arguments += @('--admission-run', [string]$AdmissionRunId) }
    # Windows PowerShell's legacy native argument passing strips embedded JSON quotes.
    # Send JSON through a temporary UTF-8 file instead of a shell-escaped command string.
    if ($CiOptions) {
        $optionsFile = [System.IO.Path]::GetTempFileName()
        try {
            [System.IO.File]::WriteAllText($optionsFile, $CiOptions, (New-Object System.Text.UTF8Encoding($false)))
            $arguments += @('--ci-options-file', $optionsFile)
            & python @arguments
            if ($LASTEXITCODE -ne 0) { throw 'Release CI evidence verification failed.' }
        } finally {
            Remove-Item -LiteralPath $optionsFile -Force
        }
        return
    }
    & python @arguments
    if ($LASTEXITCODE -ne 0) {
        throw 'Release gate failed; no candidate admission or publication was granted.'
    }
}

Export-ModuleMember -Function Invoke-ReleaseGate
