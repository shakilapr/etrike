param(
    [switch]$Quick,
    [switch]$Native,
    [switch]$Python,
    [switch]$Sim,
    [switch]$Pio
)

$argsList = @()
if ($Quick) { $argsList += "--quick" }
if ($Native) { $argsList += "--native" }
if ($Python) { $argsList += "--python" }
if ($Sim) { $argsList += "--sim" }
if ($Pio) { $argsList += "--pio" }

python "$PSScriptRoot/run_all_tests.py" @argsList
exit $LASTEXITCODE
