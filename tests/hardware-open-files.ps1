param([Parameter(Mandatory)][string]$Exe, [Parameter(Mandatory)][ValidatePattern('^[a-zA-Z]$')][string]$Drive,
      [Parameter(Mandatory)][string]$ExpectedLabel)
$ErrorActionPreference = 'Stop'
$exePath = (Resolve-Path -LiteralPath $Exe).Path
$target = $Drive + ':'
$devices = & $exePath --list --json | ConvertFrom-Json
$match = @($devices | Where-Object { $_.driveLetters -contains $target -and $_.volumeLabel -ceq $ExpectedLabel -and $_.bus -eq 'USB' })
if ($match.Count -ne 1 -or (Get-Volume -DriveLetter $Drive).FileSystemLabel -cne $ExpectedLabel) { throw 'USB identity changed' }
$name = 'USB-Switch-' + [char]0x6D4B + [guid]::NewGuid() + '.tmp'
$path = Join-Path ($target + '\') $name
$other = Join-Path ([IO.Path]::GetTempPath()) $name
if ([IO.Path]::GetPathRoot($other) -eq [IO.Path]::GetPathRoot($path)) { throw 'Negative control requires another disk' }
function Hold([string]$file) {
    [IO.FileStream]::new($file, [IO.FileMode]::CreateNew, [IO.FileAccess]::ReadWrite,
        [IO.FileShare]::Read, 4096, [IO.FileOptions]::DeleteOnClose)
}
$file = Hold $path
try {
    $control = Hold $other
    try {
        $r = & $exePath --eject $match[0].instanceId --files --json | ConvertFrom-Json
        $p = @($r.fileInspection | Where-Object pid -eq $PID)
        if ($LASTEXITCODE -ne 1 -or $r.status -ne 'vetoed' -or $p.Count -ne 1 -or
            $p[0].paths -notcontains $path -or $p[0].paths -contains $other) {
            throw "Missing target file or cross-volume match (inspection status: $($p.status))"
        }
        $file.WriteByte(1); $file.Flush($true)
    } finally { $control.Dispose() }
} finally { $file.Dispose() }
Write-Host 'PASS: exact Unicode file and holder PID found; other-volume file excluded and target still writable.'
