<#
.SYNOPSIS
    Builds QuickEject and collects the release artifacts in dist\.

.DESCRIPTION
    Detects the available toolchain in this order:
      1. Visual Studio (through vswhere) + Windows SDK  -> MSVC, x64
      2. cl.exe already on PATH (developer prompt)       -> MSVC
      3. gcc.exe on PATH (MSYS2/MinGW)                   -> MinGW Makefiles
    CMake does the real work, so the same sources build with all three.

.EXAMPLE
    .\build.ps1
    .\build.ps1 -Config Debug
#>
[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
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

if ($vcvars) {
    Write-Host "toolchain: MSVC via $vcvars" -ForegroundColor Green
    # Note the quotes around -DCMAKE_BUILD_TYPE=... : without them the
    # variable is not expanded when the command line is built up as a string.
    $command = "`"$vcvars`" >nul && " +
               "`"$cmake`" -S `"$root`" -B `"$build`" -A x64 `"-DCMAKE_BUILD_TYPE=$Config`" && " +
               "`"$cmake`" --build `"$build`" --config `"$Config`""
    cmd /c $command
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
elseif ($cl) {
    Write-Host 'toolchain: MSVC (cl.exe on PATH)' -ForegroundColor Green
    & $cmake -S $root -B $build -A x64 "-DCMAKE_BUILD_TYPE=$Config"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $build --config $Config
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
elseif ($gcc) {
    $generator = if ($ninja) { 'Ninja' } else { 'MinGW Makefiles' }
    Write-Host "toolchain: MinGW gcc ($generator)" -ForegroundColor Green
    & $cmake -S $root -B $build -G $generator "-DCMAKE_BUILD_TYPE=$Config"
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
    & $cmake --build $build
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
else {
    Write-Host 'No C++ compiler found. Install one of:' -ForegroundColor Red
    Write-Host '  winget install MSYS2.MSYS2   # pacman -S mingw-w64-ucrt-x86_64-gcc'
    Write-Host '  Visual Studio 2022 Build Tools (Desktop development with C++)'
    exit 1
}

$exe = Get-ChildItem -Path $build -Filter 'quickeject.exe' -Recurse -ErrorAction SilentlyContinue |
    Select-Object -First 1
if (-not $exe) {
    Write-Host 'build finished but quickeject.exe was not found' -ForegroundColor Red
    exit 1
}

if (Test-Path $dist) { Remove-Item $dist -Recurse -Force }
New-Item -ItemType Directory -Path $dist | Out-Null
Copy-Item $exe.FullName (Join-Path $dist 'quickeject.exe')
Copy-Item (Join-Path $root 'README.md') (Join-Path $dist 'README.md')

Write-Host ''
Write-Host "built $($exe.LastWriteTime): $dist\quickeject.exe" -ForegroundColor Cyan
Write-Host 'smoke test:  .\dist\quickeject.exe --list'
Write-Host 'run:         .\dist\quickeject.exe'
Write-Host 'installer:   iscc installer\quickeject.iss   (needs Inno Setup)'
exit 0
