$ErrorActionPreference = "Stop"

Write-Host "Cleaning build matrix..."
$folders = @("rt-esp32", "sys-esp32", "native-test/build")

foreach ($folder in $folders) {
    $pioDir = Join-Path -Path $folder -ChildPath ".pio"
    if (Test-Path $pioDir) {
        Write-Host "Removing $pioDir"
        Remove-Item -Recurse -Force $pioDir -ErrorAction SilentlyContinue
    }
    
    $buildDir = Join-Path -Path $folder -ChildPath "build"
    if (Test-Path $buildDir) {
        Write-Host "Removing $buildDir"
        Remove-Item -Recurse -Force $buildDir -ErrorAction SilentlyContinue
    }
}

Write-Host "Clean complete."
