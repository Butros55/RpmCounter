param(
    [string]$SerialPort = $env:SHIFTLIGHT_USB_PORT,
    [switch]$OpenStartupFolder
)

$startupFolder = [Environment]::GetFolderPath('Startup')
$shortcutPath = Join-Path $startupFolder 'ShiftLight USB Bridge.lnk'
$repoRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $PSScriptRoot 'run_usb_sim_bridge.ps1'

if (!(Test-Path $runner)) {
    throw "Bridge runner not found: $runner"
}

$argList = @('-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$runner`"")
if ($SerialPort) {
    $argList += @('-SerialPort', $SerialPort)
}
$argList += '-Debug'

$wsh = New-Object -ComObject WScript.Shell
$shortcut = $wsh.CreateShortcut($shortcutPath)
$shortcut.TargetPath = (Get-Command pwsh).Source
$shortcut.Arguments = ($argList -join ' ')
$shortcut.WorkingDirectory = $repoRoot
$shortcut.WindowStyle = 7
$shortcut.IconLocation = "$env:SystemRoot\System32\shell32.dll,220"
$shortcut.Description = 'Starts the ShiftLight USB bridge on Windows logon.'
$shortcut.Save()

Write-Host "Autostart shortcut written to:" -ForegroundColor Cyan
Write-Host "  $shortcutPath"
Write-Host
Write-Host "Ab dem naechsten Windows-Login startet die USB-Bridge automatisch." -ForegroundColor Green
Write-Host "Dann reicht typischerweise: SimHub starten, ESP per USB anstecken." -ForegroundColor Green

if ($OpenStartupFolder) {
    Start-Process explorer.exe $startupFolder
}
