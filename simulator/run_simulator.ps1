param(
    [string]$BuildDir = "build/simulator",
    [string]$Config = "Debug",
    [ValidateSet("Auto", "Simulator", "SimHub")]
    [string]$Telemetry = "Auto",
    [int]$Width = 800,
    [int]$Height = 400,
    [int]$Scale = 1,
    [int]$Jobs = 0,
    [int]$WebPort = 8765,
    [switch]$NoWeb,
    [switch]$NoBrowser,
    [switch]$NoLedBar,
    [int]$LedBarHeight = 92,
    [string]$CaptureFramePath = "",
    [string]$UiActions = "",
    [switch]$ExitOnCapture,
    [switch]$Fresh,
    [switch]$NoRun
)

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$defaultBuildDir = $BuildDir

function Resolve-ToolchainBin {
    $candidates = @()

    if ($env:RPMCOUNTER_TOOLCHAIN_BIN) {
        $candidates += $env:RPMCOUNTER_TOOLCHAIN_BIN
    }

    $candidates += @(
        (Join-Path $repoRoot ".tools\llvm-mingw\llvm-mingw-20260407-ucrt-x86_64\bin"),
        (Join-Path $env:TEMP "WinGet\MartinStorsjo.LLVM-MinGW.UCRT.22.1.3-20260407\extracted\llvm-mingw-20260407-ucrt-x86_64\bin"),
        (Join-Path $env:TEMP "WinGet\BrechtSanders.WinLibs.POSIX.UCRT.15.2.0-14.0.0-r7\extracted\mingw64\bin")
    )

    foreach ($candidate in $candidates) {
        if (-not [string]::IsNullOrWhiteSpace($candidate)) {
            $gcc = Join-Path $candidate "x86_64-w64-mingw32-gcc.exe"
            $gxx = Join-Path $candidate "x86_64-w64-mingw32-g++.exe"
            $make = Join-Path $candidate "mingw32-make.exe"
            if ((Test-Path $gcc) -and (Test-Path $gxx) -and (Test-Path $make)) {
                return $candidate
            }

            $gcc = Join-Path $candidate "gcc.exe"
            $gxx = Join-Path $candidate "g++.exe"
            if ((Test-Path $gcc) -and (Test-Path $gxx) -and (Test-Path $make)) {
                return $candidate
            }
        }
    }

    return $null
}

function Resolve-MinGwCompilerTriple {
    param([string]$BinPath)

    $tripledGcc = Join-Path $BinPath "x86_64-w64-mingw32-gcc.exe"
    $tripledGxx = Join-Path $BinPath "x86_64-w64-mingw32-g++.exe"
    if ((Test-Path $tripledGcc) -and (Test-Path $tripledGxx)) {
        return @{
            CCompiler = $tripledGcc
            CxxCompiler = $tripledGxx
        }
    }

    return @{
        CCompiler = (Join-Path $BinPath "gcc.exe")
        CxxCompiler = (Join-Path $BinPath "g++.exe")
    }
}

if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "cmake wurde nicht gefunden."
    exit 1
}

$toolchainBin = Resolve-ToolchainBin
$generator = $null
$generatorArgs = @()
$exePath = $null

if ($toolchainBin) {
    $compiler = Resolve-MinGwCompilerTriple -BinPath $toolchainBin
    $env:PATH = "$toolchainBin;$env:PATH"
    $generator = "MinGW Makefiles"
    $generatorArgs = @(
        "-D", "CMAKE_BUILD_TYPE=$Config",
        "-D", "CMAKE_C_COMPILER=$($compiler.CCompiler)",
        "-D", "CMAKE_CXX_COMPILER=$($compiler.CxxCompiler)",
        "-D", "CMAKE_MAKE_PROGRAM=$(Join-Path $toolchainBin 'mingw32-make.exe')"
    )
    if (-not $PSBoundParameters.ContainsKey('BuildDir') -or $defaultBuildDir -eq "build/simulator") {
        $BuildDir = "build/simulator-local"
    }
}
elseif ((Get-Command cl -ErrorAction SilentlyContinue) -or (Get-Command msbuild -ErrorAction SilentlyContinue)) {
    $generator = "Visual Studio 17 2022"
    $generatorArgs = @("-A", "x64")
    if (-not $PSBoundParameters.ContainsKey('BuildDir') -or $defaultBuildDir -eq "build/simulator") {
        $BuildDir = "build/simulator-vs"
    }
}
else {
    Write-Error "Kein passender C/C++-Compiler gefunden. Erwartet wurde ein repo-lokales LLVM-MinGW unter .tools\\llvm-mingw oder eine vorhandene Visual-Studio-/MinGW-Toolchain."
    exit 1
}

$buildPath = Join-Path $repoRoot $BuildDir
$cachePath = Join-Path $buildPath "CMakeCache.txt"

if ($Jobs -le 0) {
    $Jobs = [Math]::Max([Environment]::ProcessorCount, 1)
}

$needsConfigure = $Fresh -or -not (Test-Path $cachePath)
if (-not $needsConfigure) {
    $cacheGeneratorLine = Select-String -Path $cachePath -Pattern '^CMAKE_GENERATOR:INTERNAL=' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($cacheGeneratorLine) {
        $cachedGenerator = ($cacheGeneratorLine.Line -split '=', 2)[1]
        if ($cachedGenerator -ne $generator) {
            Write-Host "Build cache uses generator '$cachedGenerator' instead of '$generator' - reconfiguring fresh."
            $needsConfigure = $true
        }
    }
}

