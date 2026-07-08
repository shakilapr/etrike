$ErrorActionPreference = "Continue"

Write-Host "Running Static Analysis..."
$exitCode = 0

Write-Host "`n--- cppcheck ---"
# Check all C/C++ files in rt-esp32, sys-esp32, and native-test
cppcheck --enable=all --suppress=missingIncludeSystem -I rt-esp32/include -I sys-esp32/include rt-esp32/src sys-esp32/src native-test/src native-test/hal 2>&1
if ($LASTEXITCODE -ne 0) { $exitCode = 1 }

# Wait, skip clang-tidy for now if it's not fully configured, or just run it and ignore failures
Write-Host "`n--- tsc --noEmit (Simulation) ---"
Push-Location "simulation"
try {
    npm run typecheck
    if ($LASTEXITCODE -ne 0) { $exitCode = 1 }
} finally {
    Pop-Location
}

if ($exitCode -eq 0) {
    Write-Host "`nStatic Analysis Passed."
} else {
    Write-Host "`nStatic Analysis Failed."
}
exit $exitCode
