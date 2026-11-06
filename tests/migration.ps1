param([string]$Exe = (Join-Path $PSScriptRoot '..\dist\usb-switch.exe'))
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path $Exe).Path
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ("usb-switch-test-" + [guid]::NewGuid())
$oldAppData = $env:APPDATA
$oldLocalAppData = $env:LOCALAPPDATA
try {
    $env:APPDATA = Join-Path $testRoot 'roaming'
    $env:LOCALAPPDATA = Join-Path $testRoot 'local'
    $legacy = Join-Path $env:APPDATA 'QuickEject\settings.json'
    $current = Join-Path $env:APPDATA 'USB-Switch\settings.json'
    New-Item -ItemType Directory -Force -Path (Split-Path $legacy) | Out-Null
    [IO.File]::WriteAllText($legacy, '{"language":"en","showNotifications":false}')
    & $exePath --version | Out-Null
    if ($LASTEXITCODE -ne 0 -or !(Test-Path $current)) { throw 'Legacy settings were not migrated' }
    $settings = Get-Content $current -Raw | ConvertFrom-Json
    if ($settings.language -ne 'en' -or $settings.showNotifications -ne $false) { throw 'Migrated values differ' }
    [IO.File]::WriteAllText($current, '{"language":"zh"}')
    & $exePath --version | Out-Null
    if ((Get-Content $current -Raw | ConvertFrom-Json).language -ne 'zh') { throw 'Current settings were overwritten' }
    [IO.File]::WriteAllText($current, 'invalid json')
    & $exePath --version | Out-Null
    if ((Get-Content $current -Raw) -ne 'invalid json') { throw 'Invalid settings were overwritten' }
    Write-Host 'Settings migration checks passed'
} finally {
    $env:APPDATA = $oldAppData
    $env:LOCALAPPDATA = $oldLocalAppData
    if (Test-Path -LiteralPath $testRoot) { Remove-Item -LiteralPath $testRoot -Recurse -Force }
}
