param(
    [string]$JUnitPath
)

Set-StrictMode -Version 2.0
$ErrorActionPreference = 'Stop'

$repoRoot = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
$subject = Join-Path $repoRoot 'scripts\chatserver-instances.ps1'
$powershell = Join-Path $PSHOME 'powershell.exe'
if (-not (Test-Path -LiteralPath $powershell -PathType Leaf)) {
    $powershell = (Get-Command powershell.exe -ErrorAction Stop).Source
}
$placeholderExecutable = $env:ComSpec
if ([string]::IsNullOrWhiteSpace($placeholderExecutable) -or
    -not (Test-Path -LiteralPath $placeholderExecutable -PathType Leaf)) {
    throw 'The Windows command processor could not be located for the validation-only test seam.'
}

$testRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("chat-instance-validation-{0}" -f ([Guid]::NewGuid().ToString('N')))
$fixtureRoot = Join-Path $testRoot 'fixtures'
$stateRoot = Join-Path $testRoot 'state'
$invoker = Join-Path $testRoot 'invoke-subject.ps1'
$script:passed = 0
$script:failed = 0
$script:results = @()

function Write-JUnitReport {
    param([Parameter(Mandatory = $true)][string]$Path)

    $parent = Split-Path -Parent $Path
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        [void](New-Item -ItemType Directory -Path $parent -Force)
    }
    $failures = @($script:results | Where-Object { -not $_.Passed }).Count
    $duration = ($script:results | Measure-Object -Property Duration -Sum).Sum
    $settings = New-Object System.Xml.XmlWriterSettings
    $settings.Indent = $true
    $settings.Encoding = New-Object System.Text.UTF8Encoding($false)
    $writer = [System.Xml.XmlWriter]::Create($Path, $settings)
    try {
        $writer.WriteStartDocument()
        $writer.WriteStartElement('testsuites')
        $writer.WriteAttributeString('tests', [string]$script:results.Count)
        $writer.WriteAttributeString('failures', [string]$failures)
        $writer.WriteAttributeString('errors', '0')
        $writer.WriteStartElement('testsuite')
        $writer.WriteAttributeString('name', 'ChatServerInstanceValidation')
        $writer.WriteAttributeString('tests', [string]$script:results.Count)
        $writer.WriteAttributeString('failures', [string]$failures)
        $writer.WriteAttributeString('errors', '0')
        $writer.WriteAttributeString('time', $duration.ToString('0.000', [Globalization.CultureInfo]::InvariantCulture))
        foreach ($result in $script:results) {
            $writer.WriteStartElement('testcase')
            $writer.WriteAttributeString('classname', 'scripts.chatserver-instances.validation')
            $writer.WriteAttributeString('name', $result.TestId)
            $writer.WriteAttributeString('time', $result.Duration.ToString('0.000', [Globalization.CultureInfo]::InvariantCulture))
            if (-not $result.Passed) {
                $writer.WriteStartElement('failure')
                $writer.WriteAttributeString('message', $result.Failure)
                $writer.WriteString($result.Failure)
                $writer.WriteEndElement()
            }
            $writer.WriteStartElement('system-out')
            $writer.WriteString($result.Name)
            $writer.WriteEndElement()
            $writer.WriteEndElement()
        }
        $writer.WriteEndElement()
        $writer.WriteEndElement()
        $writer.WriteEndDocument()
    }
    finally {
        $writer.Dispose()
    }
}

function Write-ConfigFixture {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$SelfServerName,
        [Parameter(Mandatory = $true)]
        [string]$TcpPort,
        [Parameter(Mandatory = $true)]
        [string]$RpcPort,
        [AllowEmptyString()]
        [string]$LogName,
        [switch]$OmitLogName
    )

    $parent = Split-Path -Parent $Path
    [void](New-Item -ItemType Directory -Path $parent -Force)
    $lines = @(
        '[SelfServer]'
        "Name=$SelfServerName"
        "Port=$TcpPort"
        "RPCPort=$RpcPort"
        ''
        '[Log]'
    )
    if (-not $OmitLogName) {
        $lines += "Name=$LogName"
    }
    Set-Content -LiteralPath $Path -Value $lines -Encoding ASCII
}

