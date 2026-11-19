param(
    [string]$Before='',
    [string]$After=''
)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
if(!$Before){$Before=Join-Path $taskRoot 'artifacts/native-1.1.0/LHPadShell.dll'}
if(!$After){$After=Join-Path $taskRoot 'artifacts/native-1.1.1/LHPadShell.dll'}
$taskStage=Join-Path $taskRoot 'artifacts/menu-latency-1.1.1'
New-Item -ItemType Directory -Force $taskStage | Out-Null
$taskVswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
$taskVs=& $taskVswhere -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$taskVcvars=Join-Path $taskVs 'VC/Auxiliary/Build/vcvars64.bat'
$taskEnvironment=& $env:ComSpec /d /c "call `"$taskVcvars`" >nul && set"
if($LASTEXITCODE){throw 'MSVC setup failed'}
foreach($taskEntry in $taskEnvironment){if($taskEntry -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($Matches[1],$Matches[2],'Process')}}
Push-Location $taskStage
try {
    & cl /nologo /std:c++20 /utf-8 /EHsc /O2 /MT /W4 /DUNICODE /D_UNICODE /Fe:probe.exe (Join-Path $taskRoot 'cpp/menu_latency_bench.cpp') ole32.lib shell32.lib uuid.lib
    if($LASTEXITCODE){throw 'Probe compilation failed'}
    $taskFile=Join-Path $taskStage 'fixture.txt'
    'Menu timing fixture; no Invoke calls.' | Set-Content -LiteralPath $taskFile -Encoding ascii
    & ./probe.exe $Before $After $taskFile | Tee-Object -FilePath (Join-Path $taskStage 'results.jsonl')
    if($LASTEXITCODE){throw "Probe failed: $LASTEXITCODE"}
} finally {Pop-Location}
