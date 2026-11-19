param([Parameter(Mandatory=$true)][string]$Directory)
$ErrorActionPreference='Stop'
$taskRoot=Split-Path -Parent $PSScriptRoot
$taskDirectory=(Resolve-Path -LiteralPath $Directory).ProviderPath
$taskBinaries=@('LHPad.exe','LHPadShell.dll','Scintilla.dll','Lexilla.dll','WebView2Loader.dll','LHPad.Explorer.msix')
foreach($taskName in $taskBinaries){
    $taskSignature=Get-AuthenticodeSignature -LiteralPath (Join-Path $taskDirectory $taskName)
    if($taskSignature.Status -ne 'Valid' -or $taskSignature.SignerCertificate.Thumbprint -ne '537D10B46D7742E859498D5642863AEC947973CB'){
        throw "Release signature is invalid: $taskName"
    }
}
foreach($taskName in @('README.md','LICENSE','THIRD-PARTY-NOTICES.txt','Register-LHPad.cmd')){
    Copy-Item -LiteralPath (Join-Path $taskRoot $taskName) -Destination $taskDirectory -Force
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'cpp/README.txt') -Destination $taskDirectory -Force
foreach($taskName in @('install-explorer.ps1','register-explorer.ps1')){
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot $taskName) -Destination (Join-Path $taskDirectory 'tools') -Force
}
Copy-Item -LiteralPath (Join-Path $taskRoot 'docs/RELEASE-1.1.2.md') -Destination (Join-Path $taskDirectory 'docs') -Force
$taskNames=$taskBinaries+@('LHPad.Development.cer','README.md','README.txt','LICENSE','THIRD-PARTY-NOTICES.txt','Register-LHPad.cmd','data','Assets','tools','docs')
$taskFiles=@($taskNames | ForEach-Object { Join-Path $taskDirectory $_ })
$taskZip=Join-Path $taskRoot 'artifacts/LHPad-1.1.2-windows-x64.zip'
Compress-Archive -LiteralPath $taskFiles -DestinationPath $taskZip -Force
foreach($taskAlias in @('LHPad-native-windows-x64.zip','LHPad-windows-x64.zip')){
    Copy-Item -LiteralPath $taskZip -Destination (Join-Path $taskRoot "artifacts/$taskAlias") -Force
}
$taskDigest=(Get-FileHash -LiteralPath $taskZip -Algorithm SHA256).Hash.ToLowerInvariant()
"$taskDigest  LHPad-1.1.2-windows-x64.zip" | Set-Content -LiteralPath (Join-Path $taskRoot 'artifacts/LHPad-1.1.2-SHA256SUMS.txt') -Encoding ascii
Write-Output "Release ZIP: $taskZip"
