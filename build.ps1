<#
.SYNOPSIS
    Builds USB-Switch and collects the release artifacts in dist\.

.DESCRIPTION
    Detects the available toolchain in this order when -Toolchain is Auto:
      1. Visual Studio (through vswhere) + Windows SDK  -> MSVC, x64
      2. cl.exe already on PATH (developer prompt)       -> MSVC
      3. gcc.exe on PATH (MSYS2/MinGW)                   -> MinGW Makefiles
    Pass -Toolchain MinGW or MSVC to skip that order. A hosted image that has
    both Visual Studio and MSYS2 otherwise always takes the MSVC branch.
    CMake does the real work, so the same sources build with all three.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Debug
    .\build.ps1 -Toolchain MinGW
    .\build.ps1 -Toolchain MSVC -WarningsAsErrors
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release',

    [ValidateSet('Auto', 'MSVC', 'MinGW')]
    [string]$Toolchain = 'Auto',

    # Passes -DQE_WARNINGS_AS_ERRORS=ON. CI uses this so a new warning fails the build.
    [switch]$WarningsAsErrors
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
$dist = Join-Path $root 'dist'

function Find-Cmake {
    $command = Get-Command cmake -ErrorAction SilentlyContinue
    if ($command) { return $command.Source }
    foreach ($candidate in @("$env:ProgramFiles\CMake\bin\cmake.exe",
                             "${env:ProgramFiles(x86)}\CMake\bin\cmake.exe")) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$cmake = Find-Cmake
if (-not $cmake) {
    Write-Host 'CMake was not found. Install a toolchain first:' -ForegroundColor Red
    Write-Host '  winget install Kitware.CMake'
    Write-Host '  winget install MSYS2.MSYS2      # then: pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake'
    Write-Host '  (or) Visual Studio 2022 Build Tools with the "C++ build tools" workload'
    exit 1
}
Write-Host "cmake: $cmake"

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vcvars = $null
if (Test-Path $vswhere) {
    $vsPath = & $vswhere -latest -products * `
        -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($vsPath) {
        $candidate = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
        if (Test-Path $candidate) { $vcvars = $candidate }
    }
}

$cl = Get-Command cl -ErrorAction SilentlyContinue
$gcc = Get-Command gcc -ErrorAction SilentlyContinue
$ninja = Get-Command ninja -ErrorAction SilentlyContinue

$cmakeConfig = @("-DCMAKE_BUILD_TYPE=$Config")
if ($WarningsAsErrors) { $cmakeConfig += '-DQE_WARNINGS_AS_ERRORS=ON' }

$mode = $Toolchain
if ($mode -eq 'Auto') {
    if ($vcvars -or $cl) { $mode = 'MSVC' }
    elseif ($gcc) { $mode = 'MinGW' }
}

if ($mode -eq 'MSVC' -and $vcvars) {
    Write-Host "toolchain: MSVC via $vcvars" -ForegroundColor Green
    $generator = if ($ninja) { 'Ninja' } else { 'NMake Makefiles' }
    # Quote each -D flag. Without the quotes, cmd.exe drops the value when
    # this command line is assembled as one string.
    # call is required: vcvars64.bat is itself a batch file, and cmd /c runs
    # a batch file by transferring control. Without call, cmake never starts
    # and the script then reports that usb-switch.exe is missing.
    $quotedDefs = ($cmakeConfig | ForEach-Object { "`"$_`"" }) -join ' '
    $command = "set VSLANG=1033&& call `"$vcvars`" >nul && " +
               "`"$cmake`" -S `"$root`" -B `"$build`" -G `"$generator`" $quotedDefs && " +
               "`"$cmake`" --build `"$build`" --config `"$Config`" --clean-first"
    cmd /c $command
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
elseif ($mode -eq 'MSVC' -and $cl) {
    Write-Host 'toolchain: MSVC (cl.exe on PATH)' -ForegroundColor Green
    $env:VSLANG = '1033'
    $generator = if ($ninja) { 'Ninja' } else { 'NMake Makefiles' }
    & $cmake -S $root -B $build -G $generator @cmakeConfig
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $build --config $Config --clean-first
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
elseif ($mode -eq 'MinGW' -and $gcc) {
    $generator = if ($ninja) { 'Ninja' } else { 'MinGW Makefiles' }
    # Prefer windres when both MinGW and the Windows SDK are installed.
    # Otherwise CMake can pick rc.exe and then fail to link the gcc objects.
    $windres = Get-Command windres -ErrorAction SilentlyContinue
    if ($windres) {
        $windresPath = $windres.Source.Replace('\', '/')
        $cmakeConfig += "-DCMAKE_RC_COMPILER=$windresPath"
    }
    Write-Host "toolchain: MinGW gcc ($generator)" -ForegroundColor Green
    & $cmake -S $root -B $build -G $generator @cmakeConfig
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
else {
    $missing = if ($Toolchain -eq 'Auto') { 'C++ compiler' } else { "$Toolchain compiler" }
    Write-Host "No $missing found. Install one of:" -ForegroundColor Red
    Write-Host '  winget install MSYS2.MSYS2   # pacman -S mingw-w64-ucrt-x86_64-gcc'
    Write-Host '  Visual Studio 2022 Build Tools (Desktop development with C++)'
    exit 1
}

$exePath = Join-Path $build 'usb-switch.exe'
$exe = Get-Item -LiteralPath $exePath -ErrorAction SilentlyContinue
if (-not $exe) {
    Write-Host 'build finished but usb-switch.exe was not found' -ForegroundColor Red
    exit 1
}

New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item $exe.FullName (Join-Path $dist 'usb-switch.exe') -Force
Copy-Item (Join-Path $root 'README.md') (Join-Path $dist 'README.md') -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $dist 'LICENSE') -Force

Write-Host ''
Write-Host "built $($exe.LastWriteTime): $dist\usb-switch.exe" -ForegroundColor Cyan
Write-Host 'smoke test:  .\dist\usb-switch.exe --list'
Write-Host 'run:         .\dist\usb-switch.exe'
Write-Host 'installer:   iscc installer\usb-switch.iss   (needs Inno Setup)'
exit 0
