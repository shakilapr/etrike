param(
    [switch]$Quick,
    [switch]$Native,
    [switch]$Python,
    [switch]$Sim,
    [switch]$Pio,
    [switch]$CompileDb
)

if ($CompileDb) {
    python "$PSScriptRoot/generate_compile_commands.py"
    exit $LASTEXITCODE
}

$argsList = @()
if ($Quick) { $argsList += "--quick" }
if ($Native) { $argsList += "--native" }
if ($Python) { $argsList += "--python" }
if ($Sim) { $argsList += "--sim" }
if ($Pio) { $argsList += "--pio" }

python "$PSScriptRoot/run_all_tests.py" @argsList
exit $LASTEXITCODE
