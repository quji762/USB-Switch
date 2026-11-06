param(
    [string]$Installer = '',
    [string]$LegacyInstaller = ''
)
$ErrorActionPreference = 'Stop'
if (!$Installer) {
    $version = (Get-Item (Join-Path $PSScriptRoot '..\dist\usb-switch.exe')).VersionInfo.ProductVersion
    $Installer = Join-Path $PSScriptRoot "..\dist\installer\usb-switch-setup-$version.0.exe"
}
$installerPath = (Resolve-Path -LiteralPath $Installer).Path
$runKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{88DB2805-7CF6-449D-9C44-A6239B4A86CD}_is1'
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('usb-switch-install-test-' + [guid]::NewGuid())
$installDir = Join-Path $testRoot 'application'
$ownCommand = '"' + (Join-Path $installDir 'usb-switch.exe') + '"'
$foreignCommand = '"C:\unrelated-program.exe"'

# This integration test uses the real current-user registry. Never overwrite
# an existing installation or startup entry belonging to the user.
if (Test-Path -LiteralPath $uninstallKey) { throw 'An existing USB-Switch installation was found; test skipped.' }
$run = Get-ItemProperty -LiteralPath $runKey -ErrorAction SilentlyContinue
foreach ($name in @('USB-Switch', 'QuickEject')) {
    if ($null -ne $run -and $null -ne $run.PSObject.Properties[$name]) { throw "Existing startup entry $name; test skipped." }
}
if (Get-Process usb-switch,quickeject -ErrorAction SilentlyContinue) { throw 'Application is running; test skipped.' }

function Install-TestApp {
    $process = Start-Process -FilePath $installerPath -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', '/CURRENTUSER', "/DIR=`"$installDir`"") -Wait -PassThru
    if ($process.ExitCode -ne 0 -or !(Test-Path -LiteralPath (Join-Path $installDir 'usb-switch.exe'))) { throw "Installation failed: $($process.ExitCode)" }
    $installedHash = (Get-FileHash -LiteralPath (Join-Path $installDir 'usb-switch.exe')).Hash
    $buildHash = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot '..\dist\usb-switch.exe')).Hash
    if ($installedHash -ne $buildHash) { throw 'Installer payload differs from the tested executable' }
    $license = Join-Path $installDir 'LICENSE'
    if (!(Test-Path -LiteralPath $license) -or (Get-FileHash $license).Hash -ne (Get-FileHash (Join-Path $PSScriptRoot '..\LICENSE')).Hash) { throw 'Installed license is missing or differs from source' }
}
function Uninstall-TestApp {
    $uninstaller = Join-Path $installDir 'unins000.exe'
    $process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait -PassThru
    if ($process.ExitCode -ne 0) { throw "Uninstall failed: $($process.ExitCode)" }
}
try {
    New-Item -ItemType Directory -Path $testRoot | Out-Null
    $oldExe = Join-Path $installDir 'quickeject.exe'
    if ($LegacyInstaller) {
        $legacyInstallerPath = (Resolve-Path -LiteralPath $LegacyInstaller).Path
        $legacyProcess = Start-Process -FilePath $legacyInstallerPath -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', "/DIR=`"$installDir`"") -Wait -PassThru
        if ($legacyProcess.ExitCode -ne 0 -or !(Test-Path -LiteralPath $oldExe)) { throw 'Legacy installation failed' }
    } else {
        Install-TestApp
        # Without an old installer, simulate its installed executable.
        Copy-Item -LiteralPath (Join-Path $installDir 'usb-switch.exe') -Destination $oldExe
    }
    $legacyCommand = '"' + $oldExe + '"'
    New-ItemProperty -LiteralPath $runKey -Name 'QuickEject' -PropertyType String -Value $legacyCommand | Out-Null
    # Emulate the legacy tray process's mutex: silent upgrade must stop safely.
    $legacyMutex = [Threading.Mutex]::new($false, 'Local\QuickEject')
    try {
        $blocked = Start-Process -FilePath $installerPath -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/NOICONS', '/CURRENTUSER', "/DIR=`"$installDir`"") -PassThru
        if (!$blocked.WaitForExit(15000)) { $blocked.Kill(); throw 'Installer did not stop when the application was running' }
        if ($blocked.ExitCode -eq 0 -or !(Test-Path -LiteralPath $oldExe)) { throw 'Upgrade proceeded while the old application was running' }
    } finally {
        $legacyMutex.Dispose()
    }
    Install-TestApp
    if (Test-Path -LiteralPath $oldExe) { throw 'Legacy executable survived upgrade' }
    if ((Get-ItemProperty -LiteralPath $runKey).QuickEject) { throw 'Legacy startup entry survived upgrade' }
    if ((Get-ItemProperty -LiteralPath $runKey).'USB-Switch' -ne $ownCommand) { throw 'Startup entry was not migrated' }
    Uninstall-TestApp
    if ((Get-ItemProperty -LiteralPath $runKey).PSObject.Properties['USB-Switch']) { throw 'Owned startup entry survived uninstall' }
    if (Test-Path -LiteralPath $uninstallKey) { throw 'Uninstall registration survived' }
    if (Test-Path -LiteralPath (Join-Path $installDir 'usb-switch.exe')) { throw 'Executable survived uninstall' }

    Install-TestApp
    New-ItemProperty -LiteralPath $runKey -Name 'USB-Switch' -PropertyType String -Value $foreignCommand | Out-Null
    Copy-Item -LiteralPath (Join-Path $installDir 'usb-switch.exe') -Destination $oldExe
    New-ItemProperty -LiteralPath $runKey -Name 'QuickEject' -PropertyType String -Value $legacyCommand | Out-Null
    Install-TestApp
    if ((Get-ItemProperty -LiteralPath $runKey).'USB-Switch' -ne $foreignCommand) { throw 'Upgrade changed an unrelated startup entry' }
    if ((Get-ItemProperty -LiteralPath $runKey).QuickEject -ne $legacyCommand) { throw 'Upgrade deleted legacy startup despite a conflict' }
    Uninstall-TestApp
    if ((Get-ItemProperty -LiteralPath $runKey).'USB-Switch' -ne $foreignCommand) { throw 'Uninstall changed an unrelated startup entry' }
    if ((Get-ItemProperty -LiteralPath $runKey).QuickEject) { throw 'Owned legacy startup entry survived uninstall' }
    Write-Host "Install, upgrade, uninstall and startup ownership checks passed (real legacy installer: $([bool]$LegacyInstaller))"
} finally {
    $current = (Get-ItemProperty -LiteralPath $runKey -ErrorAction SilentlyContinue).'USB-Switch'
    if ($current -eq $ownCommand -or $current -eq $foreignCommand) { Remove-ItemProperty -LiteralPath $runKey -Name 'USB-Switch' }
    $legacy = (Get-ItemProperty -LiteralPath $runKey -ErrorAction SilentlyContinue).QuickEject
    if ($legacy -eq ('"' + (Join-Path $installDir 'quickeject.exe') + '"')) { Remove-ItemProperty -LiteralPath $runKey -Name 'QuickEject' }
    $uninstaller = Join-Path $installDir 'unins000.exe'
    if (Test-Path -LiteralPath $uninstaller) { Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART') -Wait | Out-Null }
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
