param(
    [ValidateSet("release", "debug", "test")]
    [string]$Mode = "release",
    [string]$ConfigPath = (Join-Path $PSScriptRoot "config.json"),
    [string]$OutputDirectory = $PSScriptRoot,
    [string]$Python = "python"
)

$ErrorActionPreference = "Stop"
$configuration = if ($Mode -eq "release") { "Release" } else { "Debug" }
$buildDirectory = Join-Path $PSScriptRoot "build/windows/$Mode"
$pythonExecutable = & $Python -c "import sys; print(sys.executable)"
if ($LASTEXITCODE -ne 0) { throw "Cannot locate Python. Pass -Python with an interpreter path." }

# MSVC Debug tests use its checked runtime. ASan/UBSan and TSan are separate
# Clang/GCC configurations; those options remain off in this Windows wrapper.
& cmake -S $PSScriptRoot -B $buildDirectory `
    "-DCMAKE_BUILD_TYPE=$configuration" `
    "-DPython3_EXECUTABLE=$pythonExecutable" `
    -DBUILD_TESTING=ON -DCOLLATZ_WARNINGS_AS_ERRORS=ON `
    -DCOLLATZ_SANITIZERS=OFF -DCOLLATZ_TSAN=OFF
if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed." }
& cmake --build $buildDirectory --config $configuration --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed." }

if ($Mode -eq "test") {
    & ctest --test-dir $buildDirectory -C $configuration --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "Tests failed." }
} elseif ($Mode -eq "debug") {
    Write-Host "Built Debug in $buildDirectory"
} else {
    $candidates = @(
        (Join-Path $buildDirectory "$configuration/collatz_run.exe"),
        (Join-Path $buildDirectory "collatz_run.exe")
    )
    $binary = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    if (-not $binary) { throw "Built executable was not found in $buildDirectory" }
    & $binary $ConfigPath $OutputDirectory
    if ($LASTEXITCODE -ne 0) { throw "Calculation failed." }
}
