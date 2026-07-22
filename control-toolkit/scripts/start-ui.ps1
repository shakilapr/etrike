# Start Control Toolkit frontend (Vite) on 127.0.0.1:5173
# Proxies /api → API address (default http://127.0.0.1:8001)
#
# Run from monorepo root OR anywhere:
#   pwsh -File control-toolkit/scripts/start-ui.ps1
#   .\control-toolkit\scripts\start-ui.ps1

$ErrorActionPreference = "Stop"
$ToolkitRoot = Split-Path $PSScriptRoot -Parent
$FrontendRoot = Join-Path $ToolkitRoot "frontend"

$uiHost = if ($env:CTK_UI_HOST) { $env:CTK_UI_HOST } else { "127.0.0.1" }
$uiPort = if ($env:CTK_UI_PORT) { [int]$env:CTK_UI_PORT } else { 5173 }
$api = if ($env:CTK_E2E_API) { $env:CTK_E2E_API } else { "http://127.0.0.1:8001" }

$env:CTK_E2E_API = $api

Write-Host "Control Toolkit UI"
Write-Host "  host     : $uiHost"
Write-Host "  port     : $uiPort"
Write-Host "  frontend : $FrontendRoot"
Write-Host "  API proxy: $api  (CTK_E2E_API)"
Write-Host "  open     : http://${uiHost}:${uiPort}/"
Write-Host ""

Set-Location $FrontendRoot
if (-not (Test-Path ".\node_modules")) {
  Write-Host "node_modules missing — running npm install..."
  npm install
}

npx vite --host $uiHost --port $uiPort --strictPort
