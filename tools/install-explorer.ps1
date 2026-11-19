[CmdletBinding()]
param(
    [string]$Directory = (Split-Path -Parent $PSScriptRoot),
    [switch]$CertificateOnly,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
# Pinned to the public certificate shipped with this LHPad development release.
# Update this value deliberately if the project's signing key is rotated.
$taskExpectedThumbprint = '537D10B46D7742E859498D5642863AEC947973CB'

function Test-LHPadAdministrator {
    $taskIdentity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $taskPrincipal = New-Object Security.Principal.WindowsPrincipal($taskIdentity)
    return $taskPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Test-LHPadCertificateTrusted {
    $taskStore = New-Object Security.Cryptography.X509Certificates.X509Store('TrustedPeople','LocalMachine')
    try {
        $taskStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadOnly)
        return @($taskStore.Certificates | Where-Object { $_.Thumbprint -eq $taskExpectedThumbprint }).Count -gt 0
    } finally { $taskStore.Close() }
}

try {
    $taskDirectory = (Resolve-Path -LiteralPath $Directory).ProviderPath
    if (!(Test-Path -LiteralPath $taskDirectory -PathType Container)) { throw '배포 폴더를 찾을 수 없습니다.' }
    foreach ($taskName in @('LHPad.exe','LHPadShell.dll','Scintilla.dll','Lexilla.dll','WebView2Loader.dll','LHPad.Explorer.msix',
                            'LHPad.Development.cer','Assets\LHPad.png','tools\register-explorer.ps1')) {
        if (!(Test-Path -LiteralPath (Join-Path $taskDirectory $taskName) -PathType Leaf)) {
            throw "배포 파일이 없습니다: $taskName. ZIP 전체를 먼저 풀어 주세요."
        }
    }
    $taskCertificatePath = Join-Path $taskDirectory 'LHPad.Development.cer'
    $taskCertificate = New-Object Security.Cryptography.X509Certificates.X509Certificate2($taskCertificatePath)
    if ($taskCertificate.Thumbprint -ne $taskExpectedThumbprint -or $taskCertificate.Subject -ne 'CN=LHZip Development') {
        throw '제공된 인증서가 LHPad 배포 인증서와 다릅니다. 등록하지 않았습니다.'
    }
    $taskNow = Get-Date
    if ($taskCertificate.NotBefore -gt $taskNow -or $taskCertificate.NotAfter -lt $taskNow) {
        throw 'LHPad 개발 인증서의 유효기간이 아닙니다. 새 배포본이 필요합니다.'
    }
    $taskSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $taskDirectory 'LHPad.Explorer.msix')
    if (!$taskSignature.SignerCertificate -or $taskSignature.SignerCertificate.Thumbprint -ne $taskExpectedThumbprint -or
        @('Valid','NotTrusted','UnknownError') -notcontains [string]$taskSignature.Status) {
        throw 'LHPad 셸 패키지의 서명자가 다르거나 서명이 손상되었습니다. 등록하지 않았습니다.'
    }
    foreach ($taskBinary in @('LHPad.exe','LHPadShell.dll','Scintilla.dll','Lexilla.dll','WebView2Loader.dll')) {
        $taskBinarySignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $taskDirectory $taskBinary)
        if (!$taskBinarySignature.SignerCertificate -or $taskBinarySignature.SignerCertificate.Thumbprint -ne $taskExpectedThumbprint -or
            !$taskBinarySignature.TimeStamperCertificate -or @('Valid','NotTrusted','UnknownError') -notcontains [string]$taskBinarySignature.Status) {
            throw "배포 파일의 서명이 다르거나 손상되었습니다: $taskBinary"
        }
    }
    Write-Host "배포 폴더: $taskDirectory"
    Write-Host "인증서: $($taskCertificate.Subject)"
    Write-Host "지문: $taskExpectedThumbprint"
    if ($CheckOnly) {
        Write-Host '점검 완료. 인증서·메뉴·시스템 설정은 변경하지 않았습니다.'
        exit 0
    }
    $taskWindowsInfo = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Microsoft\Windows NT\CurrentVersion'
    $taskWindowsProduct = Get-ItemPropertyValue -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Control\ProductOptions' -Name ProductType
    $taskWindows11 = $taskWindowsProduct -eq 'WinNT' -and ([int]$taskWindowsInfo.CurrentMajorVersionNumber -gt 10 -or [int]$taskWindowsInfo.CurrentBuildNumber -ge 22000)
    if ($CertificateOnly -and !(Test-LHPadAdministrator)) { throw '인증서 등록 단계에는 관리자 권한이 필요합니다.' }
    if ($taskWindows11 -and !(Test-LHPadCertificateTrusted)) {
        Write-Host '이 인증서로 서명한 패키지를 신뢰하도록 컴퓨터 TrustedPeople 저장소에 등록합니다.'
        if (Test-LHPadAdministrator) {
            $taskStore = New-Object Security.Cryptography.X509Certificates.X509Store('TrustedPeople','LocalMachine')
            try {
                $taskStore.Open([Security.Cryptography.X509Certificates.OpenFlags]::ReadWrite)
                $taskStore.Add($taskCertificate)
            } finally { $taskStore.Close() }
        } else {
            Write-Host '관리자 권한 확인 창에서 승인해 주세요. 취소하면 설치를 중단합니다.'
            $taskPowerShell = Join-Path $env:WINDIR 'System32\WindowsPowerShell\v1.0\powershell.exe'
            $taskArguments = '-NoLogo -NoProfile -ExecutionPolicy Bypass -File "' + $PSCommandPath +
                             '" -Directory "' + $taskDirectory + '" -CertificateOnly'
            $taskChild = Start-Process -FilePath $taskPowerShell -ArgumentList $taskArguments -Verb RunAs -WindowStyle Hidden -Wait -PassThru
            if ($taskChild.ExitCode -ne 0) { throw '인증서 등록이 실패했거나 관리자 승인이 취소되었습니다.' }
        }
        if (!(Test-LHPadCertificateTrusted)) { throw '컴퓨터 TrustedPeople 인증서 등록을 확인하지 못했습니다.' }
    }
    if ($CertificateOnly) {
        Write-Host '인증서 등록 완료.'
        exit 0
    }
    if ($taskWindows11) {
        $taskSignature = Get-AuthenticodeSignature -LiteralPath (Join-Path $taskDirectory 'LHPad.Explorer.msix')
        if ($taskSignature.Status -ne 'Valid') { throw '인증서 등록 후에도 셸 패키지 서명을 신뢰할 수 없습니다.' }
    }
    # Certificate import is the only elevated stage. Register the menu through
    # desktop Explorer so it belongs to the interactive user, even with a
    # different account used for the administrator credential prompt.
    $taskStatusPath = Join-Path $taskDirectory 'shell-package-status.txt'
    $taskPreviousStamp = if (Test-Path -LiteralPath $taskStatusPath) { (Get-Item -LiteralPath $taskStatusPath).LastWriteTimeUtc.Ticks } else { 0 }
    & (Join-Path $taskDirectory 'tools\register-explorer.ps1') -Executable (Join-Path $taskDirectory 'LHPad.exe') -Quiet
    $taskDeadline = (Get-Date).AddSeconds(30)
    $taskConfirmed = $false
    do {
        if (Test-Path -LiteralPath $taskStatusPath) {
            $taskStamp = (Get-Item -LiteralPath $taskStatusPath).LastWriteTimeUtc.Ticks
            if ($taskStamp -ne $taskPreviousStamp) {
                $taskResult = [IO.File]::ReadAllText($taskStatusPath)
                if (($taskResult.StartsWith('Explorer menu registration checked successfully.') -or $taskResult.StartsWith('Windows 11 menu registration checked successfully.'))) {
                    $taskConfirmed = $true
                    break
                }
                if ($taskResult.Length -gt 0) { throw "탐색기 메뉴 등록 실패: $taskResult" }
            }
        }
        Start-Sleep -Milliseconds 250
    } while ((Get-Date) -lt $taskDeadline)
    if (!$taskConfirmed) { throw '메뉴 등록 결과를 확인하지 못했습니다. 쓰기 가능한 폴더에 ZIP을 풀고 탐색기 폴더 창을 연 상태에서 다시 실행해 주세요.' }
    Write-Host 'LHPad 탐색기 메뉴 등록 완료.' -ForegroundColor Green
    Write-Host '우클릭 메뉴를 다시 열어 주세요. 처음 등록해도 보이지 않으면 복사·이동 작업을 마친 뒤 탐색기를 다시 시작하거나 로그아웃 후 로그인하세요.'
    Write-Host '배포 폴더를 이동하거나 삭제하면 메뉴가 작동하지 않습니다. 이동한 경우 새 위치에서 이 스크립트를 다시 실행하세요.'
    exit 0
} catch {
    Write-Host ('실패: ' + $_.Exception.Message) -ForegroundColor Red
    exit 1
}
