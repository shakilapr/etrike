param(
    [Parameter(Mandatory = $true)]
    [string]$RepoRoot,

    [Parameter(Mandatory = $true)]
    [string]$LogDir
)

$gitLog = Join-Path $LogDir "git-state.log"
Push-Location -LiteralPath $RepoRoot
try {
    $hash = (& git rev-parse HEAD).Trim()
    $branch = (& git branch --show-current).Trim()
    $status = @(& git status --short)

    "hash=$hash" | Set-Content -Path $gitLog
    "branch=$branch" | Add-Content -Path $gitLog
    "status:" | Add-Content -Path $gitLog
    $status | Add-Content -Path $gitLog

    [pscustomobject]@{
        hash = $hash
        branch = $branch
        dirty = $status.Count -gt 0
        status = $status
    }
} finally {
    Pop-Location
}
