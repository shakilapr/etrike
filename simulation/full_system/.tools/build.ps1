# Build etrike_sim with LLVM-MinGW clang++ (the repo's C++17 toolchain; the
# default g++ on PATH is MinGW 6.3 which lacks <string_view>).
$ErrorActionPreference = 'Stop'

$root  = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$sim   = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$out   = Join-Path $sim 'build'
New-Item -ItemType Directory -Force -Path $out | Out-Null

$clang = (Get-Command clang++).Source

& $clang -std=c++17 -O0 -g -Wall -Wextra `
    -I $root `
    -I (Join-Path $root 'rt-esp32\src') `
    -I $sim `
    (Join-Path $sim 'scenarios\main.cpp') `
    (Join-Path $root 'rt-esp32\src\core\rt_core.cpp') `
    -o (Join-Path $out 'etrike_sim.exe')

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& (Join-Path $out 'etrike_sim.exe')
exit $LASTEXITCODE
