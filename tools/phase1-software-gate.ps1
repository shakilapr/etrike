param(
    [switch]$SkipVehicle,
    [switch]$SkipSimulation,
    [string]$ArtifactRoot = "artifacts/phase1"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$configPath = Join-Path $repoRoot "tools/phase1/phase1_config.json"
$config = Get-Content -Raw -LiteralPath $configPath | ConvertFrom-Json

$timestamp = Get-Date -Format "yyyyMMdd-HHmmss"
$artifactDir = Join-Path $repoRoot (Join-Path $ArtifactRoot $timestamp)
$logDir = Join-Path $artifactDir "logs"
$traceDir = Join-Path $artifactDir "traces"
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
New-Item -ItemType Directory -Force -Path $traceDir | Out-Null
Copy-Item -LiteralPath $configPath -Destination (Join-Path $traceDir "phase1_config.json")

$results = New-Object System.Collections.Generic.List[object]

function Invoke-LoggedStep {
    param(
        [string]$Name,
        [string]$WorkDir,
        [string]$Command,
        [string[]]$Arguments = @(),
        [bool]$Required = $true
    )

    $safeName = ($Name -replace '[^a-zA-Z0-9_.-]', '_')
    $logPath = Join-Path $script:logDir "$safeName.log"
    $start = Get-Date
    "=== $Name ===" | Tee-Object -FilePath $logPath | Out-Null
    "workdir=$WorkDir" | Tee-Object -FilePath $logPath -Append | Out-Null
    "command=$Command $($Arguments -join ' ')" | Tee-Object -FilePath $logPath -Append | Out-Null
    "started=$($start.ToString('o'))" | Tee-Object -FilePath $logPath -Append | Out-Null

    $cmdInfo = Get-Command $Command -ErrorAction SilentlyContinue
    if ($null -eq $cmdInfo -and -not (Test-Path -LiteralPath $Command)) {
        $end = Get-Date
        "blocked=command not found: $Command" | Tee-Object -FilePath $logPath -Append | Out-Null
        $script:results.Add([pscustomobject]@{
            name = $Name
            status = "BLOCKED"
            required = $Required
            exitCode = 127
            durationSeconds = [math]::Round(($end - $start).TotalSeconds, 3)
            log = $logPath.Substring($script:repoRoot.Length + 1)
        }) | Out-Null
        return
    }

    Push-Location -LiteralPath $WorkDir
    try {
        $oldError = $ErrorActionPreference
        $ErrorActionPreference = "Continue"
        & $Command @Arguments 2>&1 | Tee-Object -FilePath $logPath -Append | Out-Null
        $ErrorActionPreference = $oldError
        $exitCode = if ($null -eq $LASTEXITCODE) { 0 } else { $LASTEXITCODE }
    } catch {
        $_ | Tee-Object -FilePath $logPath -Append | Out-Null
        $exitCode = 1
    } finally {
        $ErrorActionPreference = "Stop"
        Pop-Location
    }

    $end = Get-Date
    $status = if ($exitCode -eq 0) { "PASS" } elseif ($Required) { "FAIL" } else { "WARN" }
    "finished=$($end.ToString('o'))" | Tee-Object -FilePath $logPath -Append | Out-Null
    "exitCode=$exitCode" | Tee-Object -FilePath $logPath -Append | Out-Null
    "status=$status" | Tee-Object -FilePath $logPath -Append | Out-Null

    $script:results.Add([pscustomobject]@{
        name = $Name
        status = $status
        required = $Required
        exitCode = $exitCode
        durationSeconds = [math]::Round(($end - $start).TotalSeconds, 3)
        log = $logPath.Substring($script:repoRoot.Length + 1)
    }) | Out-Null
}

$gitState = & (Join-Path $repoRoot "tools/phase1/collect_git_state.ps1") -RepoRoot $repoRoot -LogDir $logDir

Invoke-LoggedStep "rt-native-build" "$repoRoot/rt-esp32" "pio" @("run", "-e", "native") $true
Invoke-LoggedStep "sys-native-build" "$repoRoot/sys-esp32" "pio" @("run", "-e", "native") $true
Invoke-LoggedStep "rt-native-unity" "$repoRoot/rt-esp32" "pio" @("test", "-e", "native") $true
Invoke-LoggedStep "sys-native-unity" "$repoRoot/sys-esp32" "pio" @("test", "-e", "native") $true

if (-not $SkipVehicle) {
    Invoke-LoggedStep "rt-vehicle-build" "$repoRoot/rt-esp32" "pio" @("run", "-e", "vehicle") $true
    Invoke-LoggedStep "sys-vehicle-build" "$repoRoot/sys-esp32" "pio" @("run", "-e", "vehicle") $true
}

if (-not (Test-Path -LiteralPath "$repoRoot/native-test/build")) {
    Invoke-LoggedStep "native-test-configure" "$repoRoot/native-test" "cmake" @("-S", ".", "-B", "build") $true
}
Invoke-LoggedStep "native-test-build" "$repoRoot/native-test" "cmake" @("--build", "build") $true
Invoke-LoggedStep "native-test-ctest" "$repoRoot/native-test/build" "ctest" @("-C", "Debug", "--output-on-failure") $true

if (-not $SkipSimulation) {
    Invoke-LoggedStep "simulation-npm-test" "$repoRoot/simulation" "npm" @("test") $true
}

Invoke-LoggedStep "can-protocol-verify" "$repoRoot" "python" @("protocol/tools/protocol.py", "validate") $true
$missingCoverage = @($config.knownMissingCoverage)
$blockedRequired = @($results | Where-Object { $_.required -and $_.status -eq "BLOCKED" })
$failedRequired = @($results | Where-Object { $_.required -and $_.status -eq "FAIL" })
$overall = if ($blockedRequired.Count -gt 0) { "BLOCKED" } elseif ($failedRequired.Count -gt 0) { "FAIL" } elseif ($missingCoverage.Count -gt 0) { "INCOMPLETE" } else { "PASS" }

$report = [ordered]@{
    schemaVersion = 1
    phase = $config.phase
    result = $overall
    startedAt = $timestamp
    git = $gitState
    artifactDir = $artifactDir.Substring($repoRoot.Length + 1)
    skipped = [ordered]@{
        vehicle = $SkipVehicle.IsPresent
        simulation = $SkipSimulation.IsPresent
    }
    missingCoverage = @($missingCoverage)
    results = @($results.ToArray())
}

$tracePath = Join-Path $traceDir "phase1-results.json"
$report.results | ConvertTo-Json -Depth 8 | Set-Content -Path $tracePath
$paths = & (Join-Path $repoRoot "tools/phase1/write_phase1_report.ps1") -Report $report -ArtifactDir $artifactDir -RepoRoot $repoRoot

"Phase 1 result: $overall"
"Report: $($paths.markdown)"

if ($overall -eq "PASS") { exit 0 }
if ($overall -eq "INCOMPLETE") { exit 2 }
if ($overall -eq "BLOCKED") { exit 3 }
exit 1
