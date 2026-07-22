# Stops the Control Toolkit backend API (port 8001) and frontend UI (port 5173)
#
# Run from monorepo root OR anywhere:
#   pwsh -File control-toolkit/scripts/stop.ps1

$ErrorActionPreference = "Continue"

Write-Host "Stopping Control Toolkit processes..."

foreach ($port in 8001, 5173) {
  $pids = (Get-NetTCPConnection -LocalPort $port -State Listen -ErrorAction SilentlyContinue).OwningProcess | Select-Object -Unique
  foreach ($id in $pids) {
    if ($id) {
      Write-Host "Stopping PID $id (listening on port $port)"
      Stop-Process -Id $id -Force -ErrorAction SilentlyContinue
    }
  }
}

Write-Host "Done."