function Invoke-ExpectedValidationFailure {
    param(
        [Parameter(Mandatory = $true)]
        [ValidatePattern('^A02-VAL-\d{2}$')]
        [string]$TestId,
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [string[]]$Configs,
        [Parameter(Mandatory = $true)]
        [string]$ExpectedMessage,
        [switch]$NoConfigArgument
    )

    $started = [DateTime]::UtcNow
    $caseRoot = Join-Path $stateRoot ($Name -replace '[^A-Za-z0-9._-]', '_')
    $configList = Join-Path $caseRoot 'configs.txt'
    [void](New-Item -ItemType Directory -Path $caseRoot -Force)
    if (-not $NoConfigArgument) {
        Set-Content -LiteralPath $configList -Value @($Configs) -Encoding ASCII
    }

    $arguments = @(
        '-NoLogo'
        '-NoProfile'
        '-NonInteractive'
        '-ExecutionPolicy'
        'Bypass'
        '-File'
        $invoker
        '-Subject'
        $subject
        '-Executable'
        $placeholderExecutable
        '-StateDirectory'
        $caseRoot
    )
    if (-not $NoConfigArgument) {
        $arguments += @('-ConfigList', $configList)
    }

    $savedErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $output = (& $powershell @arguments 2>&1 | Out-String)
    $exitCode = $LASTEXITCODE
    $ErrorActionPreference = $savedErrorActionPreference
    $failure = $null
    if ($exitCode -eq 0) {
        $failure = 'expected a non-zero process exit code, but validation returned zero'
    }
    elseif ($output -notmatch $ExpectedMessage) {
        $failure = "expected output to match '$ExpectedMessage', but received:`n$output"
    }

    if ($failure) {
        $script:failed++
        Write-Host "FAIL $TestId $Name - $failure" -ForegroundColor Red
    }
    else {
        $script:passed++
        Write-Host "PASS $TestId $Name"
    }
    $script:results += [pscustomobject]@{
        TestId = $TestId
        Name = $Name
        Passed = -not [bool]$failure
        Failure = [string]$failure
        Duration = ([DateTime]::UtcNow - $started).TotalSeconds
    }
}

