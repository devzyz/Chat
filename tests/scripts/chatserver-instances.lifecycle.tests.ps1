Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$subject = Join-Path $repoRoot 'scripts\chatserver-instances.ps1'
$powershell = Join-Path $PSHOME 'powershell.exe'
if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) {
    $powershell = (Get-Command powershell.exe -ErrorAction Stop).Source
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("chat-instance-lifecycle-{0}" -f ([Guid]::NewGuid().ToString('N')))
$script:passed = 0
$script:failed = 0

function Write-ConfigFixture {
    param([string]$Path)

    [void](New-Item -ItemType Directory -Path (Split-Path -Parent $Path) -Force)
    @(
        '[SelfServer]'
        'Name=chat-lifecycle'
        'Port=18090'
        'RPCPort=50090'
        ''
        '[Log]'
        'Name=chat-lifecycle-log'
    ) | Set-Content -LiteralPath $Path -Encoding ASCII
}

function Write-StateFixture {
    param(
        [string]$StateDirectory,
        [string]$StartedUtc,
        [string]$Executable
    )

    $statePath = Join-Path $StateDirectory 'state'
    [void](New-Item -ItemType Directory -Path $statePath -Force)
    [ordered]@{
        Instance = 'chat-lifecycle'
        Pid = $PID
        Executable = $Executable
        StartedUtc = $StartedUtc
        Config = (Join-Path $testRoot 'chat-lifecycle.ini')
        WorkingDirectory = (Join-Path $StateDirectory 'runtime\chat-lifecycle')
        SelfServerName = 'chat-lifecycle'
        TcpPort = 18090
        RpcPort = 50090
        LogName = 'chat-lifecycle-log'
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $statePath 'chat-lifecycle.json') -Encoding UTF8
}

function Invoke-TestCase {
    param([string]$Name, [scriptblock]$Body)

    try {
        & $Body
        $script:passed++
        Write-Host "PASS $Name"
    }
    catch {
        $script:failed++
        Write-Host "FAIL $Name - $($_.Exception.Message)" -ForegroundColor Red
    }
}

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw $Message
    }
}

try {
    [void](New-Item -ItemType Directory -Path $testRoot -Force)
    $config = Join-Path $testRoot 'chat-lifecycle.ini'
    Write-ConfigFixture $config
    $currentProcess = Get-Process -Id $PID
    $currentExecutable = [System.IO.Path]::GetFullPath($currentProcess.Path)
    $currentStartedUtc = $currentProcess.StartTime.ToUniversalTime().ToString('o')

    Invoke-TestCase 'A child that exits during startup reports diagnostics and leaves no state' {
        $stateDirectory = Join-Path $testRoot 'startup-failure'
        $caught = $null
        try {
            & $subject -Task Start -Executable $powershell -Config $config `
                -StateDirectory $stateDirectory -StartupTimeoutSeconds 2
        }
        catch {
            $caught = $_
        }

        Assert-True ($null -ne $caught) 'Expected startup failure to throw.'
        Assert-True ($caught.Exception.Message -match 'exited during startup with code') `
            "Expected exit-code diagnostics, received: $($caught.Exception.Message)"
        $stateFiles = @(Get-ChildItem -LiteralPath (Join-Path $stateDirectory 'state') -Filter '*.json' -File -ErrorAction SilentlyContinue)
        Assert-True ($stateFiles.Count -eq 0) 'Startup failure left a process state file behind.'
    }

    Invoke-TestCase 'Status treats a reused PID with a different start time as stopped' {
        $stateDirectory = Join-Path $testRoot 'stale-status'
        Write-StateFixture $stateDirectory '2000-01-01T00:00:00.0000000Z' $currentExecutable

        $status = @(& $subject -Task Status -StateDirectory $stateDirectory)

        Assert-True ($status.Count -eq 1) "Expected one status row, received $($status.Count)."
        Assert-True ($status[0].Status -eq 'Stopped') "Expected Stopped, received $($status[0].Status)."
        Assert-True ($null -ne (Get-Process -Id $PID -ErrorAction SilentlyContinue)) 'Status terminated the unrelated current process.'
    }

    Invoke-TestCase 'Stop never terminates a PID whose recorded identity is stale' {
        $stateDirectory = Join-Path $testRoot 'stale-stop'
        Write-StateFixture $stateDirectory '2000-01-01T00:00:00.0000000Z' $currentExecutable

        $warningText = (& $subject -Task Stop -StateDirectory $stateDirectory 3>&1 | Out-String)

        Assert-True ($warningText -match 'no longer matches the recorded executable and start time') `
            "Expected stale-identity warning, received: $warningText"
        Assert-True ($null -ne (Get-Process -Id $PID -ErrorAction SilentlyContinue)) 'Stop terminated the unrelated current process.'
        Assert-True (-not (Test-Path -LiteralPath (Join-Path $stateDirectory 'state\chat-lifecycle.json'))) `
            'Stop did not remove the stale state file.'
    }

    Invoke-TestCase 'Start rejects conflicts recorded by an independently running instance' {
        $stateDirectory = Join-Path $testRoot 'running-conflict'
        Write-StateFixture $stateDirectory $currentStartedUtc $currentExecutable
        $caught = $null
        try {
            & $subject -Task Start -Executable $powershell -Config $config `
                -StateDirectory $stateDirectory -StartupTimeoutSeconds 1
        }
        catch {
            $caught = $_
        }

        Assert-True ($null -ne $caught) 'Expected a running-instance conflict to throw.'
        Assert-True ($caught.Exception.Message -match 'conflicts with running instance') `
            "Expected running-instance conflict details, received: $($caught.Exception.Message)"
        Assert-True ($null -ne (Get-Process -Id $PID -ErrorAction SilentlyContinue)) 'Conflict detection terminated the recorded process.'
    }
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

Write-Host "ChatServer instance lifecycle tests: $script:passed passed, $script:failed failed."
if ($script:failed -ne 0) {
    exit 1
}
exit 0
