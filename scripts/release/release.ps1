[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('PrepareCandidate', 'PreflightCandidate', 'VerifyCandidate', 'PackCandidate', 'SealCandidate', 'VerifyPlanTask',
        'RegisterCandidate', 'BuildCandidate', 'VerifyUpload', 'CleanupCandidate', 'VerifyCiEvidence')]
    [string]$Task,
    [string]$InputFile,
    [string]$OutputFile,
    [string]$PlanTask,
    [string]$Version,
    [long]$AdmissionRunId,
    [string]$Repository,
    [string]$WorkflowFile,
    [string]$HeadSha,
    [string]$ExpectedWorkflowName,
    [string]$ExpectedJobName,
    [string]$ExpectedArtifactName,
    [string]$ExpectedCompanionArtifactName,
    [string]$ExpectedJUnitPath,
    [string[]]$ExpectedTestIds,
    [string]$EvidenceProfile,
    [string]$DownloadRoot,
    [int]$TimeoutSeconds,
    [ValidateSet('Release')][string]$Configuration = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
Import-Module (Join-Path $PSScriptRoot 'ReleaseGate.psm1') -Force
$ciOptions = ''
if ($Task -eq 'VerifyCiEvidence' -or $PlanTask -in @('R-00-T2', 'R-00-T3')) {
    foreach ($required in @('Repository', 'WorkflowFile', 'HeadSha', 'ExpectedWorkflowName', 'ExpectedJobName',
        'ExpectedArtifactName', 'ExpectedJUnitPath', 'ExpectedTestIds', 'EvidenceProfile', 'DownloadRoot', 'TimeoutSeconds')) {
        if (-not (Get-Variable -Name $required -ValueOnly)) { throw "Missing mandatory CI parameter: $required" }
    }
    $ciOptions = @{
        repository = $Repository; workflowFile = $WorkflowFile; headSha = $HeadSha
        expectedWorkflowName = $ExpectedWorkflowName; expectedJobName = $ExpectedJobName
        expectedArtifactName = $ExpectedArtifactName; expectedCompanionArtifactName = $ExpectedCompanionArtifactName
        expectedJUnitPath = $ExpectedJUnitPath; expectedTestIds = @($ExpectedTestIds -split ',')
        evidenceProfile = $EvidenceProfile; downloadRoot = $DownloadRoot; timeoutSeconds = $TimeoutSeconds
    } | ConvertTo-Json -Depth 5 -Compress
}
Invoke-ReleaseGate -Task $Task -InputFile $InputFile -OutputFile $OutputFile -PlanTask $PlanTask `
    -Version $Version -AdmissionRunId $AdmissionRunId -CiOptions $ciOptions
