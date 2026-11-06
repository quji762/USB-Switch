<#
.SYNOPSIS
    Builds and runs the unit tests (tests/unit.cpp).

.DESCRIPTION
    The tests cover the JSON parser (including the nesting-depth guard that
    prevents a stack overflow on a corrupted settings file) and the i18n
    formatter (including the single-pass placeholder substitution).

.EXAMPLE
    .\tools\run-tests.ps1
    .\tools\run-tests.ps1 -ToolChain 'C:\mingw64\bin'
#>
[CmdletBinding()]
param(
    [string]$ToolChain = ''
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$genDir = Join-Path $root 'build\generated'
$outExe = Join-Path $root 'build\tests\unit.exe'

function Find-Tool {
    param([string]$Name, [string]$Directory)
    if ($Directory) {
        $candidate = Join-Path $Directory $Name
        if (Test-Path $candidate) { return $candidate }
    }
    $command = Get-Command $Name -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    return $null
}

$gxx = Find-Tool 'g++.exe' $ToolChain
if (-not $gxx) {
    Write-Host 'g++ was not found. Pass -ToolChain <mingw64\bin>.' -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path (Split-Path $outExe) | Out-Null

# Include dirs mirror the main build: src/ for the project headers and
# build/generated/ for the CMake-generated AppInfo.h.
$includeDirs = @((Join-Path $root 'src'))
$appInfo = Join-Path $genDir 'app\AppInfo.h'
if (Test-Path $appInfo) { $includeDirs += $genDir }

& $gxx -std=c++17 -O1 -g -Wall -Wextra `
    -DWIN32_LEAN_AND_MEAN -DNOMINMAX -DUNICODE -D_UNICODE `
    -DWINVER=0x0A00 -D_WIN32_WINNT=0x0A00 -D_CRT_SECURE_NO_WARNINGS `
    ($includeDirs | ForEach-Object { "-I$_" }) `
    -o $outExe (Join-Path $root 'tests\unit.cpp') `
    (Join-Path $root 'src\util\Json.cpp') `
    (Join-Path $root 'src\util\Str.cpp') `
    (Join-Path $root 'src\i18n\I18n.cpp') `
    (Join-Path $root 'src\i18n\Strings.cpp') `
    -lsetupapi -lcfgmgr32 -lcomctl32 -lole32 -ladvapi32 -luser32 -lgdi32
if ($LASTEXITCODE -ne 0) { throw "test build failed ($LASTEXITCODE)" }

Write-Host "built $outExe" -ForegroundColor Cyan
& $outExe
$code = $LASTEXITCODE
Write-Host ''
if ($code -eq 0) { Write-Host 'ALL TESTS PASSED' -ForegroundColor Green }
else { Write-Host "TESTS FAILED (exit $code)" -ForegroundColor Red }
exit $code
