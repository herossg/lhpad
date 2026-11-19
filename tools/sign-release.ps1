param(
    [Parameter(Mandatory=$true)][string]$Directory,
    [string]$TimestampUrl = 'http://timestamp.digicert.com'
)
$ErrorActionPreference = 'Stop'
$taskDirectory = (Resolve-Path -LiteralPath $Directory).ProviderPath
$taskPublicCertificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2((Join-Path $taskDirectory 'LHPad.Development.cer'))
$taskCertificate = Get-Item -LiteralPath ('Cert:\CurrentUser\My\' + $taskPublicCertificate.Thumbprint)
if (!$taskCertificate.HasPrivateKey -or $taskCertificate.Subject -ne 'CN=LHZip Development') {
    throw 'The matching LHZip development signing key is required.'
}
$taskSdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
$taskSignTool = Get-ChildItem -LiteralPath $taskSdkRoot -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'x64\signtool.exe' } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
if (!$taskSignTool) { throw 'Windows SDK SignTool.exe is required.' }
$taskNames = @('LHPad.exe','LHPadShell.dll','Scintilla.dll','Lexilla.dll','WebView2Loader.dll')
$taskPaths = @($taskNames | ForEach-Object {
    $taskPath = Join-Path $taskDirectory $_
    if (!(Test-Path -LiteralPath $taskPath -PathType Leaf)) { throw "Missing release file: $_" }
    $taskPath
})
& $taskSignTool sign /fd SHA256 /sha1 $taskCertificate.Thumbprint /s My /tr $TimestampUrl /td SHA256 /d 'LHPad distribution' @taskPaths
if ($LASTEXITCODE) { throw 'Release signing or timestamping failed; do not publish.' }
$taskReport = @()
foreach ($taskPath in $taskPaths) {
    $taskSignature = Get-AuthenticodeSignature -LiteralPath $taskPath
    if (!$taskSignature.SignerCertificate -or $taskSignature.SignerCertificate.Thumbprint -ne $taskCertificate.Thumbprint -or
        !$taskSignature.TimeStamperCertificate -or @('Valid','NotTrusted','UnknownError') -notcontains [string]$taskSignature.Status) {
        throw "Release signature verification failed: $([IO.Path]::GetFileName($taskPath))"
    }
    $taskReport += [ordered]@{
        file = [IO.Path]::GetFileName($taskPath)
        status = [string]$taskSignature.Status
        signer = $taskSignature.SignerCertificate.Subject
        thumbprint = $taskSignature.SignerCertificate.Thumbprint
        timestampSigner = $taskSignature.TimeStamperCertificate.Subject
        sha256 = (Get-FileHash -LiteralPath $taskPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
New-Item -ItemType Directory -Force (Join-Path $taskDirectory 'docs') | Out-Null
$taskReport | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $taskDirectory 'docs\signature-verification.json') -Encoding UTF8
$taskReport | ForEach-Object { Write-Output ("Signed: {0} / {1} / RFC3161 timestamp" -f $_.file,$_.status) }
