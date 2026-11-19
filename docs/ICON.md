# LitePad 아이콘 제작 기록

## 0.4.6 투명 배경 타자기 앱·탭 아이콘

내장 ImageGen 생성 도구로 메인 아이콘을 파스텔 민트·하늘색 빈티지 타자기로 새로 만들었습니다. 아이보리 종이와 건반, 짙은 회청색 롤러·캐리지, 얇고 선명한 윤곽선으로 형태를 구분합니다. 바깥 색상 바탕이나 사각형 테두리 없이 타자기 바깥과 캐리지 주변은 실제 알파 투명 배경입니다. 16/20/24/32픽셀에서도 알아보기 쉬운 간결한 형태이며, 잘리지 않도록 투명 여백을 둡니다.

저장 원본은 `data/LitePad-icon.png`, 실행 파일과 창에 적용하는 파일은 `data/LitePad.ico`입니다. `tools/build-icon.ps1`로 변환하며 16/20/24/32/40/48/64/128/256픽셀의 9개 프레임 구성은 유지합니다. 16/20픽셀에서는 축소 필터가 바깥 경계에 닿지 않게 1픽셀 안쪽에 그립니다.

문서 탭의 저장 원본 `data/tab-saved-source.png`는 메인 타자기와 같습니다. 미저장 원본 `data/tab-unsaved-source.png`는 내장 ImageGen 편집 도구로 본체만 파스텔 빨강 계열로 변경했습니다. 종이·건반·금속 부분과 배경은 유지하며, 두 상태 모두 타자기 바깥은 실제 알파 투명입니다. `tools/build-tab-icons.ps1`로 실행용 64픽셀 `data/tab-saved.png`, `data/tab-unsaved.png`를 만듭니다. `UiImages.DocumentIcon`은 이전 사각형 프레임·안쪽 자르기를 제거하고 전체 타자기를 투명 1픽셀 여백 안에 표시 크기별로 캐시합니다.

### 최종 프롬프트: Main

> Use case: logo-brand. Asset type: LitePad Windows text editor APPLICATION ICON. Create one clean, distinctive compact vintage TYPEWRITER icon on a truly transparent square canvas. The typewriter is the ONLY object, with one upright ivory-white sheet inserted in its carriage, a broad compact pastel powder-blue / pale mint-blue body, a dark slate platen roller and carriage bar with subtle metal ends, a simple keyboard of a few clearly separated rounded ivory key shapes in three short rows, and one long spacebar. Put just two short muted-gray writing strokes on the paper, no actual letters. Front-facing, very slight top view so keyboard is visible, nearly symmetrical with a simple recognizable typewriter silhouette. Flat Windows desktop icon style with restrained light shading, thin but crisp dark slate/near-black contours, refined and friendly, not photorealistic, not glossy 3D. Simplify details aggressively so it remains recognizable at 16,20,24,32px. Use pastel blue, ivory and gray, with at most a tiny pastel coral accent. Center the complete visible typewriter silhouette in the square, use approximately 90–94% of available canvas width/height with safe transparent margins on all sides, no clipping. Preserve negative transparent areas around and between the carriage ends and body. TRUE alpha transparent background: no green or colored backing tile, no enclosing border or app-icon squircle, no backdrop, no checkerboard pixels, no ground shadow, no glow. No pencil, no separate notebook, no logo, no text, no caption, no watermark. A single isolated typewriter, no multiple variants.

### 최종 프롬프트: 미저장 탭

