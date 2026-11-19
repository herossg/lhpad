# LitePad 1.0.0 네이티브 전환

2026-10-26 (Asia/Seoul).

긴 한글 파일 경로와 깊은 설치 폴더에서는 Windows의 확장 경로 접두사를 내부 파일 I/O에 적용합니다. OS의 LongPathsEnabled 설정을 변경하지 않습니다.

실행 프로그램을 C# WinForms에서 C++20 / Win32로 옮겼습니다. 기존 C# 소스(`src/`, `LitePad.csproj`)와 0.5.2 배포 폴더는 보존했습니다. 기본 `build.ps1`은 이제 C++ 버전을 빌드하며, `-CSharp`를 지정할 때만 이전 관리 코드 빌드를 실행합니다.

## 실행과 빌드

- 배포 폴더: `artifacts/native-1.0.0/`
- ZIP: `artifacts/LitePad-native-windows-x64.zip`
- 전체 소스: `artifacts/LitePad-source.zip`
- C++ 소스와 제3자 소스: `cpp/`
- Visual Studio C++ 데스크톱 도구와 Windows SDK를 사용합니다. CMake 경로도 제공하며 .NET SDK나 NuGet 복원이 필요하지 않습니다.
- 실행 파일, Scintilla.dll, Lexilla.dll은 `/MT`로 빌드합니다. WebView2Loader.dll도 별도의 VC 런타임 DLL을 요구하지 않습니다. 실제 PE 의존성은 `native-dependencies.txt`에 기록했습니다.
- .NET을 설치하거나 다운로드하지 않습니다. Markdown·HTML·SVG 미리보기에는 별도로 Microsoft Edge WebView2 Runtime을 사용하며, 일반 텍스트·Hex 편집에서는 브라우저를 생성하지 않습니다.

## 드래그 앤 드롭

Scintilla의 기존 OLE 텍스트 드래그 처리를 유지하면서 `CF_HDROP` 파일 형식을 추가했습니다. `cpp/patch-scintilla.ps1`을 원본 소스에 적용하고 엔진을 다시 빌드합니다. 파일 목록은 Unicode 경로에서 UTF-8로 변환해 `SCN_URIDROPPED` 알림으로 전달하고, 부모 창이 알림 처리 후 파일을 엽니다. 편집기 내부의 선택 텍스트 이동과 파일 열기를 구분합니다.

탭·상태 표시줄·Hex 등 다른 영역에는 Windows 파일 드롭 메시지를 연결했습니다. 여러 파일과 한글·공백 경로를 지원하고, 폴더는 건너뜁니다. 이미 열린 경로는 해당 탭으로 이동합니다. 자동 검사에서는 실제 `DoDragDrop` / `IDataObject` / `CF_HDROP` 경로를 사용해 네이티브 편집기 위로 파일 두 개를 떨어뜨렸습니다.

## 탐색기 우클릭

현재 사용자 레지스트리 `HKCU\Software\Classes\*\shell\LitePad`에 `LitePad로 열기`를 등록합니다. 실행 명령은 실행 파일과 대상 파일 경로를 각각 큰따옴표로 감쌉니다. `MultiSelectModel=Document`를 지정하며 관리자 권한과 기본 파일 연결 변경은 필요하지 않습니다. Windows 11에서는 기존 확장 메뉴인 **더 많은 옵션 표시**에 나타납니다.

`LitePad.exe --register-shell` / `--unregister-shell`과 도구 메뉴에서 등록·제거합니다. 실행 파일을 옮긴 경우 새 위치에서 다시 등록해야 합니다.

실행 중인 창은 정규화한 프로필 경로에 해당하는 Windows 속성으로 찾습니다. 후속 프로세스는 `WM_COPYDATA`로 UTF-8 JSON 파일 목록을 전달한 뒤 종료합니다. 수신 창은 UI 메시지 큐에서 파일을 엽니다. 프로필별 named mutex와 기존 `.lock` 파일 잠금을 함께 사용해 C# 버전 또는 다른 프로세스가 같은 복구 자료를 덮어쓰지 못하게 합니다.

## 기존 자료 복원

기본 프로필은 기존과 같은 `%LOCALAPPDATA%\LitePad`입니다. 세션·창 설정·매크로 JSON의 Version 1 형식을 유지합니다. C#의 base64 매크로 문자열, UTF-8 복구 스냅샷, 원본 파일 인코딩, 여러 선택 영역, 줄바꿈·줄바꿈 표시·배율·자동 들여쓰기 설정을 읽습니다. 복구 스냅샷은 원본 파일에 덮어쓰지 않습니다.

첫 네이티브 실행 시 기존 JSON 파일과 `.bak`를 `legacy-csharp-backup`에도 복사합니다. 이후 저장은 임시 파일과 Windows 원자적 교체를 사용합니다. 손상된 세션은 `.bak`로 다시 시도하고, 모두 읽지 못하면 기존 자료를 보존한 별도 프로필을 사용합니다. 열지 못한 탭 항목도 다음 세션에 남깁니다.

대용량 파일은 읽는 동안 편집기 화면 갱신과 스크롤 폭 추적을 멈추고 청크 단위로 추가합니다. 진행 표시는 계속 갱신하며 UI 메시지를 처리합니다. 저장 도중 문서 내용이 바뀌면 원본 파일 교체 전에 중단합니다. Hex의 원본 바이트는 디스크에 두고 piece table로 변경 부분만 저장하며, 검색은 UI에서 중지할 수 있습니다.

## 렌더러와 범위

구문 강조·검색 엔진은 기존 Scintilla / Lexilla / Boost.Regex 빌드를 사용합니다. Markdown은 네이티브 md4c의 GitHub 문법, JSON은 nlohmann/json, XML은 pugixml로 처리합니다. Markdown의 표·할 일 목록·코드 블록·원문 HTML을 지원하지만 Markdig의 모든 확장 문법과 출력이 동일한 것은 아닙니다. 예를 들어 각주 확장은 같은 방식으로 렌더링하지 않습니다.

HTML 미리보기는 기본적으로 문서 스크립트를 실행하지 않으며 탭별 메뉴에서 켭니다. 상대 CSS·이미지는 해당 문서 폴더 안의 실제 파일만 제공합니다. 미리보기는 기존과 같이 4 MiB, 정렬과 비교는 각각 32 MiB를 기준으로 제한합니다. 비교는 합계 400,000줄까지이며 원본과 분리된 읽기 전용 정렬 스냅샷입니다. ISO-2022 계열은 현재 네이티브 변환 경로에서 64 MiB 이하로 제한합니다.

검증 로그와 실제 창 이미지는 `native-results.txt`, `native-markdown-split.png`, `native-html-preview.png`, `native-hex.png`, `native-compare.png`, `native-about.png`를 참고하세요.
