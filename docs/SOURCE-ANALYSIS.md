# Notepad++ / HxD 참조와 구현 결정

## 0.5.1 자동 들여쓰기

Scintilla `Editor.cxx`의 `NewLine()`과 `NotifyMacroRecord()`, `ScintillaWin.cxx`의 키 처리 순서를 동봉 소스에서 확인했습니다. Enter의 네이티브 처리는 줄바꿈 형식과 다중 선택 영역을 처리하고, 컨테이너가 Enter 이전에 확보한 선행 탭·공백을 각 새 줄에 이어 붙입니다. 줄 전체를 복사하지 않고 선행 구간을 최대 64KiB 청크로 읽습니다. CRLF/LF/CR, 줄 중간 분할, 기존 선택 영역과 커서 위치를 유지합니다.

줄바꿈과 자동 들여쓰기는 하나의 네이티브 undo 그룹입니다. 줄바꿈의 CR/LF·다중 커서별 녹화 알림과 생성한 들여쓰기 대신 하나의 `SCI_NEWLINE` 단계를 기록해 재생 때 대상 줄의 들여쓰기를 한 번 이어 줍니다. 붙여넣기·파일 적재·로그 수신은 Enter 명령 경로를 사용하지 않으므로 원래 바이트를 유지합니다. 기존 매크로 저장 형식은 유지합니다.

`Session.AutoIndent` 기본값은 true이며 기존 세션에 속성이 없어도 켜집니다. 편집 메뉴의 설정은 기존·신규 텍스트 탭에 적용하고 기존 원자적 세션 저장에 포함합니다.

## 0.5 코드 접기

