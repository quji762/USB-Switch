param(
    [Parameter(Mandatory)][string]$Exe,
    [Parameter(Mandatory)][ValidatePattern('^[A-Za-z]$')][string]$Drive,
    [Parameter(Mandatory)][string]$ExpectedLabel
)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$target = $Drive + ':'
$root = $target + '\'
$inventory = & $exePath --list --json | ConvertFrom-Json
$match = @($inventory | Where-Object { $_.driveLetters -contains $target -and $_.volumeLabel -ceq $ExpectedLabel })
if ($match.Count -ne 1 -or !$match[0].driverMediaRemovable) { throw 'Expected one removable test medium' }
$id = $match[0].instanceId
$file = Join-Path $root ('usb-switch-busy-' + [guid]::NewGuid() + '.tmp')
$hold = [IO.FileStream]::new($file, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite,
    [IO.FileShare]::Read, 4096, [IO.FileOptions]::DeleteOnClose)
try {
    $result = & $exePath --eject-media $id --json | ConvertFrom-Json
    if ($LASTEXITCODE -ne 1 -or $result.instanceId -ne $id -or $result.status -ne 'lock_volume' -or !$result.win32Error) {
        throw "Expected lock refusal, got $($result | ConvertTo-Json -Compress)"
    }
    $hold.WriteByte(1)
    $hold.Flush($true)
} finally { $hold.Dispose() }
if (!(Test-Path -LiteralPath $root) -or (Test-Path -LiteralPath $file)) { throw 'Drive or cleanup check failed' }
Write-Host 'Occupied volume refused media eject and remained writable.'
