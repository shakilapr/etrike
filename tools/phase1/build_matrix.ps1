$ErrorActionPreference = "Stop"

Write-Host "Building Matrix..."

# ESP32 projects matrix
$pioDirs = @("rt-esp32", "sys-esp32")
$envs = @("native", "vehicle")

foreach ($dir in $pioDirs) {
    Push-Location $dir
    try {
        foreach ($env in $envs) {
            Write-Host "Building $dir for $env"
            pio run -e $env
            if ($LASTEXITCODE -ne 0) {
                Write-Error "Build failed for $dir - $env"
                exit 1
            }
        }
    } finally {
        Pop-Location
    }
}

# Native C++ Tests Matrix
Push-Location "native-test"
try {
    Write-Host "Building native tests"
    cmake -B build
    cmake --build build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Native test build failed"
        exit 1
    }
} finally {
    Pop-Location
}

# Simulation Matrix
Push-Location "simulation"
try {
    Write-Host "Building simulation"
    npm install
    npm run build
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Simulation build failed"
        exit 1
    }
} finally {
    Pop-Location
}

Write-Host "Build Matrix complete."