try {
    [void](New-Item -ItemType Directory -Path $fixtureRoot -Force)
    @'
param(
    [Parameter(Mandatory = $true)][string]$Subject,
    [Parameter(Mandatory = $true)][string]$Executable,
    [Parameter(Mandatory = $true)][string]$StateDirectory,
    [string]$ConfigList
)
$ErrorActionPreference = 'Stop'
$configs = @()
if (-not [string]::IsNullOrWhiteSpace($ConfigList)) {
    $configs = @(Get-Content -LiteralPath $ConfigList)
}
& $Subject -Task Start -Executable $Executable -Config $configs -StateDirectory $StateDirectory
'@ | Set-Content -LiteralPath $invoker -Encoding ASCII

    $duplicateNameA = Join-Path $fixtureRoot 'duplicate-name-a.ini'
    $duplicateNameB = Join-Path $fixtureRoot 'duplicate-name-b.ini'
    Write-ConfigFixture $duplicateNameA 'chat-shared' '8090' '50051' 'log-a'
    Write-ConfigFixture $duplicateNameB 'chat-shared' '8091' '50052' 'log-b'
    $duplicateNamePattern = 'SelfServer\.Name must be unique'
    Invoke-ExpectedValidationFailure 'A02-VAL-01' 'Duplicate SelfServer.Name is rejected' `
        @($duplicateNameA, $duplicateNameB) $duplicateNamePattern

    $duplicateLogA = Join-Path $fixtureRoot 'duplicate-log-a.ini'
    $duplicateLogB = Join-Path $fixtureRoot 'duplicate-log-b.ini'
    Write-ConfigFixture $duplicateLogA 'chat-a' '8090' '50051' 'shared-log'
    Write-ConfigFixture $duplicateLogB 'chat-b' '8091' '50052' 'shared-log'
    Invoke-ExpectedValidationFailure 'A02-VAL-02' 'Duplicate Log.Name is rejected' `
        @($duplicateLogA, $duplicateLogB) 'Log\.Name must be unique'

    $crossTypeA = Join-Path $fixtureRoot 'cross-type-a.ini'
    $crossTypeB = Join-Path $fixtureRoot 'cross-type-b.ini'
    Write-ConfigFixture $crossTypeA 'chat-a' '8090' '50051' 'log-a'
    Write-ConfigFixture $crossTypeB 'chat-b' '8091' '8090' 'log-b'
    Invoke-ExpectedValidationFailure 'A02-VAL-03' 'TCP and RPC cross-type collision is rejected' `
        @($crossTypeA, $crossTypeB) "listener ports must be unique; port '8090'"

    $normalizedA = Join-Path $fixtureRoot 'normalized-a.ini'
    $normalizedB = Join-Path $fixtureRoot 'normalized-b.ini'
    Write-ConfigFixture $normalizedA 'chat-a' '08090' '50051' 'log-a'
    Write-ConfigFixture $normalizedB 'chat-b' '8091' '8090' 'log-b'
    Invoke-ExpectedValidationFailure 'A02-VAL-04' 'Port 08090 is normalized to 8090 before comparison' `
        @($normalizedA, $normalizedB) "listener ports must be unique; port '8090'"

    $unsafeName = Join-Path $fixtureRoot 'unsafe name.ini'
    Write-ConfigFixture $unsafeName 'chat-a' '8090' '50051' 'log-a'
    Invoke-ExpectedValidationFailure 'A02-VAL-05' 'Unsafe config file name is rejected' `
        @($unsafeName) 'Config file name must contain only letters, digits, dot, dash or underscore'

    $duplicateIdA = Join-Path $fixtureRoot 'first\duplicate.ini'
    $duplicateIdB = Join-Path $fixtureRoot 'second\duplicate.ini'
    Write-ConfigFixture $duplicateIdA 'chat-a' '8090' '50051' 'log-a'
    Write-ConfigFixture $duplicateIdB 'chat-b' '8091' '50052' 'log-b'
    Invoke-ExpectedValidationFailure 'A02-VAL-06' 'Duplicate config base names are rejected' `
        @($duplicateIdA, $duplicateIdB) 'Config file names must be unique: duplicate'

    $missingPath = Join-Path $fixtureRoot 'missing.ini'
    Invoke-ExpectedValidationFailure 'A02-VAL-07' 'Missing config file is rejected' `
        @($missingPath) 'Config file does not exist'

    Invoke-ExpectedValidationFailure 'A02-VAL-08' 'Missing config argument is rejected' `
        -ExpectedMessage 'Provide -Config and/or -ConfigDirectory when starting instances' -NoConfigArgument

    $missingValue = Join-Path $fixtureRoot 'missing-log-name.ini'
    Write-ConfigFixture $missingValue 'chat-a' '8090' '50051' '' -OmitLogName
    Invoke-ExpectedValidationFailure 'A02-VAL-09' 'Missing required config value is rejected' `
        @($missingValue) 'Required config value .* is missing'
}
finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

if (-not [string]::IsNullOrWhiteSpace($JUnitPath)) {
    Write-JUnitReport -Path $JUnitPath
}
Write-Host "ChatServer instance validation tests: $script:passed passed, $script:failed failed."
if ($script:failed -ne 0) {
    exit 1
}
exit 0
