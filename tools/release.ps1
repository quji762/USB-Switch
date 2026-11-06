<# Build and validate a local release bundle. Does not publish or sign it. #>
[CmdletBinding()]
param(
    [switch]$AllowDirty,
    [switch]$GuiSmoke,
    [string]$ToolChain = '',
    [string]$InnoCompiler = ''
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Push-Location $root
try {
    if ($ToolChain) { $env:PATH = "$ToolChain;$env:PATH" }
    $status = & git status --porcelain --untracked-files=all
    if ($LASTEXITCODE -ne 0) { throw 'Could not inspect Git status' }
    if ($status -and !$AllowDirty) { throw 'Working tree is dirty; commit changes or use -AllowDirty for a local review build.' }
    $fingerprint = & python tools/package-release.py --fingerprint
    if ($LASTEXITCODE -ne 0) { throw 'Could not fingerprint source files' }
    & .\build.ps1 -Config Release
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
    & python tools/check-strings.py
    if ($LASTEXITCODE -ne 0) { throw 'Source consistency check failed' }
    & python tools/check-resources.py dist/usb-switch.exe
    if ($LASTEXITCODE -ne 0) { throw 'Resource check failed' }
    & .\tools\run-tests.ps1 -Runtime -ToolChain $ToolChain
    if ($LASTEXITCODE -ne 0) { throw 'Tests failed' }
    & .\tests\migration.ps1
    if ($GuiSmoke) { & .\tests\gui-smoke.ps1 }
    if (!$InnoCompiler) {
        $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($command) { $InnoCompiler = $command.Source }
        else {
            $candidates = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe")
            $InnoCompiler = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
        }
    }
    if (!$InnoCompiler) { throw 'Inno Setup compiler not found; use -InnoCompiler.' }
    & $InnoCompiler /Qp installer/usb-switch.iss
    if ($LASTEXITCODE -ne 0) { throw 'Installer build failed' }
    $match = [regex]::Match((Get-Content CMakeLists.txt -Raw), 'project\(usb-switch\s+VERSION\s+(\d+\.\d+\.\d+)')
    if (!$match.Success) { throw 'Project version not found' }
    $version = $match.Groups[1].Value
    $exeVersion = (Get-Item dist/usb-switch.exe).VersionInfo.ProductVersion
    if ($exeVersion -ne $version) { throw "Executable version $exeVersion does not match $version" }
    $installer = Join-Path $root "dist\installer\usb-switch-setup-$version.0.exe"
    & .\tests\installer.ps1 -Installer $installer
    $packageArgs = @('tools/package-release.py', '--expected-fingerprint', $fingerprint)
    if ($AllowDirty) { $packageArgs += '--allow-dirty' }
    & python @packageArgs
    if ($LASTEXITCODE -ne 0) { throw 'Packaging failed' }
} finally {
    Pop-Location
}
