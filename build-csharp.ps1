param([switch]$Test,[switch]$RebuildNative,[string]$OutputDirectory = 'artifacts/portable')
$ErrorActionPreference = 'Stop'
Push-Location $PSScriptRoot
try {
    if ($RebuildNative -or !(Test-Path native/Scintilla.dll) -or !(Test-Path native/Lexilla.dll)) { & ./native/build-native.ps1 }
    & dotnet publish LHPad.csproj -c Release -r win-x64 -o $OutputDirectory --no-self-contained -p:BaseOutputPath=artifacts/build/ -p:DebugType=None -p:DebugSymbols=false --nologo
    if ($LASTEXITCODE -ne 0) { throw 'Managed build failed.' }
    if ($Test) {
        $testExe = Join-Path $PSScriptRoot "$OutputDirectory/LHPad.exe"
        $testProcess = Start-Process -FilePath $testExe -ArgumentList '--self-test' -WindowStyle Hidden -PassThru -Wait
        if ($testProcess.ExitCode -ne 0) { throw "Verification failed. See $OutputDirectory/test-results/results.txt." }
        Get-Content (Join-Path $OutputDirectory 'test-results/results.txt') | Select-Object -Last 12
    }
    Copy-Item README.md (Join-Path $OutputDirectory 'README.md')
    # Package only runtime files, excluding test fixtures and profiles.
    $runtimeFiles = Get-ChildItem $OutputDirectory -File | Where-Object { $_.Extension -ne '.pdb' }
    $packageItems = @($runtimeFiles.FullName) + @((Join-Path $OutputDirectory 'data'))
    if(Test-Path (Join-Path $OutputDirectory 'runtimes')) { $packageItems += Join-Path $OutputDirectory 'runtimes' }
    Compress-Archive -LiteralPath $packageItems -DestinationPath artifacts/LHPad-csharp-windows-x64.zip -Force
    & ./tools/package-source.ps1
    Write-Host "Ready: $OutputDirectory/LHPad.exe"
    Write-Host 'Package: artifacts/LHPad-csharp-windows-x64.zip'
} finally { Pop-Location }
