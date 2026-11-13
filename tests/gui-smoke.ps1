param([string]$Exe = (Join-Path $PSScriptRoot '..\dist\usb-switch.exe'))
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
if (Get-Process usb-switch -ErrorAction SilentlyContinue) { throw 'USB-Switch is already running; GUI test skipped.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class UsbSwitchGuiTest {
    [DllImport("user32.dll", CharSet=CharSet.Unicode, ExactSpelling=true)]
    public static extern IntPtr FindWindowW(string cls, string title);
    [DllImport("user32.dll")]
    public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll")]
    public static extern bool PostMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32.dll")]
    public static extern IntPtr GetDlgItem(IntPtr hwnd, int id);
    [DllImport("user32.dll")]
    public static extern IntPtr SendMessageW(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
}
'@
$savedAppData = $env:APPDATA
$savedLocalAppData = $env:LOCALAPPDATA
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('usb-switch-gui-test-' + [guid]::NewGuid())
$process = $null
$second = $null
function Find-TestDialog([string]$title) {
    for ($i = 0; $i -lt 100; $i++) {
        $dialog = [UsbSwitchGuiTest]::FindWindowW('#32770', $title)
        [uint32]$dialogPid = 0
        [void][UsbSwitchGuiTest]::GetWindowThreadProcessId($dialog, [ref]$dialogPid)
        if ($dialogPid -eq $process.Id) { return $dialog }
        Start-Sleep -Milliseconds 50
    }
    throw "Test process did not open $title"
}
try {
    $env:APPDATA = Join-Path $testRoot 'roaming'
    $env:LOCALAPPDATA = Join-Path $testRoot 'local'
    $configDir = New-Item -ItemType Directory -Force (Join-Path $env:APPDATA 'USB-Switch')
    $config = Join-Path $configDir.FullName 'settings.json'
    [IO.File]::WriteAllText($config, '{"language":"en"}')
    $process = Start-Process -FilePath $exePath -ArgumentList '--settings' -PassThru
    $window = [IntPtr]::Zero
    for ($i = 0; $i -lt 100; $i++) {
        $window = [UsbSwitchGuiTest]::FindWindowW('USBSwitchTrayWindow', 'USB-Switch')
        if ($window -ne [IntPtr]::Zero -or $process.HasExited) { break }
        Start-Sleep -Milliseconds 100
    }
    if ($window -eq [IntPtr]::Zero) { throw 'Tray window did not appear' }
    [uint32]$owner = 0
    [void][UsbSwitchGuiTest]::GetWindowThreadProcessId($window, [ref]$owner)
    if ($owner -ne $process.Id) { throw 'Tray window belongs to another process' }
    $dialog = Find-TestDialog 'USB-Switch Settings'
    $combo = [UsbSwitchGuiTest]::GetDlgItem($dialog, 405)
    $keys = [UsbSwitchGuiTest]::GetDlgItem($dialog, 408)
    if ([UsbSwitchGuiTest]::SendMessageW($keys, 0x147, [IntPtr]::Zero, [IntPtr]::Zero) -ne [IntPtr]21) { throw 'Expected default hotkey U' }
    if ([UsbSwitchGuiTest]::SendMessageW($keys, 0x14E, [IntPtr]10, [IntPtr]::Zero) -ne [IntPtr]10) { throw 'Hotkey selection failed' }
    if ([UsbSwitchGuiTest]::SendMessageW($combo, 0x14E, [IntPtr]1, [IntPtr]::Zero) -ne [IntPtr]1) { throw 'Language selection failed' }
    [void][UsbSwitchGuiTest]::SendMessageW($dialog, 0x111, [IntPtr]1, [IntPtr]::Zero)
    if ((Get-Content $config -Raw | ConvertFrom-Json).language -ne 'zh') { throw 'Language change was not saved' }
    if ((Get-Content $config -Raw | ConvertFrom-Json).menuHotkey -ne 'J') { throw 'Hotkey change was not saved' }
    [void][UsbSwitchGuiTest]::PostMessageW($window, 0x312, [IntPtr]1, [IntPtr]0x004A0003)
    $menuOpened = $false
    for ($i = 0; $i -lt 50; $i++) {
        $popup = [UsbSwitchGuiTest]::FindWindowW('#32768', $null)
        [uint32]$popupPid = 0
        [void][UsbSwitchGuiTest]::GetWindowThreadProcessId($popup, [ref]$popupPid)
        if ($popupPid -eq $process.Id) { $menuOpened = $true; break }
        Start-Sleep -Milliseconds 50
    }
    if (!$menuOpened) { throw 'Hotkey message did not open the device menu' }
    [void][UsbSwitchGuiTest]::PostMessageW($window, 0x1F, [IntPtr]::Zero, [IntPtr]::Zero)
    $second = Start-Process -FilePath $exePath -ArgumentList '--settings' -PassThru
    if (!$second.WaitForExit(10000) -or $second.ExitCode -ne 0 -or $process.HasExited) { throw 'Second-instance handling failed' }
    $dialog = Find-TestDialog ('USB-Switch ' + [char]0x8BBE + [char]0x7F6E)
    [void][UsbSwitchGuiTest]::SendMessageW($dialog, 0x111, [IntPtr]2, [IntPtr]::Zero)
    $logDir = Join-Path $env:LOCALAPPDATA 'USB-Switch\logs'
    $target = 'USB-Switch-unused-private-target'
    & $exePath --eject $target | Out-Null
    if ($LASTEXITCODE -ne 2) { throw 'Expected unknown target during CLI logging check' }
    $cliLog = Get-Content -Raw -LiteralPath (Join-Path $logDir 'usb-switch-cli.log')
    if ($cliLog -notmatch 'starting \(CLI\)' -or $cliLog.Contains($target)) { throw 'CLI logging failed while tray was running or retained raw arguments' }
    if (!(Test-Path -LiteralPath (Join-Path $logDir 'usb-switch.log'))) { throw 'Tray log missing' }
    # The second instance cannot append to the tray's usb-switch.log - the tray
    # holds it with FILE_SHARE_READ only - so it must fall back to a per-process
    # file and leave its single-instance notice there. The name carries that
    # process's own PID, which is why it is matched instead of spelled out.
    $secondLog = Join-Path $logDir "usb-switch.$($second.Id).log"
    if (!(Test-Path -LiteralPath $secondLog)) { throw 'Second instance did not write a per-process log' }
    $secondLogText = Get-Content -Raw -LiteralPath $secondLog
    if ($secondLogText -notmatch 'starting \(tray\)') { throw 'Second instance log is missing its startup line' }
    if ($secondLogText -notmatch 'another instance is already running') { throw 'Second instance log is missing the single-instance notice' }
    $otherLogs = Get-ChildItem -LiteralPath $logDir -Filter 'usb-switch.*.log' -File | Where-Object { $_.FullName -ne $secondLog }
    if ($otherLogs) { throw ("Unexpected per-process log(s): " + ($otherLogs.Name -join ', ')) }
    if (![UsbSwitchGuiTest]::PostMessageW($window, 0x10, [IntPtr]::Zero, [IntPtr]::Zero)) { throw 'Could not request window close' }
    if (!$process.WaitForExit(10000) -or $process.ExitCode -ne 0) { throw 'Application did not exit cleanly' }
    Write-Host 'GUI startup, single-instance handling and graceful shutdown passed'
} finally {
    $env:APPDATA = $savedAppData
    $env:LOCALAPPDATA = $savedLocalAppData
    if ($second -and !$second.HasExited) { Stop-Process -Id $second.Id -Force }
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
# --eject of an unknown target exits 2 on purpose. In-process callers,
# including Actions' pwsh shell, treat a leftover $LASTEXITCODE as this
# script's result. A failure above throws and never reaches this line.
$global:LASTEXITCODE = 0
