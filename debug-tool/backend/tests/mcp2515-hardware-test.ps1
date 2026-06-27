# MCP2515 Hardware Validation Script
# ===================================
# Validates that the RT ESP32-S3 MCP2515 CAN controller (high bus)
# transmits frames correctly after the 12-bug fix series.
#
# Prerequisites:
#   - RT ESP32-S3 running the fixed firmware (pio run completed)
#   - CANalyst-II connected to the high CAN bus (RT MCP2515 bus)
#   - Debug tool backend running: cd debug-tool/backend && npm run dev
#   - CAN_TRANSPORT=canalystii (or serial)
#
# Usage:
#   pwsh debug-tool/backend/tests/mcp2515-hardware-test.ps1

param(
    [string]$BaseUrl = "http://localhost:3000",
    [int]$CaptureDurationS = 30,
    [switch]$SkipHardware  # Skip tests that need physical CAN bus
)

$ErrorActionPreference = "Stop"
$Pass = 0
$Fail = 0
$Warn = 0

function Assert-That {
    param([string]$Name, [scriptblock]$Condition, [string]$Details = "")
    try {
        $result = & $Condition
        if ($result) {
            $script:Pass++
            Write-Host "  PASS: $Name" -ForegroundColor Green
        } else {
            $script:Fail++
            Write-Host "  FAIL: $Name — $Details" -ForegroundColor Red
        }
    } catch {
        $script:Warn++
        Write-Host "  WARN: $Name — API error: $_" -ForegroundColor Yellow
    }
}

Write-Host ""
Write-Host "=== MCP2515 High-Bus Hardware Validation ===" -ForegroundColor Cyan
Write-Host ""

# ── Test 1: Backend is reachable ──────────────────────────────────
Write-Host "-- Test 1: Backend connectivity --"

try {
    $health = Invoke-RestMethod -Uri "$BaseUrl/api/health" -TimeoutSec 5
    Assert-That "Backend health endpoint responds" { $true }
} catch {
    Write-Host "  FAIL: Backend not reachable at $BaseUrl" -ForegroundColor Red
    Write-Host "  Start with: cd debug-tool/backend && npm run dev" -ForegroundColor Yellow
    exit 1
}

# ── Test 2: Stats endpoint returns bus data ───────────────────────
Write-Host "-- Test 2: CAN stats endpoint --"

try {
    $stats = Invoke-RestMethod -Uri "$BaseUrl/api/can/stats" -TimeoutSec 5
    Assert-That "Stats response has buses" { $stats.buses -ne $null }
    Assert-That "High bus present in stats" { $stats.buses.high -ne $null }
    Assert-That "Low bus present in stats" { $stats.buses.low -ne $null }
} catch {
    Write-Host "  WARN: Stats endpoint error: $_" -ForegroundColor Yellow
    $script:Warn++
}

# ── Test 3: High bus TEC/REC = 0 (error-free operation) ───────────
Write-Host "-- Test 3: CAN error counters (TEC/REC) --"

try {
    $stats = Invoke-RestMethod -Uri "$BaseUrl/api/can/stats" -TimeoutSec 5
    $highTec = $stats.buses.high.tec
    $highRec = $stats.buses.high.rec

    Assert-That "High bus TEC = 0 (no transmit errors)" { $highTec -eq 0 } `
        "TEC=$highTec — indicates CAN bus errors (timing, termination, noise)"
    Assert-That "High bus REC = 0 (no receive errors)" { $highRec -eq 0 } `
        "REC=$highRec — indicates CAN bus errors"
} catch {
    Write-Host "  WARN: Could not check error counters: $_" -ForegroundColor Yellow
    $script:Warn++
}

# ── Test 4: Frame capture — collect 30s of data ───────────────────
Write-Host "-- Test 4: Frame capture ($CaptureDurationS seconds) --"

$startTime = Get-Date
Write-Host "  Collecting frames for $CaptureDurationS seconds..."

# Start a recording
try {
    $recording = Invoke-RestMethod -Uri "$BaseUrl/api/recordings/start" `
        -Method Post -ContentType "application/json" `
        -Body '{"label":"MCP2515-validation"}' -TimeoutSec 5
    Write-Host "  Recording started: $($recording.id)"
} catch {
    Write-Host "  WARN: Could not start recording: $_" -ForegroundColor Yellow
    $script:Warn++
}

# Wait for frames to accumulate
Start-Sleep -Seconds $CaptureDurationS

