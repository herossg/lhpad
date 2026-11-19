$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$sourceRoot = Join-Path $projectRoot 'vendor/notepad-plus-plus'
if (!(Test-Path (Join-Path $sourceRoot 'scintilla/win32/scintilla.mak'))) {
    Expand-Archive -LiteralPath (Join-Path $projectRoot 'vendor/notepad-plus-plus-src.zip') -DestinationPath $sourceRoot -Force
}
& (Join-Path $projectRoot 'cpp/patch-scintilla.ps1') -SourceRoot $sourceRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vsInstall) { throw 'Install Visual Studio C++ desktop build tools and a Windows SDK.' }
$vcvars = Join-Path $vsInstall 'VC/Auxiliary/Build/vcvars64.bat'
$buildEnvironment = & $env:ComSpec /d /c "call `"$vcvars`" >nul && set"
if ($LASTEXITCODE -ne 0) { throw 'Failed to initialize MSVC.' }
foreach ($entry in $buildEnvironment) {
    if ($entry -match '^([^=]+)=(.*)$') { [Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process') }
}
foreach ($target in @('scintilla/win32','lexilla/src')) {
    Push-Location (Join-Path $sourceRoot $target)
    try {
        $makefile = if ($target.StartsWith('scintilla')) { 'scintilla.mak' } else { 'lexilla.mak' }
        $log = Join-Path $PSScriptRoot ($makefile + '.log')
        & nmake /nologo -f $makefile QUIET=1 2>&1 | Out-File -LiteralPath $log -Encoding utf8
        if ($LASTEXITCODE -ne 0) { throw "Native build failed. See $log" }
    } finally { Pop-Location }
}
Copy-Item (Join-Path $sourceRoot 'scintilla/bin/Scintilla.dll') (Join-Path $PSScriptRoot 'Scintilla.dll') -Force
Copy-Item (Join-Path $sourceRoot 'lexilla/bin/lexilla.dll') (Join-Path $PSScriptRoot 'Lexilla.dll') -Force
Write-Host 'Native Scintilla and Lexilla built from bundled source.'
