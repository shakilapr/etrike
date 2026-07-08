$ErrorActionPreference = "Stop"

Write-Host "Running TS Coverage..."

Push-Location "simulation"
try {
    npm run test -- --coverage
} finally {
    Pop-Location
}
Write-Host "TS coverage passed."
exit 0
