# Start Control Toolkit backend on 127.0.0.1:8001
# Run from monorepo root OR control-toolkit OR anywhere:
#   pwsh -File control-toolkit/scripts/start-api.ps1
#   .\control-toolkit\scripts\start-api.ps1

$ErrorActionPreference = "Stop"
$ToolkitRoot = Split-Path $PSScriptRoot -Parent
$BackendRoot = Join-Path $ToolkitRoot "backend"

# Monorepo root (for protocol package path resolution is handled in protocol_bridge)
$RepoRoot = Split-Path $ToolkitRoot -Parent

$hostAddr = if ($env:CTK_HOST) { $env:CTK_HOST } else { "127.0.0.1" }
$port = if ($env:CTK_PORT) { [int]$env:CTK_PORT } else { 8001 }

Write-Host "Control Toolkit API"
Write-Host "  host     : $hostAddr"
Write-Host "  port     : $port"
Write-Host "  backend  : $BackendRoot"
Write-Host "  repo     : $RepoRoot"
Write-Host "  status   : http://${hostAddr}:${port}/api/v1/status"
Write-Host "  docs     : http://${hostAddr}:${port}/docs"
Write-Host "  stream   : ws://${hostAddr}:${port}/api/v1/stream"
Write-Host ""

Set-Location $BackendRoot
if (Test-Path ".\.venv\Scripts\python.exe") {
  $py = (Resolve-Path ".\.venv\Scripts\python.exe").Path
} else {
  $py = "python"
}

& $py -m uvicorn control_toolkit.main:app --host $hostAddr --port $port
