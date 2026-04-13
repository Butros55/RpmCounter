param(
    [string]$BuildDir = "build/simulator",
    [string]$Config = "Debug",
    [switch]$NoRun
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$buildPath = Join-Path $repoRoot $BuildDir

cmake -S $repoRoot -B $buildPath -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build $buildPath --config $Config --target rpmcounter_simulator
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $NoRun)
{
    if (-not $env:SIM_MODE) { $env:SIM_MODE = "false" }
    if (-not $env:SIMHUB_SOURCE) { $env:SIMHUB_SOURCE = "http" }
    if (-not $env:SIMHUB_HTTP_PORT) { $env:SIMHUB_HTTP_PORT = "8888" }
    if (-not $env:SIMHUB_UDP_PORT) { $env:SIMHUB_UDP_PORT = "20888" }
    if (-not $env:SIM_ALLOW_FALLBACK_SIMULATOR) { $env:SIM_ALLOW_FALLBACK_SIMULATOR = "false" }
    if (-not $env:SIM_DEBUG_TELEMETRY) { $env:SIM_DEBUG_TELEMETRY = "false" }

    Write-Host "Starting simulator with SIM_MODE=$($env:SIM_MODE) SIMHUB_SOURCE=$($env:SIMHUB_SOURCE) SIMHUB_HTTP_PORT=$($env:SIMHUB_HTTP_PORT) SIMHUB_UDP_PORT=$($env:SIMHUB_UDP_PORT) SIM_ALLOW_FALLBACK_SIMULATOR=$($env:SIM_ALLOW_FALLBACK_SIMULATOR) SIM_DEBUG_TELEMETRY=$($env:SIM_DEBUG_TELEMETRY)"
    if ($env:SIM_CAPTURE_FRAME_PATH)
    {
        Write-Host "Capture frame path: $($env:SIM_CAPTURE_FRAME_PATH) EXIT_ON_CAPTURE=$($env:SIM_EXIT_ON_CAPTURE)"
    }
    $exePath = Join-Path $buildPath "$Config\rpmcounter_simulator.exe"
    & $exePath
    exit $LASTEXITCODE
}
