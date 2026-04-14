param(
    [string]$SerialPort = $env:SHIFTLIGHT_USB_PORT,
    [string]$SimHubHost = $(if ($env:SIMHUB_HOST) { $env:SIMHUB_HOST } else { "127.0.0.1" }),
    [int]$SimHubPort = $(if ($env:SIMHUB_PORT) { [int]$env:SIMHUB_PORT } else { 8888 }),
    [int]$WebPort = $(if ($env:SHIFTLIGHT_USB_WEB_PORT) { [int]$env:SHIFTLIGHT_USB_WEB_PORT } else { 8765 }),
    [switch]$Debug
)

$repoRoot = Split-Path -Parent $PSScriptRoot
$scriptPath = Join-Path $PSScriptRoot "usb_sim_bridge.py"

$args = @($scriptPath, "--simhub-host", $SimHubHost, "--simhub-port", "$SimHubPort", "--web-port", "$WebPort")
if ($SerialPort) {
    $args += @("--serial-port", $SerialPort)
}
if ($Debug) {
    $args += "--debug"
}

Write-Host "Starting ShiftLight USB bridge on http://127.0.0.1:$WebPort" -ForegroundColor Cyan
Write-Host "SimHub source: http://$SimHubHost`:$SimHubPort" -ForegroundColor DarkGray
if ($SerialPort) {
    Write-Host "Forced serial port: $SerialPort" -ForegroundColor DarkGray
} else {
    Write-Host "Serial port: auto-detect" -ForegroundColor DarkGray
}

$existingBridge = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match 'usb_sim_bridge.py' }
if ($existingBridge) {
    Write-Host "Stopping previous USB bridge instance..." -ForegroundColor Yellow
    $existingBridge | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Milliseconds 600
}

$pioWorkers = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object {
        $_.CommandLine -match 'platformio.exe.*--target monitor' -or
        $_.CommandLine -match 'platformio.exe.*--target upload' -or
        $_.CommandLine -match 'pio device monitor' -or
        $_.CommandLine -match 'esptool'
    }
if ($pioWorkers) {
    Write-Host "Stopping active PlatformIO/Upload serial workers..." -ForegroundColor Yellow
    $pioWorkers | ForEach-Object { Stop-Process -Id $_.ProcessId -Force -ErrorAction SilentlyContinue }
    Start-Sleep -Seconds 2
}

if ($SerialPort) {
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        $modeOutput = & cmd /c "mode $SerialPort" 2>&1
        if ($LASTEXITCODE -eq 0) {
            break
        }
        Start-Sleep -Milliseconds 700
    }
}

Set-Location $repoRoot
python @args
