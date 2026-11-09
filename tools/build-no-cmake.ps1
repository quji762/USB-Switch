<#
.SYNOPSIS
    Builds USB-Switch with a bare MinGW-w64 toolchain - no CMake required.

.DESCRIPTION
    CMake is the primary build system (see CMakeLists.txt). This script exists
    for the "I only have a gcc" case: it performs the two substitutions CMake
    would do (AppInfo.h, version.rc) and then compiles and links directly.

    The version number is read from CMakeLists.txt, so there is still only one
    place to bump it.

.EXAMPLE
    .\tools\build-no-cmake.ps1 -ToolChain 'C:\mingw64\bin'
    .\tools\build-no-cmake.ps1        # uses g++ from PATH
#>
[CmdletBinding()]
param(
    [string]$ToolChain = '',
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

$ErrorActionPreference = 'Stop'
# Separate from the CMake build directory so the two can coexist (and so this
# script never overwrites CMake's generated files).
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$genDir = Join-Path $root 'build-direct\generated'
$objDir = Join-Path $root 'build-direct\obj'
$distDir = Join-Path $root 'dist'

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
$windres = Find-Tool 'windres.exe' $ToolChain
if (-not $gxx -or -not $windres) {
    Write-Host 'g++ and/or windres were not found.' -ForegroundColor Red
    Write-Host '  -ToolChain <dir>   point at a mingw64\bin directory'
    Write-Host '  winget install MSYS2.MSYS2   (then pacman -S mingw-w64-ucrt-x86_64-gcc)'
    exit 1
}

# --- version: parsed from CMakeLists.txt so it stays the single source -----
$cmakeText = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
if ($cmakeText -notmatch 'project\(usb-switch\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
    throw 'could not read the version from CMakeLists.txt'
}
$major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]
$version = "$major.$minor.$patch"
Write-Host "building USB-Switch $version ($Config) with $gxx"

New-Item -ItemType Directory -Force -Path $genDir, $objDir, $distDir | Out-Null

# --- generated files (what CMake's configure_file would produce) ------------
# Note: each .Replace() call is wrapped in parentheses. PowerShell 5.1's parser
# treats a bare leading ".Replace(" at the start of a continuation line as a
# command name, so this form is required for Windows PowerShell compatibility.
$appInfoTemplate = Get-Content (Join-Path $root 'src\app\AppInfo.h.in') -Raw

# Product identity is parsed from AppInfo.h.in exactly as CMakeLists.txt does,
# so this direct build and the CMake build can never drift on these four values.
if ($appInfoTemplate -notmatch '#define QUICKEJECT_APP_NAME[ \t]+"([^"]*)"') { throw 'could not read QUICKEJECT_APP_NAME from src\app\AppInfo.h.in' }
$appName = $Matches[1]
if ($appInfoTemplate -notmatch '#define QUICKEJECT_APP_DESCRIPTION[ \t]+"([^"]*)"') { throw 'could not read QUICKEJECT_APP_DESCRIPTION from src\app\AppInfo.h.in' }
$appDescription = $Matches[1]
if ($appInfoTemplate -notmatch '#define QUICKEJECT_COMPANY[ \t]+"([^"]*)"') { throw 'could not read QUICKEJECT_COMPANY from src\app\AppInfo.h.in' }
$company = $Matches[1]
if ($appInfoTemplate -notmatch '#define QUICKEJECT_COPYRIGHT[ \t]+"([^"]*)"') { throw 'could not read QUICKEJECT_COPYRIGHT from src\app\AppInfo.h.in' }
$copyright = $Matches[1]

$appInfo = $appInfoTemplate
$appInfo = ($appInfo.Replace('@PROJECT_VERSION_MAJOR@', $major))
$appInfo = ($appInfo.Replace('@PROJECT_VERSION_MINOR@', $minor))
$appInfo = ($appInfo.Replace('@PROJECT_VERSION_PATCH@', $patch))
$appInfo = ($appInfo.Replace('@PROJECT_VERSION@', $version))
$appInfoPath = Join-Path $genDir 'app\AppInfo.h'
New-Item -ItemType Directory -Force -Path (Split-Path $appInfoPath) | Out-Null
[System.IO.File]::WriteAllText($appInfoPath, $appInfo,
    (New-Object System.Text.UTF8Encoding($false)))

