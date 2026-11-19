param([Parameter(Mandatory=$true)][string]$Destination)
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$taskStage = Join-Path $taskRoot 'artifacts\modern-shell-package'
New-Item -ItemType Directory -Force $taskStage,(Join-Path $taskStage 'Assets'),$Destination | Out-Null
Copy-Item -LiteralPath (Join-Path $taskRoot 'cpp\modern\AppxManifest.xml') -Destination $taskStage -Force
# Reuse the program's embedded 256px PNG; no graphics framework or runtime added.
$taskIcon = [IO.File]::ReadAllBytes((Join-Path $taskRoot 'data\LHPad.ico'))
$taskFound = $false
for ($taskIndex=0; $taskIndex -lt [BitConverter]::ToUInt16($taskIcon,4); $taskIndex++) {
    $taskEntry = 6 + 16 * $taskIndex
    if ($taskIcon[$taskEntry] -ne 0 -or $taskIcon[$taskEntry+1] -ne 0) { continue }
    $taskLength = [BitConverter]::ToUInt32($taskIcon,$taskEntry+8)
    $taskOffset = [BitConverter]::ToUInt32($taskIcon,$taskEntry+12)
    $taskImage = New-Object byte[] $taskLength
    [Array]::Copy($taskIcon,$taskOffset,$taskImage,0,$taskLength)
    [IO.File]::WriteAllBytes((Join-Path $taskStage 'Assets\LHPad.png'),$taskImage)
    $taskFound = $true
    break
}
if (!$taskFound) { throw 'Missing 256px program icon.' }
$taskSdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$taskMakeAppx = Get-ChildItem -LiteralPath $taskSdkRoot -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\makeappx.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (!$taskMakeAppx) { throw 'Windows SDK MakeAppx.exe is required for the Windows 11 menu package.' }
& $taskMakeAppx pack /o /nv /d $taskStage /p (Join-Path $Destination 'LHPad.Explorer.msix')
if ($LASTEXITCODE) { throw 'Explorer identity package build failed.' }
$taskCertificate = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert |
    Where-Object { $_.Subject -eq 'CN=LHZip Development' -and $_.HasPrivateKey -and $_.NotAfter -gt (Get-Date).AddDays(7) } |
    Sort-Object NotAfter -Descending | Select-Object -First 1
if (!$taskCertificate -or $taskCertificate.Thumbprint -ne '537D10B46D7742E859498D5642863AEC947973CB') { throw 'The existing LHZip development signing key is required.' }
$taskSignTool = Join-Path (Split-Path -Parent $taskMakeAppx) 'signtool.exe'
& $taskSignTool sign /fd SHA256 /sha1 $taskCertificate.Thumbprint /s My /d 'LHPad Explorer menu' (Join-Path $Destination 'LHPad.Explorer.msix')
if ($LASTEXITCODE) { throw 'Explorer identity package signing failed.' }
Export-Certificate -Cert $taskCertificate -FilePath (Join-Path $Destination 'LHPad.Development.cer') -Force | Out-Null
Write-Output "Development signing certificate: $($taskCertificate.Subject) / $($taskCertificate.Thumbprint)"
# Trust is deliberately a separate user-approved deployment step.
New-Item -ItemType Directory -Force (Join-Path $Destination 'Assets') | Out-Null
Copy-Item -LiteralPath (Join-Path $taskStage 'Assets\LHPad.png') -Destination (Join-Path $Destination 'Assets') -Force
