param(
    [string]$RepoRoot = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)),
    [string]$ExpectedVersion = "1.0"
)

$ErrorActionPreference = "Stop"

$highYaml = Join-Path $RepoRoot "shared/can/can_high.yaml"
$lowYaml = Join-Path $RepoRoot "shared/can/can_low.yaml"
$protocol = Join-Path $RepoRoot "shared/can/can_protocol.h"

if (-not (Test-Path -LiteralPath $highYaml)) { throw "Missing $highYaml" }
if (-not (Test-Path -LiteralPath $lowYaml)) { throw "Missing $lowYaml" }
if (-not (Test-Path -LiteralPath $protocol)) { throw "Missing $protocol" }

$highText = Get-Content -Raw -LiteralPath $highYaml
if ($highText -notmatch 'can_version:\s*"([^"]+)"') {
    throw "shared/can/can_high.yaml does not define can_version"
}

$actualVersion = $Matches[1]
if ($actualVersion -ne $ExpectedVersion) {
    throw "CAN protocol version mismatch: expected $ExpectedVersion, got $actualVersion"
}

"CAN protocol version $actualVersion"
