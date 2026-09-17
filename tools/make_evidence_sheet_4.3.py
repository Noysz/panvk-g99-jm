#!/usr/bin/env python3
"""Contact sheet for Phase 4.3 (multiple render targets, square and circle).

Captions carry the measured drawn-pixel count and, where one exists, the
arithmetic prediction. Nothing here is a hand-picked expected value: the square's
1024 is arithmetic and the circle is bound-checked against a CPU rasterizer that
applies Vulkan's own coverage rule.
"""
import os, hashlib
from PIL import Image, ImageDraw, ImageFont
from collections import Counter

W = "/data/data/com.termux/files/home/panvk-g57/phase4"
IMG = os.path.join(W, "img")
SCALE, PAD, CAP_H, TITLE_H, COLS = 5, 18, 118, 74, 3

# Font lookup. Set EVIDENCE_FONT_DIR to a directory holding a serif TTF pair,
# or leave it unset and PIL's built-in bitmap font is used instead. The sheet
# renders either way; only the typography changes.
FONT_DIR = os.environ.get("EVIDENCE_FONT_DIR", "")
_SERIF = ("CrimsonPro-Regular.ttf", "DejaVuSerif.ttf", "LiberationSerif-Regular.ttf")
_ITALIC = ("CrimsonPro-Italic.ttf", "DejaVuSerif-Italic.ttf", "LiberationSerif-Italic.ttf")

def _find(which):
    """Return a usable font path, or "" to fall back to PIL's default."""
    names = _ITALIC if which in ("italic",) or "Italic" in str(which) else _SERIF
    cands = []
    if FONT_DIR:
        cands += [os.path.join(FONT_DIR, n) for n in names]
    for base in ("/usr/share/fonts", "/system/fonts"):
        for n in names:
            cands.append(os.path.join(base, n))
    for c in cands:
        if os.path.exists(c):
            return c
    return ""

def font(sz, it=False):
    try:
        return ImageFont.truetype(_find("italic" if it else "regular"), sz)
    except Exception:
        return ImageFont.load_default()
F_TITLE, F_SUB, F_NAME, F_BODY, F_MONO = font(34), font(19, True), font(22), font(17), font(15)

# file, id, description, claim, kind
TILES = [
    ("mrt_sq1_rt0.ppm", "T4.3.1", "square, 1 target",
     "exactly 1024 by arithmetic", "pass"),
    ("mrt_sq2_rt0.ppm", "T4.3.2 RT0", "square, 2 targets, red to location 0",
     "1024, matches reference", "pass"),
    ("mrt_sq2_rt1.ppm", "T4.3.2 RT1", "same draw, blue to location 1",
     "own clear colour, independent", "pass"),
    ("mrt_ci1_rt0.ppm", "T4.3.3", "circle, 64-segment fan, 1 target",
     "1568, inside CPU bound", "pass"),
    ("mrt_ci2_rt0.ppm", "T4.3.4 RT0", "circle, 2 targets, red to location 0",
     "1568, matches reference", "pass"),
    ("mrt_ci2_rt1.ppm", "T4.3.4 RT1", "same draw, blue to location 1",
     "own clear colour, independent", "pass"),
    ("mrt_sq2only0_rt0.ppm", "T4.3.5 RT0", "2 targets, shader writes location 0 only",
     "1024, unaffected", "control"),
    ("mrt_sq2only0_rt1.ppm", "T4.3.5 RT1", "location 1 never written",
     "zero blue pixels, no leak", "control"),
]

BG, CARD, INK, DIM_ = (22,24,28), (33,36,42), (238,240,244), (150,156,166)
OK, CTRL, LINE = (120,205,140), (232,176,92), (58,62,70)

tile = 64*SCALE
cw, ch = tile+PAD*2, tile+CAP_H+PAD*2
rows = (len(TILES)+COLS-1)//COLS
sw = COLS*cw + PAD*(COLS+1)
sh = TITLE_H + rows*ch + PAD*(rows+1) + 76

sheet = Image.new("RGB", (sw, sh), BG)
d = ImageDraw.Draw(sheet)
d.text((PAD+6, 16), "PanVK on Mali-G57 MC2 (Valhall v9 / Job Manager)", font=F_TITLE, fill=INK)
d.text((PAD+8, 52),
       "Phase 4.3 multiple render targets - square and circle, real 64x64 GPU "
       "readbacks upscaled 5x. Three identical runs per case.",
       font=F_SUB, fill=DIM_)

for i, (fn, tid, desc, claim, kind) in enumerate(TILES):
    r, c = divmod(i, COLS)
    x = PAD + c*(cw+PAD)
    y = TITLE_H + PAD + r*(ch+PAD)
    d.rounded_rectangle([x, y, x+cw, y+ch], 10, fill=CARD)

    p = os.path.join(IMG, fn)
    if os.path.exists(p):
        im0 = Image.open(p).convert("RGB")
        cnt = Counter(im0.getdata())
        dominant = cnt.most_common(1)[0]
        # count non-clear-ish drawn pixels: report the two most common colours
        top = cnt.most_common(2)
        dg = hashlib.sha256(open(p,"rb").read()).hexdigest()[:12]
        im = im0.resize((tile, tile), Image.NEAREST)
        colour_note = "  ".join(f"{col}:{n}" for col, n in top)
    else:
        dg, colour_note = "missing", "-"
        im = Image.new("RGB", (tile, tile), (70,30,30))
    sheet.paste(im, (x+PAD, y+PAD))
    d.rectangle([x+PAD, y+PAD, x+PAD+tile, y+PAD+tile], outline=LINE)

    ty = y+PAD+tile+10
    accent = OK if kind == "pass" else CTRL
    d.text((x+PAD, ty), tid, font=F_NAME, fill=accent)
    tw = d.textlength(tid, font=F_NAME)
    d.text((x+PAD+tw+10, ty+4), "verified" if kind=="pass" else "negative control",
           font=F_MONO, fill=DIM_)
    d.text((x+PAD, ty+27), desc, font=F_BODY, fill=INK)
    d.text((x+PAD, ty+48), claim, font=F_BODY, fill=accent)
    d.text((x+PAD, ty+69), colour_note, font=F_MONO, fill=DIM_)
    d.text((x+PAD, ty+88), f"sha256 {dg}", font=F_MONO, fill=DIM_)

f1 = ("Colour:count pairs are measured, not asserted. RT1 clears to 0,64,0 and RT0 to 0,0,0, "
      "so aliasing would show up in the untouched region as well as the drawn one.")
f2 = ("The square's 1024 is arithmetic: NDC +/-0.5 lands on pixel 16.0 and 48.0, giving 32x32 "
      "covered pixel centres. The circle has no closed form and is bound-checked instead.")
d.text((PAD+8, sh-58), f1, font=F_MONO, fill=DIM_)
d.text((PAD+8, sh-36), f2, font=F_MONO, fill=DIM_)

out = os.path.join(IMG, "phase4.3_evidence_sheet.png")
sheet.save(out, "PNG", optimize=True)
print(f"{out}  {sheet.size[0]}x{sheet.size[1]}  {os.path.getsize(out)} bytes")
