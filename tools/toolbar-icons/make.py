# Regenerates LHPad toolbar icons: SVG sources (16x16 grid) and pre-rasterised PNGs per DPI size.
# Usage: pip install cairosvg pillow; python tools/toolbar-icons/make.py
import cairosvg, os, io
ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
SVG_DIR = os.path.join(ROOT, "tools", "toolbar-icons", "svg")
OUT_DIR = os.path.join(ROOT, "data", "toolbar")
PREVIEW = os.path.join(ROOT, "docs", "toolbar-icons-1.4-preview.png")
from PIL import Image, ImageDraw, ImageFont
INK = "#3B5160"
C = dict(mint="#BFE7D7", sky="#BCDDF4", lav="#DACFF3", peach="#F9D5BC", amber="#FBE3A3",
         pink="#F7C3C3", teal="#A8DCD3", page="#FFFFFF", red="#FFB3B3", blue="#A9D3F5")
S = f'stroke="{INK}" stroke-width="1" stroke-linejoin="round" stroke-linecap="round"'
def page(x=3.5, y=1.5, w=9, h=13, fill=C["page"], fold=3):
    return (f'<path d="M{x} {y} H{x+w-fold} L{x+w} {y+fold} V{y+h} H{x} Z" fill="{fill}" {S}/>'
            f'<path d="M{x+w-fold} {y} V{y+fold} H{x+w}" fill="none" {S}/>')
def lines(x1, x2, ys):
    return "".join(f'<path d="M{x1} {y} H{x2}" {S}/>' for y in ys)
