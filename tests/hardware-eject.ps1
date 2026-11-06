# Explicit hardware acceptance: ejects only the named test disk after a verified veto.
param(
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z]:?$')][string]$Drive,
    [Parameter(Mandatory)][string]$VolumeLabel,
    [string]$Exe = (Join-Path $PSScriptRoot '..\dist\usb-switch.exe'),
    [switch]$PreflightOnly
)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$letter = $Drive.TrimEnd(':').ToUpperInvariant()
$driveName = "${letter}:"
function Get-Inventory {
    $json = & $exePath --list --json | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'USB inventory failed' }
    $json | ConvertFrom-Json | ForEach-Object { $_ }
}
function Get-TestDevice {
    $volume = Get-Volume -DriveLetter $letter
    $disk = Get-Partition -DriveLetter $letter | Get-Disk
    if (@($disk).Count -ne 1 -or $disk.BusType -ne 'USB' -or $disk.IsBoot -or $disk.IsSystem -or $disk.IsReadOnly -or $volume.FileSystemLabel -cne $VolumeLabel) {
        throw 'Windows disk or volume identity does not match a writable non-system USB test disk'
    }
    $found = @(Get-Inventory | Where-Object { $_.driveLetters -contains $driveName })
    if ($found.Count -ne 1 -or $found[0].bus -ne 'USB' -or !$found[0].ejectable -or !$found[0].instanceId -or $found[0].volumeLabel -cne $VolumeLabel) {
        throw 'Application inventory does not match the requested test disk'
    }
    $found[0]
}
$roaming = $env:APPDATA
$local = $env:LOCALAPPDATA
$testRoot = Join-Path ([IO.Path]::GetTempPath()) ('usb-switch-hardware-' + [guid]::NewGuid())
try {
    $env:APPDATA = Join-Path $testRoot 'roaming'
    $env:LOCALAPPDATA = Join-Path $testRoot 'local'
    $device = Get-TestDevice
    Write-Host "Verified $($device.displayName)"
    if ($PreflightOnly) { return }
    $probe = Join-Path ("${driveName}\") ('usb-switch-test-' + [guid]::NewGuid() + '.tmp')
    $handle = [IO.FileStream]::new($probe, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::None, 4096, [IO.FileOptions]::DeleteOnClose)
    try {
        $handle.WriteByte(1)
        $handle.Flush($true)
        if ((Get-TestDevice).instanceId -cne $device.instanceId) { throw 'Device changed before veto check' }
        $result = & $exePath --eject $device.instanceId --json | Out-String
        if ($LASTEXITCODE -ne 1) { throw "Expected a rejected eject while file was open: $result" }
        $report = $result | ConvertFrom-Json
        if ($report.status -ne 'vetoed' -or $report.instanceId -cne $device.instanceId) { throw "Unexpected eject response: $result" }
        Write-Host 'Windows vetoed removal with the probe open'
    } finally { $handle.Dispose() }
    if ((Get-TestDevice).instanceId -cne $device.instanceId -or (Test-Path -LiteralPath $probe)) {
        throw 'Device changed or probe still present; stopped before final eject'
    }
    # Volume filters can briefly veto a second query-remove after the probe closes.
    # Retry only a fresh PnP veto against the original identity, for at most three attempts.
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        $same = @(Get-Inventory | Where-Object { $_.instanceId -ceq $device.instanceId -and $_.driveLetters -contains $driveName })
        if ($same.Count -ne 1) { throw 'Original device no longer present before retry' }
        $result = & $exePath --eject $device.instanceId --json | Out-String
        $code = $LASTEXITCODE
        $report = $result | ConvertFrom-Json
        if ($code -eq 0 -and $report.status -eq 'ejected' -and $report.instanceId -ceq $device.instanceId) { break }
        if ($attempt -eq 3 -or $code -ne 1 -or $report.status -ne 'vetoed' -or $report.instanceId -cne $device.instanceId -or $report.retryable -ne $true) {
            throw "Removal did not succeed: $result"
        }
        Write-Host "Windows still vetoed removal after close; retrying ($attempt/3)"
        Start-Sleep -Seconds 2
    }
    for ($i = 0; $i -lt 20; $i++) {
        if (!@(Get-Inventory | Where-Object instanceId -CEQ $device.instanceId).Count -and
            !(Test-Path -LiteralPath ("${driveName}\"))) {
            Write-Host 'Released probe disappeared; eject succeeded; instance and drive letter disappeared'
            return
        }
        Start-Sleep -Milliseconds 250
    }
    throw 'Windows accepted eject but the instance or drive letter remained visible'
} finally {
    $env:APPDATA = $roaming
    $env:LOCALAPPDATA = $local
    if (Test-Path -LiteralPath $testRoot) {
        $resolved = (Resolve-Path -LiteralPath $testRoot).Path
        if ([IO.Path]::GetDirectoryName($resolved) -eq [IO.Path]::GetTempPath().TrimEnd('\') -and
            [IO.Path]::GetFileName($resolved) -match '^usb-switch-hardware-[0-9a-f-]{36}$') {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
