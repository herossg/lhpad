param([switch]$Test,[switch]$ShellTest,[switch]$RebuildNative,[string]$OutputDirectory='')

$ErrorActionPreference='Stop'

$taskRoot=Split-Path $PSScriptRoot -Parent

. (Join-Path $taskRoot 'tools/version.ps1')

if(!$OutputDirectory){$OutputDirectory="artifacts/native-$(Get-LHPadVersion)"}

$taskOutput=if([IO.Path]::IsPathRooted($OutputDirectory)){$OutputDirectory}else{Join-Path $taskRoot $OutputDirectory}

New-Item -ItemType Directory -Force $taskOutput,(Join-Path $taskRoot 'artifacts/native-obj') | Out-Null

if($RebuildNative -or !(Test-Path (Join-Path $taskRoot 'native/Scintilla.dll'))) { & (Join-Path $taskRoot 'native/build-native.ps1') }

$taskVsWhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'

$taskVs=& $taskVsWhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath

if(!$taskVs){throw 'Visual Studio C++ desktop tools are required.'}

$taskVcVars=Join-Path $taskVs 'VC/Auxiliary/Build/vcvars64.bat'

$taskEnvironment=& $env:ComSpec /d /c "call `"$taskVcVars`" >nul && set"

if($LASTEXITCODE){throw 'MSVC environment initialization failed.'}

foreach($taskEntry in $taskEnvironment){if($taskEntry -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}

Push-Location $PSScriptRoot

try {

    $taskObj=Join-Path $taskRoot 'artifacts/native-obj'

    & rc /nologo /fo "$taskObj/LHPad.res" LHPad.rc

    if($LASTEXITCODE){throw 'Resource compile failed.'}

    $taskSources=@('core.cpp','diff.cpp','format.cpp','dialogs.cpp','app.cpp','tabs.cpp','documents.cpp','editing.cpp','search.cpp','compare.cpp','hex.cpp','preview.cpp','tests.cpp','shell_package.cpp','shell_registration.cpp','main.cpp','vendor/pugixml/pugixml.cpp')

    $taskHeaderTime=(Get-ChildItem -LiteralPath $PSScriptRoot -Filter '*.h' | Sort-Object LastWriteTimeUtc -Descending | Select-Object -First 1).LastWriteTimeUtc

    $taskCompile=@($taskSources | Where-Object { $taskObjectPath=Join-Path $taskObj (([IO.Path]::GetFileNameWithoutExtension($_))+'.obj'); !(Test-Path $taskObjectPath) -or (Get-Item $taskObjectPath).LastWriteTimeUtc -lt (Get-Item $_).LastWriteTimeUtc -or (Get-Item $taskObjectPath).LastWriteTimeUtc -lt $taskHeaderTime })

    if($taskCompile.Count){

        & cl /nologo /c /MP4 /std:c++20 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /D_WIN32_WINNT=0x0A00 /Fo"$taskObj/" $taskCompile

        if($LASTEXITCODE){throw 'Native C++ build failed.'}

    }

    $taskCCompile=@(@('vendor/md4c/md4c.c','vendor/md4c/md4c-html.c','vendor/md4c/entity.c') | Where-Object { $taskObjectPath=Join-Path $taskObj (([IO.Path]::GetFileNameWithoutExtension($_))+'.obj'); !(Test-Path $taskObjectPath) -or (Get-Item $taskObjectPath).LastWriteTimeUtc -lt (Get-Item $_).LastWriteTimeUtc })

    if($taskCCompile.Count){

        & cl /nologo /c /O2 /MT /W3 /D_CRT_SECURE_NO_WARNINGS /Fo"$taskObj/" $taskCCompile

        if($LASTEXITCODE){throw 'Markdown C build failed.'}

    }

    $taskObjects=@(Get-ChildItem -LiteralPath $taskObj -Filter '*.obj' | Where-Object { $_.BaseName -notin @('shell_extension','shell_tests') }).FullName

    & link /nologo /OUT:"$taskOutput/LHPad.exe" $taskObjects "$taskObj/LHPad.res" /SUBSYSTEM:WINDOWS /DYNAMICBASE /NXCOMPAT /CETCOMPAT user32.lib gdi32.lib comctl32.lib comdlg32.lib shell32.lib shlwapi.lib ole32.lib oleaut32.lib advapi32.lib bcrypt.lib gdiplus.lib uuid.lib uxtheme.lib windowsapp.lib

    if($LASTEXITCODE){throw 'Native link failed.'}

    & rc /nologo /fo "$taskObj/LHPadShell.res" shell_icon.rc
    if($LASTEXITCODE){throw 'Explorer icon compile failed.'}
    & cl /nologo /LD /std:c++20 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /Fo"$taskObj/shell_extension.obj" shell_extension.cpp /link /OUT:"$taskOutput/LHPadShell.dll" /IMPLIB:"$taskObj/LHPadShell.lib" /DEF:shell_extension.def "$taskObj/LHPadShell.res" /DYNAMICBASE /NXCOMPAT /CETCOMPAT ole32.lib shell32.lib uuid.lib

    if($LASTEXITCODE){throw 'Explorer DLL build failed.'}

    Copy-Item (Join-Path $taskRoot 'native/*.dll') $taskOutput -Force

    Copy-Item (Join-Path $PSScriptRoot 'vendor/webview2/WebView2Loader.dll') $taskOutput -Force

    New-Item -ItemType Directory -Force (Join-Path $taskOutput 'data') | Out-Null

    Get-ChildItem -LiteralPath (Join-Path $taskRoot 'data') | Copy-Item -Destination (Join-Path $taskOutput 'data') -Recurse -Force

    Copy-Item (Join-Path $taskRoot 'LICENSE'),(Join-Path $taskRoot 'THIRD-PARTY-NOTICES.txt'),(Join-Path $taskRoot 'README.md'),(Join-Path $PSScriptRoot 'README.txt') $taskOutput -Force

    & (Join-Path $taskRoot 'tools/build-modern-shell.ps1') -Destination $taskOutput

    & (Join-Path $taskRoot 'tools/sign-release.ps1') -Directory $taskOutput

    New-Item -ItemType Directory -Force (Join-Path $taskOutput 'tools') | Out-Null

    foreach($taskTool in @('install-explorer.ps1','register-explorer.ps1')) { Copy-Item -LiteralPath (Join-Path $taskRoot "tools/$taskTool") -Destination (Join-Path $taskOutput 'tools') -Force }

    Copy-Item -LiteralPath (Join-Path $taskRoot 'Register-LHPad.cmd') -Destination $taskOutput -Force

    if($Test -or $ShellTest){

        & cl /nologo /std:c++20 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /Fo"$taskObj/shell_tests.obj" /Fe"$taskObj/LHPad-shell-tests.exe" shell_tests.cpp ole32.lib shell32.lib uuid.lib user32.lib

        if($LASTEXITCODE){throw 'Explorer regression build failed.'}

        & "$taskObj/LHPad-shell-tests.exe" "$taskOutput/LHPadShell.dll" "$taskObj/shell-fixture-$([guid]::NewGuid().ToString('N'))" | Tee-Object -FilePath (Join-Path $taskOutput 'docs/shell-test-results.txt')

        if($LASTEXITCODE){throw 'Explorer command regression failed.'}

    }

    if($Test){$taskProcess=Start-Process -FilePath (Join-Path $taskOutput 'LHPad.exe') -ArgumentList '--self-test' -PassThru -Wait -WindowStyle Hidden;if($taskProcess.ExitCode){throw "Native verification failed: $taskOutput/test-results/native-results.txt"};Get-Content (Join-Path $taskOutput 'test-results/native-results.txt') | Select-Object -Last 8; Copy-Item -LiteralPath (Join-Path $taskOutput 'test-results/native-results.txt') -Destination (Join-Path $taskOutput 'docs') -Force}

    & (Join-Path $taskRoot 'tools/package-release.ps1') -Directory $taskOutput

    Write-Host "Native executable: $taskOutput/LHPad.exe"

} finally {Pop-Location}