[Notepad++ 공식 접기 설명](https://github.com/notepad-plus-plus/npp-usermanual/blob/master/content/docs/views.md#folding)의 언어별 fold margin, +/−, 펼쳐진 범위 연결선과 현재/전체/단계별 접기를 참고했습니다. 내장 Scintilla/Lexilla의 언어별 fold level과 자동 접기를 사용하므로 별도의 전체 텍스트 복사나 정규식 구조 파서를 만들지 않습니다. 기준 소스 ZIP의 `scintilla/doc/ScintillaDoc.html`, `lexilla/lexers/LexCPP.cxx`, `LexHTML.cxx`, `LexJSON.cxx`를 확인했습니다.

접기 여백은 행번호 옆에 배치하고 연결형 marker를 사용합니다. 언어 변경 시 이전 숨김 상태를 해제하며, 접기 조작은 파일 내용이나 내용 revision을 바꾸지 않습니다. 기존 정렬 단축키와 함께 사용할 수 있게 현재 구간 전환은 Ctrl+Alt+B, 전체와 단계별 동작은 Alt+숫자 및 Alt+Shift+숫자로 연결했습니다.

HTML/XML은 `fold=1`과 `fold.html=1`을 함께 설정합니다. Notepad++ `ScintillaEditView.cpp`의 `setHtmlLexer`와 Lexilla `LexHTML.cxx`의 `foldHTML` 기본값을 확인했습니다. `LineMarker.cxx`의 연결선은 marker의 BACK 색상을 사용하므로 배경과 구분되는 회청색을 적용했습니다. 언어 변경의 `ClearDocumentStyle`은 네이티브 `ShowAll`을 통해 숨김·접힘 상태를 초기화하며, 전체 문서 재분석을 요청하지 않습니다.

## 0.3 매크로

[Scintilla의 매크로 기록 API](https://www.scintilla.org/ScintillaDoc.html#SCI_STARTRECORD)인 `SCI_STARTRECORD`, `SCI_STOPRECORD`, `SCN_MACRORECORD`를 사용합니다. 네이티브 알림의 문자열 포인터는 알림 처리 중 UTF-8 바이트로 복사하며 파일에 포인터를 저장하지 않습니다. 실행은 Scintilla의 녹화 가능 메시지 목록으로 제한합니다.

Notepad++ 기준 소스의 `NppNotification.cpp`에서 CR/LF 입력을 `SCI_NEWLINE`으로 정규화하는 부분과 `WinControls/shortcut/shortcut.cpp`의 문자열 복사·실행 허용 목록, `NppBigSwitch.cpp`의 문서 끝 반복 종료 조건을 참고했습니다. LitePad는 검색·바꾸기·정렬·열 편집을 의미 단위의 별도 단계로 저장합니다. 문서 끝까지 반복할 때 남은 텍스트가 감소하거나 줄이 삭제되는지 확인하고, EOF 선택 상태와 검색 실패를 구분합니다.

반복 전체는 하나의 undo 그룹입니다. UI에 주기적으로 실행을 양보하고 Esc/중지 토큰을 확인합니다. 실행 중 원문 입력과 다른 파일 조작을 비활성화하며, 종료 요청은 실행 취소 후 복원 백업을 저장합니다. 매크로 저장소는 별도 `macros.json`과 직전 세대 `.bak`이며 기존 탭 복원 형식은 유지합니다.

## 0.2 Markdown / HTML / SVG 미리보기

Markdown 파서는 [Markdig](https://github.com/xoofx/markdig) 1.4.2, 미리보기 호스트는 [Microsoft WebView2 WinForms](https://learn.microsoft.com/en-us/microsoft-edge/webview2/get-started/winforms) SDK 1.0.4258.31입니다. 원문 편집에는 기존 Scintilla를 유지합니다. WebView2 컨트롤은 미리보기를 켤 때만 생성하며 시스템에 설치된 런타임을 사용합니다.

원문·미리보기·분할은 `DocumentSurface`에서 관리합니다. Markdown은 HTML로 변환하고 HTML은 작성한 원문을 유지합니다. SVG는 이미지 요소로 표시합니다. 문서 수정 후 400ms 대기하여 다시 렌더링하고 보기 모드를 세션에 저장합니다. 미리보기 한도는 UTF-8 원문 4 MiB입니다.

저장 전 내용을 렌더링하기 위해 브라우저의 `WebResourceRequested`에서 메모리의 HTML을 응답합니다. [Microsoft 로컬 콘텐츠 문서](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/working-with-local-content)의 요청 처리 방식을 따르며, CSS·이미지 등은 문서 디렉터리의 상대 경로로 제공합니다. 서버 프로세스나 원본 옆의 임시 HTML 파일은 만들지 않습니다. JavaScript는 기본 꺼짐이며 HTML 탭에서만 명시적으로 활성화할 수 있습니다.

WebView2 SDK가 함께 참조하는 WPF 어셈블리는 사용하지 않으므로 빌드 참조에서 제외합니다. `packages.lock.json`에 패키지 버전과 콘텐츠 해시를 기록합니다.

분석일: 2026-10-23. 로컬 Notepad++: `C:\Program Files\Notepad++\notepad++.exe`, 파일 버전 8.9.8.1.

공식 소스: https://github.com/notepad-plus-plus/notepad-plus-plus

고정 커밋: `d0db704c35aba374a934cf784d85367b615fbb02`.

## 소스에서 확인한 내용

| 요구 사항 | Notepad++ 근거 | LitePad 반영 |
|---|---|---|
| 편집·선택 | `PowerEditor/src/ScintillaComponent/ScintillaEditView.cpp`, `NppBigSwitch.cpp`, `scintilla/include/Scintilla.h` | 같은 Scintilla 제어를 직접 호스팅. 64비트 위치, 사각·다중 선택·각 커서 붙여넣기. |
| 대용량 | `ScintillaComponent/Buffer.cpp`의 large file 분기와 `Parameters.cpp`의 largeFileRestriction, 기본 200 MB 스타일링 제한 | 200 MiB 이후 null lexer. 청크 디코딩·입출력, UI 양보, undo 수집 없이 초기 읽기. |
| 복원 | `NppIO.cpp`의 loadSession/saveSession, Buffer.cpp의 백업 흐름, `Parameters.cpp`의 세션 파싱 | 별도 문서 스냅샷 + JSON 메타데이터, 원자적 세션 교체, 직전 세대 유지, 미저장 탭·선택 복원. |
| 구문 강조 | `ScintillaEditView::_langNameInfoArray`, `stylers.model.xml`, `langs.model.xml`, `lexilla/` | 원본 언어→lexer 매핑과 기본 스타일·키워드 포함. 주요 C++/JS/HTML keyword slot 차이 반영. 일부 언어별 추가 property/UDL 편집기는 미구현. |
| 인코딩 | `PowerEditor/src/EncodingMapper.cpp`의 활성 코드 페이지 표 | Windows 125x, OEM DOS, ISO, 한국어·일본어·중국어·키릴 계열. UTF-8/16/32, BOM, 엄격한 변환. ISO-8859-14는 Unicode 원본 매핑 사용. |
| 정규식 | `boostregex/BoostRegexSearch.cxx`, `scintilla/include/BoostRegexSearch.h`, FindReplaceDlg.cpp의 empty-match 플래그 | 원본 Boost.Regex 포함 빌드. zero-width 치환·캡처 치환 검사. 오류 반환 -2/-3 처리. |

Npp 소스를 그대로 복제한 축소판이 아니라 Scintilla/Lexilla를 호스팅하는 별도 앱입니다. 이미 설치된 .NET 10 Windows Desktop를 활용하는 얇은 WinForms 셸을 택했습니다. 브라우저 런타임은 포함하지 않습니다. 구성 데이터와 Npp 수정 엔진을 재사용하므로 프로젝트 라이선스는 GPL-3.0-or-later로 표시합니다.

## HxD

공식 참조: https://mh-nexus.de/en/hxd/

오프셋 / Hex 바이트 / decoded text의 3영역, 수정 바이트 색상, 삽입·덮어쓰기, 찾기, 주소 이동, undo를 UX 기준으로 삼았습니다. HxD의 소스나 자산을 복제하지 않았습니다. LitePad의 Hex 코드는 독립적인 piece table과 가상 스크롤 렌더러입니다. 원본 데이터는 디스크에 남고, 변경 조각만 메모리에 저장됩니다. 큰 파일 저장·백업·찾기는 조각을 청크로 스트리밍합니다.

데이터 검사는 8/16/32/64비트 정수·32/64비트 실수를 Little/Big endian으로 보여줍니다. 원시 디스크·프로세스 메모리 접근은 범위에 포함하지 않았습니다.

## 검증 해석

256 MiB ASCII 텍스트 읽기·저장 후 SHA-256 동일성을 확인하고, 읽는 동안 WinForms 타이머가 작동하는지 검사합니다. 3 GiB 바이너리는 파일 길이 확장 후 끝 위치에 바이트를 기록하여 2 GiB 경계를 넘는 위치 계산과 지연 읽기를 검사합니다. 모든 내용이 채워진 3 GiB 파일에 대한 처리량 측정은 아닙니다. 처리 속도·메모리 수치는 테스트 장치와 파일의 줄 수에 따라 달라집니다.