> Use case: precise-object-edit. Asset type: unsaved/modified DOCUMENT TAB icon for LitePad editor. Edit target: this exact pastel mint-blue vintage typewriter icon. Change ONLY the painted mint/cyan body shell surfaces to a soft pastel rose RED / light salmon-red color family (target around #F2A5A5, red mixed with white). Preserve subtle body shading and highlights. This is an editing-state variant and must match the original geometry: keep the EXACT same square canvas dimensions, scale, centering, silhouette, keyboard, key spacing, paper sheet, paper writing strokes, rollers, lever, metal pieces and thin dark contours. Keep ivory keys and paper, gray/slate metal and outlines UNCHANGED. Do not recolor metal, keys, paper or its text strokes. CRITICAL: preserve genuine transparent alpha everywhere outside the typewriter and in all empty gaps. There must be NO colored background, no square frame, no new badge or dot, no checkerboard, no shadow, no text. The only visual difference from the original is pastel rose-red TYPEWRITER BODY instead of mint-blue BODY; not a red backdrop. The body must read clearly as pastel RED rather than vivid red, orange, magenta or purple. Single isolated icon, same composition.

## 0.4.5 파스텔 도구 모음

상단 버튼 17개는 아이콘만 표시하며, 한국어 기능명과 단축키는 툴팁·접근성 이름으로 제공합니다. 파일/폴더/저장/검색/바꾸기/정렬/비교/원문/미리보기/분할/모니터링/끝 따라가기/중지/실행/반복/도움말 16종은 `src/ToolbarIcons.cs`의 GDI+ 도형으로 그립니다. 파스텔 파랑·민트·보라·분홍·노랑 채움과 짙은 회청색 윤곽선을 사용합니다. 녹화는 기존 파스텔 파랑/빨강 원을 유지합니다.

도구 모음의 논리 이미지 크기는 24px이며 WinForms가 DPI에 맞춰 표시합니다. 그림 원본은 여백을 포함한 64px 투명 비트맵으로 캐시하여 상태 갱신 때 다시 생성하지 않습니다. 새 외부 그림 파일이나 글꼴 의존성은 없습니다. 기존 앱·문서 탭 아이콘의 ImageGen 디자인은 유지합니다.

## 0.4.4 작은 탭 아이콘 그리기 보정

이미지 디자인은 0.4.3 원본을 유지합니다. `UiImages.DocumentIcon`에서 실제 표시 픽셀 크기별 아이콘을 캐시하고 `DrawImageUnscaled`로 그려 이중 축소를 제거했습니다. 원본의 얇은 외곽 프레임 안쪽을 한 번 축소한 뒤, 네 방향의 외곽선을 불투명한 1픽셀로 맞추고 그 바깥에 투명 1픽셀을 확보합니다. 종이·연필 이미지와 저장/미저장 배경은 유지합니다.

16/20/24/30/32/40픽셀 각각에서 저장/미저장 아이콘의 네 변과 투명 바깥 여백을 확인했습니다. 실제 창 화면은 `docs/window-tabs-preview.png`입니다.

## 0.4.3 개별 검은 윤곽선

내장 ImageGen 편집 모드로 기존 앱·저장 탭·미저장 탭 아이콘을 각각 수정했습니다. 전체 둥근 사각형, 종이와 접힌 모서리, 연필에 검은 테두리를 따로 추가했습니다. 저장 탭의 프레임 안 배경은 투명하며, 앱의 청록색과 미저장 탭의 파스텔 빨강은 유지합니다.

저장 파일: `data/LitePad-icon.png`, `data/tab-saved-source.png`, `data/tab-unsaved-source.png`. 실행용 변환: `data/LitePad.ico`(16~256px 9종), `data/tab-saved.png`, `data/tab-unsaved.png`(64px). 디자인 수정은 ImageGen에서 하고 기존 PowerShell 도구로 크기·포맷만 변환했습니다.

### 최종 프롬프트: Main

> Edit target: the attached LitePad app icon. Preserve its exact canvas, proportions, layout, white folded paper, gray writing and bullets, orange pencil, teal rounded-square background and existing colors. Add THREE DISTINCT thin crisp black outlines: (1) around the full outside boundary of the rounded teal square; (2) around the white paper silhouette, including clear black fold edges at the upper-right paper corner; (3) around the entire pencil silhouette (including eraser, metal ferrule, orange shaft, wood and graphite tip), so that the pencil has its OWN separate black contour wherever it overlaps the white paper. Outline only these boundaries; keep gray writing and bullets gray, do not outline each writing line or bullet. Use a restrained clean Windows icon stroke, thin but visibly defined at 16-32px display, not a thick cartoon outline. Do not redesign, resize or rearrange anything. Keep actual alpha transparency outside the rounded square. No text, no new badges, no external shadow.

### 최종 프롬프트: Saved

> Edit target: image 1, the LitePad saved-document tab icon (paper and pencil with transparent surroundings). Image 2 is only a geometry/color reference showing the original application's rounded square frame. Preserve image 1's exact canvas, scale, white folded paper, gray writing and bullets, orange pencil, and all positions. Add THREE DISTINCT thin crisp black outlines: (1) a rounded-square OUTER FRAME at exactly the same position and size as the teal square in reference image 2, with completely TRANSPARENT fill (do NOT add the teal background); (2) around the white paper silhouette and its folded upper-right corner, including separate fold edges; (3) around the entire pencil silhouette, including eraser, ferrule, shaft, wood and tip, so its own black contour remains distinct over the white paper. Thin clean Windows icon strokes, visibly defined at 16-32px, not bold cartoon. The interior around the paper/pencil must remain true transparent alpha. Keep gray text lines and bullets gray without added outlines on each one. No redesign, no layout change, no colored backing, no new text, no badge or shadow.

### 최종 프롬프트: Unsaved

> Edit target: the attached LitePad unsaved-document tab icon. Preserve its exact canvas, proportions, placement, white folded paper, gray writing lines and bullets, orange pencil, and soft pastel-pink rounded-square background. Add THREE DISTINCT thin crisp black outlines: (1) around the entire outside boundary of the pastel-pink rounded square; (2) around the white paper silhouette, including clear separate black edges of the upper-right folded corner; (3) around the complete pencil silhouette including eraser, metal ferrule, orange shaft, wood and graphite tip. The pencil must have its OWN black contour, distinct where it crosses the paper. Keep strokes thin, clean and consistently visible at 16-32px Windows icon size, not thick cartoon. Keep gray writing and bullets gray; do not outline each writing line or bullet. Keep the existing PASTEL pink color, not vivid red. Maintain true alpha transparency outside the rounded square. No new objects, no text, no badges, no shadow, no layout or color changes.


## 0.3.1 문서 탭 상태 아이콘

기존 `data/LitePad-icon.png`를 편집 대상으로 내장 ImageGen을 사용했습니다. 저장 상태는 `data/tab-saved-source.png`(종이·연필 바깥 투명), 미저장 상태는 `data/tab-unsaved-source.png`(파스텔 빨강 배경)입니다. `tools/build-tab-icons.ps1`은 디자인 변경 없이 64px PNG `data/tab-saved.png`, `data/tab-unsaved.png`로 축소합니다. 원본은 소스 배포에, 64px 파일은 실행 배포에 포함합니다.

녹화 표시 원은 코드에서 단색으로 그립니다. 대기 #A9D3F5, 녹화 중 #FFB3B3. 버튼 문구는 바뀌지 않습니다.

최종 프롬프트(저장 상태):

> Edit target: attached LitePad application icon. Make a clean saved-document tab icon by removing ONLY the teal rounded-square background entirely to true transparent alpha. Preserve the white folded paper, gray lines/bullets and orange pencil exactly in same shape, color, proportions and position as original, no redesign, no added objects, no shadows outside. Keep original square canvas and centered placement. This is for a small desktop document-tab status icon. All pixels outside the paper and pencil should be transparent.

최종 프롬프트(미저장 상태):

> Edit target: attached LitePad application icon. Produce its unsaved-document tab status variant. Change ONLY the teal rounded-square background to a flat soft PASTEL PINK/RED #FFB3B3 (red mixed halfway with white). Preserve the original white folded paper, gray text lines and bullets, orange pencil, shape, position, proportions and original square canvas exactly, no redesign. Transparent alpha outside the rounded square. Clean readable small desktop icon, no new objects, badges, text, glow or external shadow. Background must look pastel light red, not vivid red, orange, magenta or teal.

## 메인 아이콘 제작 기록

입력: 사용자가 첨부한 Windows shell32 문서·연필 아이콘 스크린샷.

제작: 내장 ImageGen 편집 도구, 투명 배경. 문서·접힌 모서리·회색 글줄·주황색 연필 구성을 유지하고 파란 배경을 LitePad 청록색으로 바꾼 파생 디자인입니다.

결과: `data/LitePad-icon.png`. `tools/build-icon.ps1`에서 디자인 변경 없이 PNG를 크기별로 변환하여 `data/LitePad.ico`를 생성합니다. 크기는 16, 20, 24, 32, 40, 48, 64, 128, 256픽셀입니다. PNG는 소스 패키지에, ICO는 소스와 실행 패키지에 포함합니다.

사용한 최종 프롬프트:

> Edit the attached small Windows document-and-pencil icon to create the application icon for LitePad. Preserve its recognizable design: a white paper document with folded upper-right corner, fine grey writing lines and tiny grey bullets, overlaid by an orange/yellow pencil diagonally from lower left to upper right, and a square blue backing. Make only modest changes: replace the blue backing with a rich teal #007f73 backing, very subtly soften the outer corners, refine the paper and pencil edges for a clean sharp desktop app icon at small sizes. Do not add letters, logos, badges or other objects. Keep the composition and Windows flat icon style of the reference, not a photoreal or 3D redesign. Single centered square application icon, straight-on, high clarity, transparent background outside the teal square, no external shadow, no extra surrounding frame, no caption. Produce a high-resolution square PNG suitable for deriving multi-resolution Windows .ico sizes.

## 1.4 툴바 아이콘 재작성

툴바 아이콘 25개를 16×16 격자의 SVG로 다시 그렸다(`tools/toolbar-icons/svg`). 1px 회청색(#3B5160) 윤곽선과 기능별 파스텔 색 하나를 쓰고 배경 상자는 쓰지 않는다. 파일=크림/하늘, 검색=하늘, 보기=라벤더, 편집 도구=복숭아, 실행=민트, 북마크=청록, 녹화=하늘(대기)/빨강(녹화 중).

`python tools/toolbar-icons/make.py`가 SVG를 16·20·24·32·40·48px PNG(`data/toolbar/<이름>-<px>.png`)로 만들고 미리보기 `docs/toolbar-icons-1.4-preview.png`를 갱신한다(cairosvg, Pillow 필요). 프로그램은 화면 배율에 맞는 가장 가까운 크기를 골라 쓰므로 하나의 큰 이미지를 줄일 때 생기는 흐림이 없다. 실행 취소·다시 실행·녹화도 같은 세트로 옮겼고, 모두 저장은 디스켓 두 장으로 저장과 구분한다.

새 버튼: 파일에서 찾기, 북마크 표시/해제, 같은 문서 위아래 분할, 함수 목록. 분할과 함수 목록 버튼은 켜져 있을 때 눌린 상태로 표시된다.