Write-Host "Preparing simulator build with generator '$generator'"
if ($toolchainBin) {
    Write-Host "Using local MinGW toolchain: $toolchainBin"
}

if ($needsConfigure) {
    if ($Fresh) {
        Write-Host "Running fresh configure..."
        cmake --fresh -S $repoRoot -B $buildPath -G $generator @generatorArgs
    }
    else {
        Write-Host "Configuring build directory..."
        cmake -S $repoRoot -B $buildPath -G $generator @generatorArgs
    }
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
else {
    Write-Host "Reusing existing CMake cache at $buildPath"
}

Write-Host "Building rpmcounter_simulator with $Jobs job(s)..."
cmake --build $buildPath --config $Config --target rpmcounter_simulator --parallel $Jobs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

if (-not $NoRun)
{
    switch ($Telemetry) {
        "Simulator" {
            $env:SIM_MODE = "true"
            $env:SIM_ALLOW_FALLBACK_SIMULATOR = "false"
        }
        "SimHub" {
            $env:SIM_MODE = "false"
            $env:SIM_ALLOW_FALLBACK_SIMULATOR = "false"
        }
        default {
            $env:SIM_MODE = "false"
            $env:SIM_ALLOW_FALLBACK_SIMULATOR = "true"
        }
    }
    if (-not $env:SIMHUB_SOURCE) { $env:SIMHUB_SOURCE = "http" }
    if (-not $env:SIMHUB_HTTP_PORT) { $env:SIMHUB_HTTP_PORT = "8888" }
    if (-not $env:SIMHUB_UDP_PORT) { $env:SIMHUB_UDP_PORT = "20888" }
    if (-not $env:SIM_DEBUG_TELEMETRY) { $env:SIM_DEBUG_TELEMETRY = "false" }
    $env:SIM_WINDOW_WIDTH = [string]$Width
    $env:SIM_WINDOW_HEIGHT = [string]$Height
    $env:SIM_WINDOW_SCALE = [string]$Scale
    $env:SIM_WEB_PORT = [string]$WebPort
    $env:SIM_WEB_ENABLED = if ($NoWeb) { "false" } else { "true" }
    $env:SIM_SHOW_LED_BAR = if ($NoLedBar) { "false" } else { "true" }
    $env:SIM_LED_BAR_HEIGHT = [string]$LedBarHeight
    if ($CaptureFramePath) { $env:SIM_CAPTURE_FRAME_PATH = $CaptureFramePath } else { Remove-Item Env:SIM_CAPTURE_FRAME_PATH -ErrorAction SilentlyContinue }
    if ($UiActions) { $env:SIM_UI_ACTIONS = $UiActions } else { Remove-Item Env:SIM_UI_ACTIONS -ErrorAction SilentlyContinue }
    if ($ExitOnCapture) { $env:SIM_EXIT_ON_CAPTURE = "true" } else { Remove-Item Env:SIM_EXIT_ON_CAPTURE -ErrorAction SilentlyContinue }

    Write-Host "Starting simulator with TELEMETRY=$Telemetry SIM_MODE=$($env:SIM_MODE) SIMHUB_SOURCE=$($env:SIMHUB_SOURCE) SIMHUB_HTTP_PORT=$($env:SIMHUB_HTTP_PORT) SIMHUB_UDP_PORT=$($env:SIMHUB_UDP_PORT) SIM_ALLOW_FALLBACK_SIMULATOR=$($env:SIM_ALLOW_FALLBACK_SIMULATOR) SIM_DEBUG_TELEMETRY=$($env:SIM_DEBUG_TELEMETRY) WINDOW=$($env:SIM_WINDOW_WIDTH)x$($env:SIM_WINDOW_HEIGHT) SCALE=$($env:SIM_WINDOW_SCALE) WEB_PORT=$($env:SIM_WEB_PORT) WEB_ENABLED=$($env:SIM_WEB_ENABLED) LED_BAR=$($env:SIM_SHOW_LED_BAR)"
    if ($env:SIM_CAPTURE_FRAME_PATH)
    {
        Write-Host "Capture frame path: $($env:SIM_CAPTURE_FRAME_PATH) EXIT_ON_CAPTURE=$($env:SIM_EXIT_ON_CAPTURE) UI_ACTIONS=$($env:SIM_UI_ACTIONS)"
    }
    if ($toolchainBin) {
        $exePath = Join-Path $buildPath "rpmcounter_simulator.exe"
    }
    else {
        $exePath = Join-Path $buildPath "$Config\rpmcounter_simulator.exe"
    }
    $process = Start-Process -FilePath $exePath -PassThru
    if (-not $NoWeb -and -not $NoBrowser -and -not $CaptureFramePath -and -not $ExitOnCapture) {
        Start-Sleep -Milliseconds 900
        $dashboardUrl = "http://127.0.0.1:$WebPort/"
        Write-Host "Opening dashboard: $dashboardUrl"
        Start-Process $dashboardUrl | Out-Null
    }
    $process.WaitForExit()
    exit $process.ExitCode
}
