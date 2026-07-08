$ErrorActionPreference = "Stop"

Write-Host "Building all PlatformIO environments..."

$pioDirs = @("rt-esp32", "sys-esp32")
$envs = @("native", "vehicle")

foreach ($dir in $pioDirs) {
    Push-Location $dir
    try {
        foreach ($env in $envs) {
            Write-Host "Building $dir for $env"
            pio run -e $env
        }
    } finally {
        Pop-Location
    }
}
Write-Host "All PIO environments built successfully."