def magnifier(cx, cy, r, fill, hx, hy):
    return (f'<path d="M{cx + r*0.7:.2f} {cy + r*0.7:.2f} L{hx} {hy}" stroke="{INK}" stroke-width="2" stroke-linecap="round"/>'
            f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{fill}" {S}/>')
def folder(fill_back=C["amber"], fill_front="#FFF1CC"):
    return (f'<path d="M1.5 3.5 H5.5 L7 5 H13.5 V13.5 H1.5 Z" fill="{fill_back}" {S}/>'
            f'<path d="M1.5 13.5 L3.5 7.5 H15 L13.5 13.5 Z" fill="{fill_front}" {S}/>')
def floppy(x, y, s, fill):
    k = s / 12.0
    def p(a, b): return f"{x + a*k:.2f} {y + b*k:.2f}"
    return (f'<path d="M{p(0,0)} H{x+9.5*k:.2f} L{p(12,2.5)} V{y+12*k:.2f} H{x:.2f} Z" fill="{fill}" {S}/>'
            f'<path d="M{p(3,0)} V{y+3.5*k:.2f} H{x+8*k:.2f} V{y:.2f}" fill="{C["page"]}" {S}/>'
            f'<path d="M{p(2.5,12)} V{y+7.5*k:.2f} H{x+9.5*k:.2f} V{y+12*k:.2f}" fill="{C["page"]}" {S}/>')
def arrow_undo(mirror=False):
    g = (f'<path d="M3 6.5 H10 A3.5 3.5 0 0 1 10 13.5 H6.5" fill="none" stroke="{INK}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'
         f'<path d="M6 3.5 L3 6.5 L6 9.5" fill="none" stroke="{INK}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>')
    return f'<g transform="translate(16 0) scale(-1 1)">{g}</g>' if mirror else g
icons = {
 "NewFile": page(2.5, 1.5, 9, 12.5) + lines(4.5, 8, [6.5, 8.5]) +
            f'<circle cx="11.5" cy="11.5" r="3.5" fill="{C["mint"]}" {S}/>' + f'<path d="M11.5 9.8 V13.2 M9.8 11.5 H13.2" {S}/>',
 "Open": folder(),
 "Save": floppy(2, 2, 12, C["sky"]),
 "SaveAll": (f'<path d="M5.5 1.5 H12.5 L14.5 3.5 V10.5 H12.5" fill="none" {S}/><path d="M5.5 1.5 V3.5" fill="none" {S}/>'
             + floppy(1.5, 4, 10.5, C["sky"])),
 "Undo": arrow_undo(False),
 "Redo": arrow_undo(True),
 "Find": magnifier(6.5, 6.5, 4.5, C["sky"], 14, 14),
 "Replace": (f'<rect x="1.5" y="1.5" width="6" height="6" rx="1" fill="{C["peach"]}" {S}/>'
             f'<rect x="8.5" y="8.5" width="6" height="6" rx="1" fill="{C["mint"]}" {S}/>'
             f'<path d="M10 2.5 H11.5 A2 2 0 0 1 13.5 4.5 V6.5 M12 5 L13.5 6.5 L15 5" fill="none" {S}/>'
             f'<path d="M6 13.5 H4.5 A2 2 0 0 1 2.5 11.5 V9.5 M1 11 L2.5 9.5 L4 11" fill="none" {S}/>'),
 "FindInFiles": folder() + f'<path d="M12.6 12.6 L15 15" stroke="{INK}" stroke-width="2" stroke-linecap="round"/>' +
                f'<circle cx="10.5" cy="10.5" r="3" fill="{C["sky"]}" {S}/>',
 "Bookmark": f'<path d="M4 1.5 H12 V14.5 L8 11 L4 14.5 Z" fill="{C["teal"]}" {S}/>',
 "Source": (f'<rect x="1.5" y="2.5" width="13" height="11" rx="1.5" fill="{C["lav"]}" {S}/>'
            f'<path d="M6 5.5 L3.5 8 L6 10.5 M10 5.5 L12.5 8 L10 10.5 M8.8 5 L7.2 11" fill="none" {S}/>'),
 "Preview": (f'<path d="M1 8 C3 4 13 4 15 8 C13 12 3 12 1 8 Z" fill="{C["page"]}" {S}/>'
             f'<circle cx="8" cy="8" r="2.5" fill="{C["lav"]}" {S}/>'),
 "Split": (f'<rect x="1.5" y="2.5" width="13" height="11" rx="1.5" fill="{C["page"]}" {S}/>'
           f'<path d="M8.5 3 H13 A1 1 0 0 1 14 4 V12 A1 1 0 0 1 13 13 H8.5 Z" fill="{C["lav"]}"/>'
           f'<rect x="1.5" y="2.5" width="13" height="11" rx="1.5" fill="none" {S}/>'
           f'<path d="M8 2.5 V13.5 M5.5 5.5 L3.5 8 L5.5 10.5" fill="none" {S}/>' + lines(10, 12.5, [6, 8, 10])),
 "SplitEditor": (f'<rect x="1.5" y="1.5" width="13" height="13" rx="1.5" fill="{C["page"]}" {S}/>'
                 f'<path d="M2 2.5 A1 1 0 0 1 3 2 H13 A1 1 0 0 1 14 3 V8 H2 Z" fill="{C["lav"]}"/>'
                 f'<rect x="1.5" y="1.5" width="13" height="13" rx="1.5" fill="none" {S}/>'
                 f'<path d="M1.5 8 H14.5" {S}/>' + lines(4, 11, [4.5]) + lines(4, 9, [6]) + lines(4, 11, [10.5]) + lines(4, 9, [12.5])),
 "Outline": (f'<rect x="1.5" y="2" width="3" height="3" rx="0.5" fill="{C["lav"]}" {S}/>' + lines(6.5, 14, [3.5]) +
             f'<path d="M3 5 V11.5 H5" fill="none" {S}/>' + f'<path d="M3 8 H5" {S}/>' +
             f'<circle cx="6.5" cy="8" r="1.2" fill="{C["peach"]}" {S}/>' + lines(9, 14, [8]) +
             f'<circle cx="6.5" cy="11.5" r="1.2" fill="{C["peach"]}" {S}/>' + lines(9, 14, [11.5])),
 "Format": (f'<rect x="1" y="2" width="2.5" height="12" rx="1" fill="{C["peach"]}" {S}/>' +
            lines(5.5, 14.5, [3.5]) + lines(8, 14.5, [6.5]) + lines(8, 13, [9.5]) + lines(5.5, 11, [12.5])),
 "Compare": (f'<rect x="1.5" y="1.5" width="5.5" height="13" rx="1" fill="{C["page"]}" {S}/>'
             f'<rect x="9" y="1.5" width="5.5" height="13" rx="1" fill="{C["page"]}" {S}/>'
             f'<rect x="2" y="6.5" width="4.5" height="2" fill="{C["peach"]}"/>'
             f'<rect x="9.5" y="6.5" width="4.5" height="2" fill="{C["mint"]}"/>' +
             lines(3, 5.5, [4, 11.5]) + lines(10.5, 13, [4, 11.5])),
 "Record": f'<circle cx="8" cy="8" r="5.5" fill="{C["blue"]}" {S}/>',
 "RecordActive": f'<circle cx="8" cy="8" r="5.5" fill="{C["red"]}" {S}/><circle cx="8" cy="8" r="2" fill="{INK}"/>',
 "Stop": f'<rect x="3" y="3" width="10" height="10" rx="2" fill="{C["pink"]}" {S}/>',
 "Play": f'<path d="M4.5 2.5 L13.5 8 L4.5 13.5 Z" fill="{C["mint"]}" {S}/>',
 "Repeat": (f'<path d="M13.5 8 A5.5 5.5 0 1 1 11.9 4.1" fill="none" stroke="{INK}" stroke-width="1.5" stroke-linecap="round"/>'
            f'<path d="M12.6 1.6 L12.4 4.6 L9.4 4.4" fill="none" stroke="{INK}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'
            f'<path d="M6.5 5.5 L10.5 8 L6.5 10.5 Z" fill="{C["mint"]}" {S}/>'),
 "Monitor": page(2.5, 1.5, 11, 13, "#EAF7F1") + f'<path d="M4 9 H6 L7.3 6 L9 12 L10.3 8.5 H12" fill="none" stroke="{INK}" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/>',
 "FollowTail": (lines(3, 13, [2.5]) + lines(3, 10, [4.5]) +
                f'<path d="M8 6.5 V11 M5.5 8.8 L8 11.3 L10.5 8.8" fill="none" stroke="{INK}" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round"/>'
                f'<rect x="2" y="12.5" width="12" height="2.5" rx="1" fill="{C["mint"]}" {S}/>'),
 "Help": (f'<circle cx="8" cy="8" r="6.5" fill="{C["sky"]}" {S}/>'
          f'<path d="M6 6.2 A2 2 0 1 1 8.9 8 C8.3 8.3 8 8.7 8 9.4 V9.8" fill="none" stroke="{INK}" stroke-width="1.4" stroke-linecap="round"/>'
          f'<circle cx="8" cy="12" r="0.9" fill="{INK}"/>'),
}
sizes = [16, 20, 24, 32, 40, 48]
os.makedirs(OUT_DIR, exist_ok=True); os.makedirs(SVG_DIR, exist_ok=True)
for name, body in icons.items():
    svg = f'<svg xmlns="http://www.w3.org/2000/svg" width="16" height="16" viewBox="0 0 16 16">{body}</svg>'
    open(os.path.join(SVG_DIR, f"{name}.svg"), "w").write(svg + "\n")
    for s in sizes:
        cairosvg.svg2png(bytestring=svg.encode(), write_to=os.path.join(OUT_DIR, f"{name}-{s}.png"), output_width=s, output_height=s)
print(len(icons), "icons")
# preview sheet: each icon at 16, 24, 32 actual pixels plus 4x zoom of 16 on light toolbar background
order = ["NewFile","Open","Save","SaveAll","Undo","Redo","Find","Replace","FindInFiles","Bookmark","Source","Preview","Split","SplitEditor","Outline","Format","Compare","Record","RecordActive","Stop","Play","Repeat","Monitor","FollowTail","Help"]
labels = {"NewFile":"새 문서","Open":"열기","Save":"저장","SaveAll":"모두 저장","Undo":"실행 취소","Redo":"다시 실행","Find":"찾기","Replace":"바꾸기","FindInFiles":"파일에서 찾기","Bookmark":"북마크","Source":"원문","Preview":"미리보기","Split":"미리보기 분할","SplitEditor":"문서 분할","Outline":"함수 목록","Format":"정렬","Compare":"비교","Record":"녹화","RecordActive":"녹화 중","Stop":"중지","Play":"실행","Repeat":"반복","Monitor":"로그 모니터","FollowTail":"끝 따라가기","Help":"도움말"}
assert set(order) == set(icons)
fontpath = None
for p in ["/usr/share/fonts/truetype/nanum/NanumGothic.ttf","/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc","/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"]:
    if os.path.exists(p): fontpath = p
font = ImageFont.truetype(fontpath, 13) if fontpath else ImageFont.load_default()
col = 92; W = col * len(order) // 2 + 40
rows = 2
H = 40 + rows * 190
sheet = Image.new("RGBA", (col * 13 + 40, H), (247, 249, 249, 255))
d = ImageDraw.Draw(sheet)
for i, name in enumerate(order):
    r, c = divmod(i, 13)
    x0, y0 = 20 + c * col, 20 + r * 190
    big = Image.open(os.path.join(OUT_DIR, f"{name}-16.png")).resize((64, 64), Image.NEAREST)
    sheet.alpha_composite(big, (x0 + 10, y0))
    xx = x0 + 6
    for s in (16, 24, 32):
        im = Image.open(os.path.join(OUT_DIR, f"{name}-{s}.png")); sheet.alpha_composite(im, (xx, y0 + 74 + (32 - s))); xx += s + 6
    d.text((x0 + 4, y0 + 116), labels[name], fill=(40, 50, 60, 255), font=font)
sheet.save(PREVIEW)
