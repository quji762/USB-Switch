<#
.SYNOPSIS
    Builds and runs the unit tests (tests/unit.cpp) and friends.

.DESCRIPTION
    The tests cover the JSON parser (including the nesting-depth guard that
    prevents a stack overflow on a corrupted settings file) and the i18n
    formatter (including the single-pass placeholder substitution), plus the
    tray state machine, the multi-partition media-eject layout logic and the
    ATA pass-through CDB register placement used to read SMART data.

    The compiler is chosen automatically: g++ (MinGW-w64) when it is available,
    otherwise cl.exe from a Visual Studio developer environment - so an
    MSVC-only machine no longer fails here after a successful build. Every
    compile uses one shared macro + include set, mirroring CMakeLists.txt, so a
    source file is never built with two different configurations.

    AppInfo.h is generated from src/app/AppInfo.h.in when it is missing, so the
    tests also run on a clean checkout with no prior CMake configure. (CMake's
    own copy lives in build/generated; tools/build-no-cmake.ps1 uses the
    separate build-direct/generated tree - the two are not mixed.)

.EXAMPLE
    .\tools\run-tests.ps1
    .\tools\run-tests.ps1 -Runtime          # also run the device enumeration test
    .\tools\run-tests.ps1 -ToolChain 'C:\mingw64\bin'
#>
[CmdletBinding()]
param(
    [string]$ToolChain = '',
    [switch]$Runtime
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$genDir = Join-Path $root 'build\generated'
$testsDir = Join-Path $root 'build\tests'

# One shared macro set for every compile below, mirroring CMakeLists.txt so no
# source is ever built with two different configurations.
$defines = @(
    'WIN32_LEAN_AND_MEAN', 'NOMINMAX', 'UNICODE', '_UNICODE',
    'WINVER=0x0A00', '_WIN32_WINNT=0x0A00', '_CRT_SECURE_NO_WARNINGS'
)

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

# --- generate AppInfo.h if CMake has not already produced it ----------------
# tests/unit.cpp includes "app/AppInfo.h", which CMake writes to build/generated
# during configure. Generate it here so the tests are self-contained on a clean
# checkout; when CMake already produced it this is a no-op.
$appInfoHeader = Join-Path $genDir 'app\AppInfo.h'
if (-not (Test-Path $appInfoHeader)) {
    $cmakeText = Get-Content (Join-Path $root 'CMakeLists.txt') -Raw
    if ($cmakeText -notmatch 'project\(usb-switch\s+VERSION\s+(\d+)\.(\d+)\.(\d+)') {
        throw 'could not read the version from CMakeLists.txt'
    }
    $major, $minor, $patch = $Matches[1], $Matches[2], $Matches[3]
    $version = "$major.$minor.$patch"
    $appInfoText = Get-Content (Join-Path $root 'src\app\AppInfo.h.in') -Raw
    $appInfoText = ($appInfoText.Replace('@PROJECT_VERSION_MAJOR@', $major))
    $appInfoText = ($appInfoText.Replace('@PROJECT_VERSION_MINOR@', $minor))
    $appInfoText = ($appInfoText.Replace('@PROJECT_VERSION_PATCH@', $patch))
    $appInfoText = ($appInfoText.Replace('@PROJECT_VERSION@', $version))
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $appInfoHeader) | Out-Null
    [System.IO.File]::WriteAllText($appInfoHeader, $appInfoText,
        (New-Object System.Text.UTF8Encoding($false)))
    Write-Host "generated $appInfoHeader" -ForegroundColor DarkGray
}

# Include dirs mirror the main build: src/ for the project headers,
# resources/ for resource.h, and build/generated/ for AppInfo.h.
$includeDirs = @((Join-Path $root 'src'), (Join-Path $root 'resources'), $genDir)

# --- pick a compiler: g++ first (what CI uses), then cl.exe (MSVC-only) -----
$gxx = Find-Tool 'g++.exe' $ToolChain
$cl = $null
if (-not $gxx) { $cl = Find-Tool 'cl.exe' $ToolChain }
if (-not $gxx -and -not $cl) {
    Write-Host 'No C++ compiler found. Pass -ToolChain <mingw64\bin>, or run from a' -ForegroundColor Red
    Write-Host 'Visual Studio developer prompt so cl.exe is on PATH.' -ForegroundColor Red
    exit 1
}
if ($gxx) { Write-Host "compiler: $gxx" -ForegroundColor DarkGray }
else { Write-Host "compiler: $cl (MSVC)" -ForegroundColor DarkGray }

# The test executables link the C++ and winpthread runtime dynamically, and the
# loader takes the first match on PATH. A machine easily carries a second copy of
# those DLLs earlier on PATH - Git for Windows ships its own mingw64 runtime and
# puts it near the front - so the test then resolves the wrong libstdc++ and dies
# at startup with STATUS_ENTRYPOINT_NOT_FOUND before a single check runs. Put the
# compiler's own directory first, so the runtime matches what linked the binary.
if ($gxx) {
    $compilerDir = Split-Path -Parent $gxx
    if ($compilerDir -and (Test-Path -LiteralPath $compilerDir) -and $env:PATH -notlike "$compilerDir;*") {
        $env:PATH = "$compilerDir;$env:PATH"
        Write-Host "runtime path: $compilerDir (first on PATH)" -ForegroundColor DarkGray
    }
}

