[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Start', 'Stop', 'Status')]
    [string]$Task,

    [string]$Executable,
    [string[]]$Config,
    [string]$ConfigDirectory,
    [string]$StateDirectory,

    [ValidateRange(1, 60)]
    [int]$StartupTimeoutSeconds = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent $PSScriptRoot
if ([string]::IsNullOrWhiteSpace($StateDirectory)) {
    $StateDirectory = Join-Path $repoRoot 'build\chatserver-instances'
}
$StateDirectory = [System.IO.Path]::GetFullPath($StateDirectory)
$statePath = Join-Path $StateDirectory 'state'
$runtimePath = Join-Path $StateDirectory 'runtime'

function Resolve-ExistingFile {
    param([string]$Path, [string]$Description)

    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-ConfigFiles {
    $paths = @()
    if ($Config) {
        $paths += $Config
    }
    if (-not [string]::IsNullOrWhiteSpace($ConfigDirectory)) {
        if (-not (Test-Path -LiteralPath $ConfigDirectory -PathType Container)) {
            throw "Config directory does not exist: $ConfigDirectory"
        }
        $paths += Get-ChildItem -LiteralPath $ConfigDirectory -Filter '*.ini' -File |
            Sort-Object Name |
            Select-Object -ExpandProperty FullName
    }
    if ($paths.Count -eq 0) {
        throw 'Provide -Config and/or -ConfigDirectory when starting instances.'
    }

    $resolved = @($paths | ForEach-Object { Resolve-ExistingFile $_ 'Config file' } | Select-Object -Unique)
    $ids = @{}
    foreach ($path in $resolved) {
        $id = [System.IO.Path]::GetFileNameWithoutExtension($path)
        if ($id -notmatch '^[A-Za-z0-9][A-Za-z0-9._-]*$') {
            throw "Config file name must contain only letters, digits, dot, dash or underscore: $path"
        }
        if ($ids.ContainsKey($id)) {
            throw "Config file names must be unique: $id"
        }
        $ids[$id] = $true
    }

    $uniqueValues = @{
        'SelfServer.Name' = @{}
        'Log.Name' = @{}
    }
    $usedPorts = @{}
    $definitions = @()
    foreach ($path in $resolved) {
        $currentSection = ''
        $values = @{}
        foreach ($line in Get-Content -LiteralPath $path) {
            $trimmed = $line.Trim()
            if ($trimmed -match '^\[([^]]+)\]$') {
                $currentSection = $Matches[1]
                continue
            }
            if ($trimmed -match '^([^=]+?)\s*=\s*(.*)$' -and -not [string]::IsNullOrWhiteSpace($currentSection)) {
                $values["$currentSection.$($Matches[1].Trim())"] = $Matches[2].Trim()
            }
        }
        foreach ($key in $uniqueValues.Keys) {
            $value = [string]$values[$key]
            if ([string]::IsNullOrWhiteSpace($value)) {
                throw "Required config value [$($key.Replace('.', '].')) is missing in $path"
            }
            if ($uniqueValues[$key].ContainsKey($value)) {
                throw "Config value $key must be unique; '$value' is used by $($uniqueValues[$key][$value]) and $path"
            }
            $uniqueValues[$key][$value] = $path
        }
        $normalizedPorts = @{}
        foreach ($key in @('SelfServer.Port', 'SelfServer.RPCPort')) {
            $value = [string]$values[$key]
            $port = 0
            if (-not [int]::TryParse($value, [ref]$port) -or $port -lt 1 -or $port -gt 65535) {
                throw "Config value $key must be an integer between 1 and 65535 in $path"
            }
            $normalized = $port.ToString()
            if ($usedPorts.ContainsKey($normalized)) {
                throw "TCP and RPC listener ports must be unique; port '$normalized' is used by $($usedPorts[$normalized]) and $path"
            }
            $usedPorts[$normalized] = "$path ($key)"
            $normalizedPorts[$key] = $normalized
        }
        $definitions += [pscustomobject]@{
            Path = $path
            Instance = [System.IO.Path]::GetFileNameWithoutExtension($path)
            SelfServerName = [string]$values['SelfServer.Name']
            TcpPort = $normalizedPorts['SelfServer.Port']
            RpcPort = $normalizedPorts['SelfServer.RPCPort']
            LogName = [string]$values['Log.Name']
        }
    }
    return $definitions
}

function Get-ProcessIdentity {
    param([int]$Id)

    $process = Get-Process -Id $Id -ErrorAction SilentlyContinue
    if (-not $process) {
        return $null
    }
    try {
        return @{
            Process = $process
            Executable = [System.IO.Path]::GetFullPath($process.Path)
            StartedUtc = $process.StartTime.ToUniversalTime().ToString('o')
        }
    }
    catch {
        return $null
    }
}

function Test-StateMatchesProcess {
    param($State, $Identity)

    if (-not $Identity) {
        return $false
    }
    return $Identity.Executable -ieq [System.IO.Path]::GetFullPath([string]$State.Executable) -and
        $Identity.StartedUtc -eq [string]$State.StartedUtc
}

function Read-StateFiles {
    if (-not (Test-Path -LiteralPath $statePath -PathType Container)) {
        return
    }
    Get-ChildItem -LiteralPath $statePath -Filter '*.json' -File | Sort-Object Name
}

function Get-RunningStates {
    $running = @()
    foreach ($file in @(Read-StateFiles)) {
        $state = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
        $identity = Get-ProcessIdentity ([int]$state.Pid)
        if (Test-StateMatchesProcess $state $identity) {
            $running += $state
        }
    }
    return $running
}

function Assert-NoRunningInstanceConflicts {
    param($Definitions)

    $properties = @('SelfServerName', 'LogName')
    foreach ($running in @(Get-RunningStates)) {
        foreach ($definition in $Definitions) {
            foreach ($property in $properties) {
                $newValue = [string]$definition.$property
                $runningValue = [string]$running.$property
                if (-not [string]::IsNullOrWhiteSpace($runningValue) -and $newValue -eq $runningValue) {
                    throw "Config '$($definition.Path)' conflicts with running instance '$($running.Instance)': $property '$newValue' must be unique."
                }
            }
            $newPorts = @([string]$definition.TcpPort, [string]$definition.RpcPort)
            $runningPorts = @([string]$running.TcpPort, [string]$running.RpcPort)
            foreach ($port in $newPorts) {
                if ($runningPorts -contains $port) {
                    throw "Config '$($definition.Path)' conflicts with running instance '$($running.Instance)': listener port '$port' must be unique."
                }
            }
        }
    }
}

switch ($Task) {
    'Start' {
        $resolvedExecutable = Resolve-ExistingFile $Executable 'ChatServer executable'
        $configs = Get-ConfigFiles
        Assert-NoRunningInstanceConflicts $configs
        [void](New-Item -ItemType Directory -Path $statePath -Force)
        [void](New-Item -ItemType Directory -Path $runtimePath -Force)

        $startedThisRun = @()

        try {
            foreach ($definition in $configs) {
                $configPath = $definition.Path
                $instanceId = $definition.Instance
                $instanceStatePath = Join-Path $statePath "$instanceId.json"
                if (Test-Path -LiteralPath $instanceStatePath) {
                    $oldState = Get-Content -LiteralPath $instanceStatePath -Raw | ConvertFrom-Json
                    $oldIdentity = Get-ProcessIdentity ([int]$oldState.Pid)
                    if (Test-StateMatchesProcess $oldState $oldIdentity) {
                        throw "Instance '$instanceId' is already running with PID $($oldState.Pid)."
                    }
                    Remove-Item -LiteralPath $instanceStatePath -Force
                }

                $workingDirectory = Join-Path $runtimePath $instanceId
                [void](New-Item -ItemType Directory -Path $workingDirectory -Force)
                $stdoutPath = Join-Path $workingDirectory 'stdout.log'
                $stderrPath = Join-Path $workingDirectory 'stderr.log'
                $quotedConfigPath = '"' + $configPath.Replace('"', '\"') + '"'
                $process = Start-Process -FilePath $resolvedExecutable `
                    -ArgumentList @('--config', $quotedConfigPath) `
                    -WorkingDirectory $workingDirectory `
                    -WindowStyle Hidden `
                    -RedirectStandardOutput $stdoutPath `
                    -RedirectStandardError $stderrPath `
                    -PassThru
                $startedThisRun += $process
                $startupDeadline = [DateTime]::UtcNow.AddSeconds($StartupTimeoutSeconds)
                do {
                    Start-Sleep -Milliseconds 200
                    $process.Refresh()
                } while (-not $process.HasExited -and [DateTime]::UtcNow -lt $startupDeadline)
                if ($process.HasExited) {
                    $process.WaitForExit()
                    $exitCode = $process.ExitCode
                    if ($null -eq $exitCode -or [string]::IsNullOrWhiteSpace([string]$exitCode)) {
                        $exitCode = 'unknown'
                    }
                    $details = @(
                        Get-Content -LiteralPath $stderrPath -ErrorAction SilentlyContinue
                        Get-Content -LiteralPath $stdoutPath -ErrorAction SilentlyContinue
                    ) -join [Environment]::NewLine
                    if ([string]::IsNullOrWhiteSpace($details)) {
                        $details = 'No process output was captured.'
                    }
                    throw "Instance '$instanceId' exited during startup with code $exitCode. $details"
                }

                $identity = Get-ProcessIdentity $process.Id
                if (-not $identity) {
                    throw "Could not verify the identity of instance '$instanceId'."
                }
                [ordered]@{
                    Instance = $instanceId
                    Pid = $process.Id
                    Executable = $identity.Executable
                    StartedUtc = $identity.StartedUtc
                    Config = $configPath
                    WorkingDirectory = $workingDirectory
                    SelfServerName = $definition.SelfServerName
                    TcpPort = $definition.TcpPort
                    RpcPort = $definition.RpcPort
                    LogName = $definition.LogName
                    StandardOutput = $stdoutPath
                    StandardError = $stderrPath
                } | ConvertTo-Json | Set-Content -LiteralPath $instanceStatePath -Encoding UTF8

                Write-Host "Started $instanceId (PID $($process.Id)) using $configPath"
            }
        }
        catch {
            foreach ($process in $startedThisRun) {
                if (-not $process.HasExited) {
                    Stop-Process -Id $process.Id -ErrorAction SilentlyContinue
                }
            }
            foreach ($definition in $configs) {
                if ($definition.PSObject.Properties['Instance']) {
                    $instanceId = $definition.Instance
                    Remove-Item -LiteralPath (Join-Path $statePath "$instanceId.json") -Force -ErrorAction SilentlyContinue
                }
            }
            throw
        }
    }
    'Status' {
        $states = @(Read-StateFiles)
        if ($states.Count -eq 0) {
            Write-Host 'No ChatServer instances are recorded.'
            break
        }
        foreach ($file in $states) {
            $state = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            $identity = Get-ProcessIdentity ([int]$state.Pid)
            $status = if (Test-StateMatchesProcess $state $identity) { 'Running' } else { 'Stopped' }
            [pscustomobject]@{
                Instance = $state.Instance
                Status = $status
                Pid = $state.Pid
                Config = $state.Config
                WorkingDirectory = $state.WorkingDirectory
            }
        }
    }
    'Stop' {
        $states = @(Read-StateFiles)
        if ($states.Count -eq 0) {
            Write-Host 'No ChatServer instances are recorded.'
            break
        }
        foreach ($file in $states) {
            $state = Get-Content -LiteralPath $file.FullName -Raw | ConvertFrom-Json
            $identity = Get-ProcessIdentity ([int]$state.Pid)
            if (Test-StateMatchesProcess $state $identity) {
                Stop-Process -Id ([int]$state.Pid)
                Wait-Process -Id ([int]$state.Pid) -Timeout 10 -ErrorAction SilentlyContinue
                Write-Host "Stopped $($state.Instance) (PID $($state.Pid))."
            }
            else {
                Write-Warning "Skipped PID $($state.Pid) for '$($state.Instance)' because it no longer matches the recorded executable and start time."
            }
            Remove-Item -LiteralPath $file.FullName -Force
        }
    }
}
