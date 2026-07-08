$ErrorActionPreference = "Stop"

Write-Host "Running Native Tests with ASan/UBSan..."

# This requires CMake configuration with -DUSE_SANITIZERS=ON
Push-Location "native-test"
try {
    cmake -B build-asan -DUSE_SANITIZERS=ON
    cmake --build build-asan
    Push-Location "build-asan"
    ctest --output-on-failure
    Pop-Location
} finally {
    Pop-Location
}

Write-Host "Sanitizers check passed."