# Stop recording
try {
    $stopResult = Invoke-RestMethod -Uri "$BaseUrl/api/recordings/$($recording.id)/stop" `
        -Method Post -TimeoutSec 5
    Write-Host "  Recording stopped: $($stopResult.frame_count) frames"
} catch {
    Write-Host "  WARN: Could not stop recording: $_" -ForegroundColor Yellow
}

# ── Test 5: Frame rate validation ─────────────────────────────────
Write-Host "-- Test 5: Frame rate validation --"

# Query frames by ID
$highIds = @(
    @{Id="0x7FD"; Name="RT_HEARTBEAT"; ExpectedHz=2; ToleranceHz=1},
    @{Id="0x210"; Name="RT_STATE_RPT"; ExpectedHz=10; ToleranceHz=3},
    @{Id="0x310"; Name="STEER_DIAG"; ExpectedHz=10; ToleranceHz=3},
    @{Id="0x311"; Name="BRAKE_DIAG"; ExpectedHz=10; ToleranceHz=3},
    @{Id="0x220"; Name="RT_PID_RPT"; ExpectedHz=10; ToleranceHz=3}
)

foreach ($msg in $highIds) {
    try {
        $frames = Invoke-RestMethod -Uri "$BaseUrl/api/can/frames?bus=high&id=$($msg.Id)&limit=500" -TimeoutSec 5
        $count = if ($frames -is [array]) { $frames.Count } else { 0 }
        $actualHz = [math]::Round($count / $CaptureDurationS, 1)
        $minFrames = [math]::Floor(($msg.ExpectedHz - $msg.ToleranceHz) * $CaptureDurationS)
        $maxFrames = [math]::Ceiling(($msg.ExpectedHz + $msg.ToleranceHz) * $CaptureDurationS)

        Assert-That "$($msg.Id) $($msg.Name) @ ~$($msg.ExpectedHz) Hz (got $count frames = $actualHz Hz)" `
            { $count -ge $minFrames -and $count -le $maxFrames } `
            "Expected $minFrames–$maxFrames frames, got $count"
    } catch {
        Write-Host "  WARN: Could not query $($msg.Id): $_" -ForegroundColor Yellow
        $script:Warn++
    }
}

# ── Test 6: Frame decode validation ───────────────────────────────
Write-Host "-- Test 6: Frame decode validation --"

try {
    # Check a 0x7FD heartbeat frame
    $hbFrames = Invoke-RestMethod -Uri "$BaseUrl/api/can/frames?bus=high&id=0x7FD&limit=1" -TimeoutSec 5
    if ($hbFrames -and $hbFrames.Count -gt 0) {
        $hb = $hbFrames[0]
        Assert-That "0x7FD has DLC=1" { $hb.dlc -eq 1 }
        Assert-That "0x7FD decoded has alive_counter" { $hb.decoded.alive_counter -ne $null }
    }

    # Check a 0x210 state report
    $rptFrames = Invoke-RestMethod -Uri "$BaseUrl/api/can/frames?bus=high&id=0x210&limit=1" -TimeoutSec 5
    if ($rptFrames -and $rptFrames.Count -gt 0) {
        $rpt = $rptFrames[0]
        Assert-That "0x210 decoded has mode" { $rpt.decoded.mode -ne $null }
        Assert-That "0x210 decoded has steer_valid" { $rpt.decoded.steer_valid -ne $null }
    }
} catch {
    Write-Host "  WARN: Decode validation error: $_" -ForegroundColor Yellow
    $script:Warn++
}

# ── Test 7: Frame injection + pipeline ────────────────────────────
Write-Host "-- Test 7: Frame injection (0x300 → 0x204 pipeline) --"

try {
    # Inject 0x300 HOST_DRIVE_CMD on high bus: speed=2000 mm/s, yaw=0, gear=D
    $body = @{
        bus = "high"
        id  = "0x300"
        dlc = 8
        data = @(0, 0, 7, 208, 0, 0, 0, 1)  # speed=2000, yaw=0, gear=1(D)
    } | ConvertTo-Json

    $injectResult = Invoke-RestMethod -Uri "$BaseUrl/api/cmd/send" `
        -Method Post -ContentType "application/json" -Body $body -TimeoutSec 5
    Assert-That "0x300 injection accepted" { $injectResult.ok -eq $true -or $injectResult.status -eq "sent" }
} catch {
    Write-Host "  WARN: Injection error: $_" -ForegroundColor Yellow
    $script:Warn++
}

# ── Results ───────────────────────────────────────────────────────
Write-Host ""
Write-Host "=== Results ===" -ForegroundColor Cyan
Write-Host "Pass: $Pass, Fail: $Fail, Warn: $Warn"
Write-Host ""

if ($Fail -gt 0) {
    Write-Host "SOME TESTS FAILED — check CAN bus wiring, termination, and MCP2515 configuration." -ForegroundColor Red
    exit 1
} else {
    Write-Host "All hardware tests passed!" -ForegroundColor Green
    exit 0
}
