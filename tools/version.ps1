# cpp/version.h의 버전을 읽는다. 사용: . (Join-Path $PSScriptRoot 'version.ps1'); Get-LHPadVersion
function Get-LHPadVersion {
    param([string]$Root = (Split-Path -Parent $PSScriptRoot))
    $taskHeader = Get-Content -Raw -LiteralPath (Join-Path $Root 'cpp/version.h')
    $taskParts = foreach ($taskName in 'MAJOR', 'MINOR', 'PATCH') {
        if ($taskHeader -notmatch "(?m)^#define LHPAD_VERSION_$taskName\s+(\d+)") { throw "cpp/version.h에 LHPAD_VERSION_$taskName 정의가 없습니다." }
        $Matches[1]
    }
    $taskParts -join '.'
}
