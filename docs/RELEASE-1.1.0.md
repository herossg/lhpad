# LHPad 1.1.0

LitePad를 LHPad로 변경했습니다. Windows 11 첫 우클릭 메뉴에 `LHPad로 열기`를 추가합니다.

- 창 제목, 정보 창, 실행 파일, 아이콘 파일, C# 보관 소스와 빌드 출력 이름을 변경했습니다.
- 기존 `%LOCALAPPDATA%\LitePad` 프로필과 잠금 파일을 유지해 세션·미저장 문서·매크로를 보존합니다. 이전 버전 창은 먼저 닫아 주세요.
- 독립 COM 클래스 `{D77FB6F2-1C04-4E31-9868-9CB139F3D2C8}`와 `LHPad.Explorer` sparse MSIX를 사용합니다. LHZip과 메뉴 등록이 충돌하지 않습니다.
- 여러 파일을 한 번에 새 탭으로 전달합니다. 공백·한글·`&` 경로를 지원하며 Windows 명령행 제한보다 큰 선택은 나누어 전달합니다.
- `Register-LHPad.cmd`는 배포 파일 서명을 검증하고, 필요한 경우 개발 인증서만 관리자 권한으로 신뢰 등록한 뒤 실제 사용자 탐색기에서 메뉴를 등록합니다.
- EXE, 셸 DLL, Scintilla.dll, Lexilla.dll, WebView2Loader.dll을 LHZip 인증서 `537D10B46D7742E859498D5642863AEC947973CB`로 서명하고 RFC3161 타임스탬프를 포함했습니다. MSIX도 같은 인증서로 서명했습니다. 공개 인증서만 ZIP에 포함합니다.
- 기존 LitePad 우클릭 메뉴를 교체합니다. `--unregister-shell`로 새 메뉴와 패키지를 제거할 수 있습니다.

기술 근거: [Microsoft의 IExplorerCommand / Windows 11 메뉴 등록 문서](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/integrate-packaged-app-with-file-explorer).

검증 기록: 배포 ZIP의 `docs/signature-verification.json`, `docs/shell-test-results.txt`, `docs/native-results.txt`.