# Compiles $Sources (+ $LibNames) into $OutExe with the shared flags. Sets
# $LASTEXITCODE; the caller checks it, exactly as the inline commands did.
function Invoke-Compile {
    param([string]$OutExe, [string[]]$Sources, [string[]]$LibNames = @())
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutExe) | Out-Null
    if ($gxx) {
        & $gxx -std=c++17 -O1 -g -Wall -Wextra `
            ($defines | ForEach-Object { "-D$_" }) `
            ($includeDirs | ForEach-Object { "-I$_" }) `
            -o $OutExe @Sources `
            ($LibNames | ForEach-Object { "-l$_" })
    } else {
        # /Fo keeps the .obj files out of the repository; /Fe names the exe.
        & $cl /nologo /std:c++17 /O2 /W4 /permissive- /utf-8 /EHsc `
            ($defines | ForEach-Object { "/D$_" }) `
            ($includeDirs | ForEach-Object { "/I$_" }) `
            "/Fo:$testsDir\" "/Fe:$OutExe" @Sources `
            ($LibNames | ForEach-Object { "$_.lib" })
    }
}

$code = 0

# --- unit tests -------------------------------------------------------------
$unitSrc = @(
    (Join-Path $root 'tests\unit.cpp')
    (Join-Path $root 'src\util\Json.cpp')
    (Join-Path $root 'src\util\Str.cpp')
    (Join-Path $root 'src\i18n\I18n.cpp')
    (Join-Path $root 'src\i18n\Strings.cpp')
    (Join-Path $root 'src\core\SmartInfo.cpp')
    # SmartInfo.cpp logs through qe::Logger, so the unit link needs it too.
    (Join-Path $root 'src\app\Logger.cpp')
)
$unitLibs = @('setupapi', 'cfgmgr32', 'wevtapi', 'comctl32', 'ole32', 'advapi32', 'user32', 'gdi32')
$outExe = Join-Path $testsDir 'unit.exe'
Invoke-Compile $outExe $unitSrc $unitLibs
if ($LASTEXITCODE -ne 0) { throw "test build failed ($LASTEXITCODE)" }
Write-Host "built $outExe" -ForegroundColor Cyan
& $outExe
$code = $LASTEXITCODE
Write-Host ''

# --- tray test --------------------------------------------------------------
$traySrc = @(
    (Join-Path $root 'tests\tray.cpp')
    (Join-Path $root 'src\ui\TrayIcon.cpp')
    (Join-Path $root 'src\app\Logger.cpp')
    (Join-Path $root 'src\util\Str.cpp')
)
$trayLibs = @('shell32', 'advapi32', 'user32')
$trayExe = Join-Path $testsDir 'tray.exe'
Invoke-Compile $trayExe $traySrc $trayLibs
if ($LASTEXITCODE -ne 0) { throw "tray test build failed ($LASTEXITCODE)" }
& $trayExe
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }

# --- media partition test ---------------------------------------------------
$mediaSrc = @(
    (Join-Path $root 'tests\media-partitions.cpp')
    (Join-Path $root 'src\core\MediaEject.cpp')
)
$mediaExe = Join-Path $testsDir 'media-partitions.exe'
Invoke-Compile $mediaExe $mediaSrc
if ($LASTEXITCODE -ne 0) { throw "media partition test build failed ($LASTEXITCODE)" }
& $mediaExe
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }

# --- SMART pass-through test ------------------------------------------------
# A mock stands in for CreateFileW/DeviceIoControl (see tests/smart-passthrough.cpp),
# so the CDB register placement of the ATA PASS-THROUGH commands is asserted
# instead of only being exercised against real hardware.
$smartSrc = @(
    (Join-Path $root 'tests\smart-passthrough.cpp')
    (Join-Path $root 'src\core\SmartInfo.cpp')
    # SmartInfo.cpp logs through qe::Logger and formats with util/Str.
    (Join-Path $root 'src\app\Logger.cpp')
    (Join-Path $root 'src\util\Str.cpp')
)
$smartLibs = @('setupapi', 'cfgmgr32', 'wevtapi', 'comctl32', 'ole32', 'advapi32', 'user32', 'gdi32')
$smartExe = Join-Path $testsDir 'smart-passthrough.exe'
Invoke-Compile $smartExe $smartSrc $smartLibs
if ($LASTEXITCODE -ne 0) { throw "smart pass-through test build failed ($LASTEXITCODE)" }
Write-Host "built $smartExe" -ForegroundColor Cyan
& $smartExe
if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }

# --- optional: device enumeration against the real machine -------------------
if ($Runtime) {
    Write-Host ''
    $sources = Get-ChildItem -Path (Join-Path $root 'src') -Filter '*.cpp' -Recurse |
        # This console test has no Common Controls v6 activation manifest.
        # The separate GUI smoke test exercises the manifested tray binary.
        Where-Object { $_.Name -ne 'main.cpp' -and $_.Name -ne 'App.cpp' -and $_.Directory.Name -ne 'ui' } |
        ForEach-Object { $_.FullName }
    $runtimeSrc = @((Join-Path $root 'tests\device-runtime.cpp')) + @($sources)
    $runtimeLibs = @('setupapi', 'uuid', 'cfgmgr32', 'wevtapi', 'comctl32', 'shell32', 'ole32', 'advapi32', 'user32', 'gdi32')
    $runtimeExe = Join-Path $testsDir 'device-runtime.exe'
    Invoke-Compile $runtimeExe $runtimeSrc $runtimeLibs
    if ($LASTEXITCODE -ne 0) { throw "runtime test build failed ($LASTEXITCODE)" }
    Write-Host "built $runtimeExe" -ForegroundColor Cyan
    & $runtimeExe
    if ($LASTEXITCODE -ne 0) { $code = $LASTEXITCODE }
    Write-Host ''
}

if ($code -eq 0) { Write-Host 'ALL TESTS PASSED' -ForegroundColor Green }
else { Write-Host "TESTS FAILED (exit $code)" -ForegroundColor Red }
exit $code
