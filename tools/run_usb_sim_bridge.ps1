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

$serialMonitor = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue |
    Where-Object { $_.Name -eq 'python.exe' -and $_.CommandLine -match 'platformio.exe.*--target monitor' }
if ($serialMonitor) {
    Write-Warning "PlatformIO Serial Monitor laeuft noch und blockiert oft COM-Ports. Bitte schliessen, falls USB getrennt oder RPC timeout angezeigt wird."
}

Set-Location $repoRoot
python @args
