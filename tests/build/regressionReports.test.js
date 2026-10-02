'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const test = require('node:test');

test('report inventory grows without a second total and retains failure gates',
    { skip: process.platform !== 'win32' },
    /** 执行生产报告校验器，验证单点增加用例以及失败、缺项、清理和敏感信息拒绝。 */ () => {
        const root = fs.mkdtempSync(path.join(os.tmpdir(), 'chat-report-contract-'));
        try {
            const script = String.raw`
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($env:CHAT_REPORT_RUNNER, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'Runner parsing failed' }
$testResults = $env:CHAT_REPORT_FIXTURES
foreach ($name in @('regressionReportGroups', 'legacyRegressionReportGroups')) {
    $assignment = $ast.EndBlock.Statements | Where-Object { $_ -is [System.Management.Automation.Language.AssignmentStatementAst] -and $_.Left.Extent.Text -eq ('$' + $name) }
    . ([scriptblock]::Create($assignment.Extent.Text))
}
foreach ($name in @('Get-RegressionExpectedCount', 'Assert-RegressionReport', 'Assert-RegressionCleanupEvidence', 'Confirm-RegressionReports')) {
    $definition = $ast.EndBlock.Statements | Where-Object { $_ -is [System.Management.Automation.Language.FunctionDefinitionAst] -and $_.Name -eq $name }
    if ($definition) { . ([scriptblock]::Create($definition.Extent.Text)) }
}
$cleanup = @{
    'server_integration.xml' = @('TeardownIsReverseOrderedAndPreservesPrimaryAndCleanupFailures', 'StopUsesGracefulSignalAndClosesPipesByDeadline', 'StopReleasesSessionsThreadsSocketsAndServerOwnership', 'ChatRealDependencyBoundaryIsExplicitBoundedAndResidueFree')
    'client_integration.xml' = @('http_transport.deletingTransportReleasesReplyAndLoopbackSocket', 'tcp_transport.closeAndDeleteReleaseOwnedResources')
}
foreach ($group in $regressionReportGroups) {
    $names = @(if ($cleanup.ContainsKey($group.Name)) { $cleanup[$group.Name] })
    $names += @(0..($group.ExpectedCount - $names.Count - 1) | ForEach-Object { 'case-' + $_ })
    $xml = '<testsuite>' + (($names | ForEach-Object { '<testcase name="' + $_ + '"/>' }) -join '') + '</testsuite>'
    [IO.File]::WriteAllText((Join-Path $testResults $group.Name), $xml)
}
Confirm-RegressionReports
# An added test changes only its inventory row and actual report.
$regressionReportGroups[0].ExpectedCount++
$reportPath = Join-Path $testResults $regressionReportGroups[0].Name
$valid = [IO.File]::ReadAllText($reportPath).Replace('</testsuite>', '<testcase name="added-contract"/></testsuite>')
[IO.File]::WriteAllText($reportPath, $valid)
Confirm-RegressionReports
foreach ($badCase in @('<testcase name="bad"><failure/></testcase>', '<testcase name="bad"><error/></testcase>', '<testcase name="bad"><skipped/></testcase>', '<testcase name="bad" status="notrun"/>', '<testcase name="bad" result="timeout"/>', '<testcase name="bad"><system-out>token=synthetic-value</system-out></testcase>', '')) {
    [IO.File]::WriteAllText($reportPath, $valid.Replace('<testcase name="added-contract"/>', $badCase))
    $rejected = $false
    try { Confirm-RegressionReports } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid report was accepted' }
}
[IO.File]::WriteAllText($reportPath, $valid)
$rejected = $false
[IO.File]::WriteAllText($reportPath, '<testsuite>')
try { Confirm-RegressionReports } catch { $rejected = $true }
if (-not $rejected) { throw 'Malformed XML was accepted' }
[IO.File]::WriteAllText($reportPath, $valid)
$cleanupPath = Join-Path $testResults 'client_integration.xml'
$cleanupXml = [IO.File]::ReadAllText($cleanupPath)
[IO.File]::WriteAllText($cleanupPath, $cleanupXml.Replace('tcp_transport.closeAndDeleteReleaseOwnedResources', 'unrelated-case'))
$rejected = $false
try { Confirm-RegressionReports } catch { $rejected = $true }
if (-not $rejected) { throw 'Missing cleanup evidence was accepted' }
[IO.File]::WriteAllText($cleanupPath, $cleanupXml)
foreach ($invalidInventory in @('duplicate', 'missing', 'zero')) {
    $saved = @($regressionReportGroups)
    if ($invalidInventory -eq 'duplicate') { $regressionReportGroups += $regressionReportGroups[0] }
    if ($invalidInventory -eq 'missing') { $regressionReportGroups = @($regressionReportGroups | Where-Object { $_.Name -ne 'server_resource_integration.xml' }) }
    if ($invalidInventory -eq 'zero') { $regressionReportGroups += [pscustomobject]@{ Lane='server'; Name='empty.xml'; ExpectedCount=0 } }
    $rejected = $false
    try { Confirm-RegressionReports } catch { $rejected = $true }
    if (-not $rejected) { throw 'Invalid inventory was accepted' }
    $regressionReportGroups = $saved
}
[IO.File]::Delete($reportPath)
$rejected = $false
try { Confirm-RegressionReports } catch { $rejected = $true }
if (-not $rejected) { throw 'Missing report was accepted' }
Write-Host 'Report growth and rejection contracts passed'
`;
            const result = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], {
                env: { ...process.env, CHAT_REPORT_RUNNER: path.resolve(__dirname, '../../scripts/windows-local.ps1'),
                    CHAT_REPORT_FIXTURES: root }, encoding: 'utf8', timeout: 30000
            });
            assert.ifError(result.error);
            assert.equal(result.status, 0, result.stdout + result.stderr);
            assert.match(result.stdout, /Report growth and rejection contracts passed/);
        } finally { fs.rmSync(root, { recursive: true, force: true }); }
    });
