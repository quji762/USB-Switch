# For an external USB reader and two test cards with distinct, nonempty labels.
# Default is read-only. -RunSwap explicitly requests a media eject.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z]$')][string]$Drive,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$FirstLabel,
    [Parameter(Mandatory)][ValidateNotNullOrEmpty()][string]$SecondLabel,
    [switch]$RunSwap
)
$ErrorActionPreference = 'Stop'
if ($FirstLabel -eq $SecondLabel) { throw 'Use two cards with distinct volume labels.' }
$bin = (Resolve-Path -LiteralPath $Exe).Path
function Inventory {
    $raw = & $bin --list --json | Out-String
    if ($LASTEXITCODE -ne 0) { throw 'USB-Switch inventory failed.' }
    ConvertFrom-Json -InputObject $raw
}
function ReaderOf([string]$diskId) {
    $parent = (Get-PnpDeviceProperty -InstanceId $diskId -KeyName DEVPKEY_Device_Parent).Data
    if (!$parent -or $parent -notlike 'USB\*') { throw 'Expected a USB storage transport parent.' }
    $service = (Get-PnpDeviceProperty -InstanceId $parent -KeyName DEVPKEY_Device_Service).Data
    if ($service -notin @('USBSTOR','UASPStor')) { throw "Unsupported reader transport: $service" }
    return $parent
}
function Arrival([string]$reader) {
    $node = Get-PnpDevice -InstanceId $reader -PresentOnly
    if ($node.Status -ne 'OK') { throw 'Reader is no longer present and operational.' }
    $stamp = (Get-PnpDeviceProperty -InstanceId $reader -KeyName DEVPKEY_Device_LastArrivalDate).Data
    if ($stamp -isnot [datetime]) { throw 'Reader arrival timestamp unavailable; cannot verify continuity.' }
    return $stamp.ToUniversalTime().Ticks
}
$target = $Drive + ':'
$first = @((Inventory) | Where-Object { $_.driveLetters -contains $target })
if ($first.Count -ne 1 -or $first[0].driverMediaRemovable -ne $true) { throw 'Expected exactly one removable test medium at the specified letter.' }
$volume = Get-Volume -DriveLetter $Drive
if ($volume.FileSystemLabel -cne $FirstLabel -or !$volume.UniqueId) { throw 'First card volume does not match.' }
$reader = ReaderOf $first[0].instanceId
$stamp = Arrival $reader
if (!$RunSwap) { Write-Output 'Preflight passed. No media request was sent.'; return }
$result = & $bin --eject-media $first[0].instanceId --json | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $result.status -ne 'media_ejected' -or $result.instanceId -ne $first[0].instanceId) {
    throw "Media request failed: $($result | ConvertTo-Json -Compress)"
}
# Check the volume itself, independent of any change of drive letter.
$deadline = [datetime]::UtcNow.AddSeconds(15)
do {
    if ((Arrival $reader) -ne $stamp) { throw 'The reader re-enumerated during media eject.' }
    $stillMounted = @(Get-Volume | Where-Object { $_.UniqueId -eq $volume.UniqueId -and $_.FileSystemLabel -ceq $FirstLabel })
    if (!$stillMounted.Count) { break }
    Start-Sleep -Milliseconds 400
} while ([datetime]::UtcNow -lt $deadline)
if ($stillMounted.Count) { throw 'Driver accepted the request but the first volume is still reported.' }
Write-Host "Leave the reader connected. Remove the first card and insert '$SecondLabel' within 120 seconds."
$deadline = [datetime]::UtcNow.AddSeconds(120)
do {
    if ((Arrival $reader) -ne $stamp) { throw 'The reader disconnected or re-enumerated during the swap.' }
    foreach ($v in @(Get-Volume | Where-Object { $_.FileSystemLabel -ceq $SecondLabel -and $_.DriveLetter })) {
        $letter = [string]$v.DriveLetter + ':'
        $found = @((Inventory) | Where-Object { $_.driveLetters -contains $letter })
        if ($found.Count -ne 1 -or (ReaderOf $found[0].instanceId) -ne $reader) { continue }
        if (!$v.UniqueId -or $v.UniqueId -eq $volume.UniqueId) { throw 'Cannot distinguish the replacement volume.' }
        [void][System.IO.Directory]::GetFileSystemEntries(($letter + '\'))
        if ((Arrival $reader) -ne $stamp) { throw 'Reader changed before the final read.' }
        Write-Output "PASS: first volume went away, same reader remained online, and replacement card is readable at $letter"
        return
    }
    Start-Sleep -Milliseconds 500
} while ([datetime]::UtcNow -lt $deadline)
throw 'No readable replacement card was verified on the original reader before timeout.'
