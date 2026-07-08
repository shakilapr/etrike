param(
    [Parameter(Mandatory = $true)]
    [object]$Report,

    [Parameter(Mandatory = $true)]
    [string]$ArtifactDir,

    [Parameter(Mandatory = $true)]
    [string]$RepoRoot
)

$jsonPath = Join-Path $ArtifactDir "phase1-report.json"
$mdPath = Join-Path $ArtifactDir "phase1-report.md"

$Report | ConvertTo-Json -Depth 10 | Set-Content -Path $jsonPath

$md = New-Object System.Collections.Generic.List[string]
$md.Add("# Phase 1 Software Gate Report") | Out-Null
$md.Add("") | Out-Null
$md.Add("Result: **$($Report.result)**") | Out-Null
$md.Add("") | Out-Null
$md.Add("Git: ``$($Report.git.hash)`` on ``$($Report.git.branch)``; dirty=$($Report.git.dirty)") | Out-Null
$md.Add("") | Out-Null
$md.Add("## Suites") | Out-Null
$md.Add("") | Out-Null
$md.Add("| Suite | Status | Required | Exit | Seconds | Log |") | Out-Null
$md.Add("|-------|--------|----------|------|---------|-----|") | Out-Null
foreach ($r in $Report.results) {
    $md.Add("| $($r.name) | $($r.status) | $($r.required) | $($r.exitCode) | $($r.durationSeconds) | ``$($r.log)`` |") | Out-Null
}

$md.Add("") | Out-Null
$md.Add("## Missing Coverage") | Out-Null
$md.Add("") | Out-Null
if ($Report.missingCoverage.Count -eq 0) {
    $md.Add("- none") | Out-Null
} else {
    foreach ($m in $Report.missingCoverage) { $md.Add("- $m") | Out-Null }
}

$md | Set-Content -Path $mdPath

[pscustomobject]@{
    json = $jsonPath.Substring($RepoRoot.Length + 1)
    markdown = $mdPath.Substring($RepoRoot.Length + 1)
}
