# Build the RT-AURIX-Lite board target headlessly (no AURIX Studio GUI).
#
# Proves (Phase D0):
#   1. The HighTec tricore-gcc toolchain + TC37x iLLD register headers +
#      LQFP-176 pin maps compile and link (test_d0_smoke).
#   2. The portable rta:: core (src/) compiles with tricore-elf-g++ (C++17).
#
# Usage:
#   pwsh board/port/build.ps1
#
# Outputs (gitignored):
#   board/port/build/*.elf, *.o

$ErrorActionPreference = "Stop"
$port = $PSScriptRoot                       # rt-aurix-lite/board/port/
$board = Split-Path -Parent $port           # rt-aurix-lite/board/
$root  = Split-Path -Parent $board          # rt-aurix-lite/
$repo  = Split-Path -Parent $root           # etrike/
$ide   = "E:\Infineon\AURIX-Studio-1.10.36"
$gcc   = "$ide\tools\Compilers\tricore-gcc11\bin\tricore-elf-gcc.exe"
$gxx   = "$ide\tools\Compilers\tricore-gcc11\bin\tricore-elf-g++.exe"
$lib   = "$board\Libraries"                 # iLLD (gitignored)
$cfg   = "$port\Configurations"
$out   = "$port\build"
$src   = "$root\src"
$gen   = "$root\protocol\generated\cpp"

if (-not (Test-Path $gcc))   { throw "HighTec tricore-gcc not found: $gcc (set IDE path)" }
if (-not (Test-Path $lib))   { throw "iLLD not extracted: $lib (see board/README.md)" }

New-Item -ItemType Directory -Path $out -Force | Out-Null

# ── iLLD include roots (mirrors what AURIX Studio passes) ───────────
$illd_incs = @(
  "-I$lib\iLLD\TC3xx\Tricore",
  "-I$lib\Infra\Sfr\TC37x",
  "-I$lib\Infra\Platform",
  "-I$lib\Service\CpuGeneric",
  "-I$lib\iLLD\TC3xx\Tricore\_PinMap",
  "-I$lib\iLLD\TC3xx\Tricore\_PinMap\TC37x",
  "-I$cfg"
)

# ── 1. C smoke test (iLLD SFR + pinmap compile/link) ───────────────
Write-Host "== C smoke: test_d0_smoke =="
$pinmap_c = "$lib\iLLD\TC3xx\Tricore\_PinMap\TC37x\IfxCan_PinMap_TC37x_LQFP176.c"
&  $gcc @("-mcpu=tc38xx","-std=gnu11","-Wall","-ffunction-sections","-fdata-sections") `
       @illd_incs `
       "$board\port\test\test_d0_smoke.c" $pinmap_c `
       "-o" "$out\d0_smoke.elf" "-Wl,--gc-sections"
if ($LASTEXITCODE -ne 0) { throw "C smoke test failed" }
Write-Host "  OK: $out\d0_smoke.elf"

# ── 2. C++ portable core (rta:: logic, no iLLD dep) ────────────────
Write-Host "== C++ portable core =="
$cpp_incs = @("-I$src", "-I$gen", "-I$repo\shared", "-I$repo")
$cpp_flags = @("-mcpu=tc38xx","-std=gnu++17","-c","-Wall","-fno-exceptions","-fno-rtti")
$cpp_srcs = @(
  "$src\domain\kinematics.cpp",
  "$src\domain\steering.cpp",
  "$src\domain\brake.cpp",
  "$src\domain\mode.cpp",
  "$src\domain\safety.cpp",
  "$src\app\controllers.cpp"
)
foreach ($s in $cpp_srcs) {
  $name = Split-Path -Leaf $s
  $obj  = "$out\" + ($name -replace '\.cpp$','.o')
  & $gxx @cpp_flags @cpp_incs $s "-o" $obj
  if ($LASTEXITCODE -ne 0) { throw "C++ compile failed: $name" }
  Write-Host "  OK: $name"
}

# ── 3. Board HAL over iLLD (C++, uses iLLD CAN/Port/Stm) ─────────────
Write-Host "== Board HAL (aurix_hal.cpp) =="
$hal_incs = @(
  "-I$lib\iLLD\TC3xx\Tricore",
  "-I$lib\iLLD\TC3xx\Tricore\Can\Std",
  "-I$lib\iLLD\TC3xx\Tricore\Can\Can",
  "-I$lib\iLLD\TC3xx\Tricore\Port\Std",
  "-I$lib\iLLD\TC3xx\Tricore\Stm\Std",
  "-I$lib\iLLD\TC3xx\Tricore\Cpu\Std",
  "-I$lib\iLLD\TC3xx\Tricore\Scu\Std",
  "-I$lib\Infra\Sfr\TC37x",
  "-I$lib\Infra\Platform",
  "-I$lib\Service\CpuGeneric",
  "-I$lib\iLLD\TC3xx\Tricore\_PinMap",
  "-I$lib\iLLD\TC3xx\Tricore\_PinMap\TC37x",
  "-I$cfg",
  "-I$root",
  "-I$src",
  "-I$gen",
  "-I$repo\shared",
  "-I$repo"
)
& $gxx @("-mcpu=tc38xx","-std=gnu++17","-c","-Wall","-fno-exceptions","-fno-rtti","-Wno-register") `
       @hal_incs `
       "$board\hal_aurix\aurix_hal.cpp" "-o" "$out\aurix_hal.o"
if ($LASTEXITCODE -ne 0) { throw "Board HAL compile failed" }
Write-Host "  OK: aurix_hal.o"

Write-Host "`nBuild OK. Artifacts in $out"