# Paths in an .rc file must use forward slashes: windres treats a backslash
# inside a quoted path as an escape sequence.
$iconPath = (Resolve-Path (Join-Path $root 'resources\usb-switch.ico')).Path.Replace('\', '/')
$manifestPath = (Resolve-Path (Join-Path $root 'resources\app.manifest')).Path.Replace('\', '/')

$resource = Get-Content (Join-Path $root 'resources\version.rc.in') -Raw
$resource = ($resource.Replace('@QUICKEJECT_RC_VERSION@', "$major,$minor,$patch,0"))
$resource = ($resource.Replace('@QUICKEJECT_COMPANY@', $company))
$resource = ($resource.Replace('@QUICKEJECT_APP_DESCRIPTION@', $appDescription))
$resource = ($resource.Replace('@QUICKEJECT_VERSION@', $version))
$resource = ($resource.Replace('@QUICKEJECT_APP_NAME@', $appName))
$resource = ($resource.Replace('@QUICKEJECT_COPYRIGHT@', $copyright))
$resource = ($resource.Replace('@QUICKEJECT_ICON_PATH@', $iconPath))
$resource = ($resource.Replace('@QUICKEJECT_MANIFEST_PATH@', $manifestPath))
[System.IO.File]::WriteAllText((Join-Path $genDir 'version.rc'), $resource,
    (New-Object System.Text.UTF8Encoding($false)))

# --- resources ---------------------------------------------------------------
$versionObj = Join-Path $objDir 'version.o'
$dialogsObj = Join-Path $objDir 'dialogs.o'
& $windres -i (Join-Path $genDir 'version.rc') -o $versionObj -O coff
if ($LASTEXITCODE -ne 0) { throw 'windres failed for version.rc' }
& $windres -i (Join-Path $root 'resources\dialogs.rc') -o $dialogsObj -O coff
if ($LASTEXITCODE -ne 0) { throw 'windres failed for dialogs.rc' }

# --- sources ------------------------------------------------------------------
$optimize = if ($Config -eq 'Release') { '-O2' } else { '-O0', '-g' }
$flags = @(
    '-std=c++17'
    $optimize
    '-Wall', '-Wextra'
    '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-DUNICODE', '-D_UNICODE'
    '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_CRT_SECURE_NO_WARNINGS'
    "-I$genDir", "-I$root\src", "-I$root\resources"
)

$objects = @($versionObj, $dialogsObj)
$sources = Get-ChildItem -Path (Join-Path $root 'src') -Filter '*.cpp' -Recurse
foreach ($source in $sources) {
    # Parentheses around the method call: without them PowerShell 5.1 parses
    # ".Substring(" at the start of a pipeline as a command name.
    $relative = ($source.FullName.Substring($root.Length + 1)) -replace '\\', '_'
    $object = Join-Path $objDir ($relative -replace '\.cpp$', '.o')
    & $gxx @flags -c $source.FullName -o $object
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $($source.Name)" }
    $objects += $object
}

# --- link ---------------------------------------------------------------------
# -static: self-contained binary, no MinGW runtime DLLs next to the exe.
$exe = Join-Path $distDir 'usb-switch.exe'
& $gxx -municode -mwindows -static -o $exe @objects `
    -lsetupapi -lcfgmgr32 -lwevtapi -lcomctl32 -lshell32 -lole32 -ladvapi32 -luser32 -lgdi32 -ldwmapi -luxtheme -luuid
if ($LASTEXITCODE -ne 0) { throw 'link failed' }

Copy-Item (Join-Path $root 'README.md') (Join-Path $distDir 'README.md') -Force
Copy-Item (Join-Path $root 'LICENSE') (Join-Path $distDir 'LICENSE') -Force
Write-Host "built: $exe" -ForegroundColor Cyan
Write-Host 'smoke test: .\dist\usb-switch.exe --list'
