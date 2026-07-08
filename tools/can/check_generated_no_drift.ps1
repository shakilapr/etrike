param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = "Stop"

$required = @(
    @{ id = "0x001"; name = "SAFETY_ESTOP"; dlc = 0; bus = "both" },
    @{ id = "0x110"; name = "SYS_MODE_CMD"; dlc = 1; bus = "low" },
    @{ id = "0x011"; name = "SYS_SAFETY_STS"; dlc = 3; bus = "both" },
    @{ id = "0x210"; name = "RT_STATE_RPT"; dlc = 6; bus = "high" },
    @{ id = "0x204"; name = "RT_DRIVE_CMD"; dlc = 5; bus = "low" },
    @{ id = "0x205"; name = "RT_BRAKE_CMD"; dlc = 4; bus = "low" },
    @{ id = "0x300"; name = "HOST_DRIVE_CMD"; dlc = 8; bus = "high" },
    @{ id = "0x301"; name = "HOST_BRAKE_REQ"; dlc = 4; bus = "high" },
    @{ id = "0x169"; name = "VCU_SES_REQ"; dlc = 8; bus = "low" },
    @{ id = "0x201"; name = "SES_STATUS"; dlc = 8; bus = "low" },
    @{ id = "0x7B9"; name = "VCU_SEB_REQ"; dlc = 8; bus = "low" },
    @{ id = "0x721"; name = "SEB_STATUS"; dlc = 8; bus = "low" },
    @{ id = "0x7FD"; name = "RT_HEARTBEAT"; dlc = 2; bus = "both" },
    @{ id = "0x7FE"; name = "SYS_HEARTBEAT"; dlc = 2; bus = "low" },
    @{ id = "0x7FC"; name = "HOST_HEARTBEAT"; dlc = 1; bus = "high" }
)

$idsTs = Join-Path $RepoRoot "shared/can/generated/can_ids.ts"
$protocol = Join-Path $RepoRoot "shared/can/can_protocol.h"
$yamlText = (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "shared/can/can_high.yaml")) + "`n" +
    (Get-Content -Raw -LiteralPath (Join-Path $RepoRoot "shared/can/can_low.yaml"))
$idsText = Get-Content -Raw -LiteralPath $idsTs
$protocolText = Get-Content -Raw -LiteralPath $protocol

$failures = New-Object System.Collections.Generic.List[string]

foreach ($frame in $required) {
    $id = $frame.id
    $name = $frame.name
    $dlc = $frame.dlc
    $bus = $frame.bus

    if ($yamlText -notmatch "id:\s*$id\b") {
        $failures.Add("$id missing from YAML contract") | Out-Null
    }
    if ($idsText -notmatch "`"$id`":\s*$dlc") {
        $failures.Add("$id generated DLC mismatch; expected $dlc") | Out-Null
    }
    if ($idsText -notmatch "`"$id`":\s*`"$bus`"") {
        $failures.Add("$id generated bus mismatch; expected $bus") | Out-Null
    }
    if ($idsText -notmatch "`"$id`":\s*`"$([regex]::Escape($name))`"") {
        $failures.Add("$id generated name mismatch; expected $name") | Out-Null
    }
    $idNoPrefix = $id -replace '^0x', ''
    if ($protocolText -notmatch "0x$idNoPrefix\b") {
        $failures.Add("$id missing from can_protocol.h") | Out-Null
    }
}

if ($failures.Count -gt 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

"CAN generated contract has no drift for $($required.Count) required frames"
