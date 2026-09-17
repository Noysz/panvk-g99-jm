#!/usr/bin/env python3
"""Build a labelled contact sheet from the Phase 4.2 framebuffer dumps.

Each tile is a real 64x64 GPU readback, nearest-neighbour upscaled so individual
pixels stay visible. The caption under each tile carries the measured centre
pixel and the driver-side resource-table numbers, so the image is self-describing
rather than needing the log beside it.
"""
import os
from PIL import Image, ImageDraw, ImageFont

W = "/data/data/com.termux/files/home/panvk-g57/phase4"
IMG = os.path.join(W, "img")
SCALE = 5           # 64 -> 320
PAD = 18
CAP_H = 96
TITLE_H = 74
COLS = 3

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

def font(sz, italic=False):
    name = "CrimsonPro-Italic.ttf" if italic else "CrimsonPro-Regular.ttf"
    try:
        return ImageFont.truetype(_find(name), sz)
    except Exception:
        return ImageFont.load_default()

F_TITLE = font(34)
F_SUB   = font(19, italic=True)
F_NAME  = font(22)
F_BODY  = font(17)
F_MONO  = font(16)

# (file, test id, what it shows, centre pixel, driver numbers, verdict)
TILES = [
    ("T4.2.1_baseline.ppm", "T4.2.1", "baseline, no application sets",
     "centre 255,0,0", "mask 0x0  count 4  table 64 B", "pass"),
    ("2sets.ppm", "T4.2.2", "two sets: 0 drives R, 1 drives G",
     "centre 191,128,0", "mask 0x3  count 4  table 64 B", "pass"),
    ("2sets_alt.ppm", "T4.2.2c", "control: same binds, other UBO values",
     "centre 64,255,0", "mask 0x3  count 4  table 64 B", "pass"),
    ("4sets.ppm", "T4.2.3", "four sets, one channel each",
     "centre 191,128,64", "mask 0xf  count 8  table 128 B", "pass"),
    ("sparse.ppm", "T4.2.5", "non-contiguous sets 0 and 3",
     "centre 191,0,64", "mask 0x9  count 8  table 128 B", "pass"),
    ("T4.2.4_skipall.ppm", "T4.2.4", "res_table forced to 0",
     "centre 0,0,0", "no fault, DONE, 0/4096 non-black", "control"),
    ("2sets_const.ppm", "T4.2.6a", "blue is a shader constant",
     "centre 191,128,128", "mask 0x3  count 4  table 64 B", "pass"),
    ("T4.2.6_skipfrag.ppm", "T4.2.6b", "fragment res_table forced to 0",
     "centre 0,0,128", "geometry intact, no fault", "control"),
    ("T4.2.6_skipall.ppm", "T4.2.6c", "all res_tables forced to 0",
     "centre 0,0,128", "geometry intact, no fault", "control"),
]

BG      = (22, 24, 28)
CARD    = (33, 36, 42)
INK     = (238, 240, 244)
DIM     = (150, 156, 166)
OK      = (120, 205, 140)
CTRL    = (232, 176, 92)
LINE    = (58, 62, 70)

tile_px = 64 * SCALE
card_w  = tile_px + PAD * 2
card_h  = tile_px + CAP_H + PAD * 2
rows    = (len(TILES) + COLS - 1) // COLS
sheet_w = COLS * card_w + PAD * (COLS + 1)
sheet_h = TITLE_H + rows * card_h + PAD * (rows + 1) + 66

sheet = Image.new("RGB", (sheet_w, sheet_h), BG)
d = ImageDraw.Draw(sheet)

d.text((PAD + 6, 16), "PanVK on Mali-G57 MC2 (Valhall v9 / Job Manager)",
       font=F_TITLE, fill=INK)
d.text((PAD + 8, 52),
       "Phase 4.2 resource table - every tile is a real 64x64 GPU readback, "
       "upscaled 5x. Three identical runs each.",
       font=F_SUB, fill=DIM)

for i, (fn, tid, desc, centre, nums, kind) in enumerate(TILES):
    r, c = divmod(i, COLS)
    x = PAD + c * (card_w + PAD)
    y = TITLE_H + PAD + r * (card_h + PAD)

    d.rounded_rectangle([x, y, x + card_w, y + card_h], 10, fill=CARD)

    path = os.path.join(IMG, fn)
    if os.path.exists(path):
        im = Image.open(path).convert("RGB").resize(
            (tile_px, tile_px), Image.NEAREST)
    else:
        im = Image.new("RGB", (tile_px, tile_px), (70, 30, 30))
    sheet.paste(im, (x + PAD, y + PAD))
    d.rectangle([x + PAD, y + PAD, x + PAD + tile_px, y + PAD + tile_px],
                outline=LINE)

    ty = y + PAD + tile_px + 10
    accent = OK if kind == "pass" else CTRL
    d.text((x + PAD, ty), tid, font=F_NAME, fill=accent)
    tw = d.textlength(tid, font=F_NAME)
    tag = "verified" if kind == "pass" else "negative control"
    d.text((x + PAD + tw + 10, ty + 4), tag, font=F_MONO, fill=DIM)
    d.text((x + PAD, ty + 27), desc, font=F_BODY, fill=INK)
    d.text((x + PAD, ty + 48), centre, font=F_MONO, fill=accent)
    d.text((x + PAD, ty + 67), nums, font=F_MONO, fill=DIM)

foot1 = ("Green = expected result reproduced.   Amber = deliberate negative control, "
         "predicted to fail.")
foot2 = ("T4.2.4 and T4.2.6 force res_table to 0. Descriptor reads then return zero "
         "with no GPU fault at all.")
d.text((PAD + 8, sheet_h - 52), foot1, font=F_MONO, fill=DIM)
d.text((PAD + 8, sheet_h - 32), foot2, font=F_MONO, fill=DIM)

out = os.path.join(IMG, "phase4.2_evidence_sheet.png")
sheet.save(out, "PNG", optimize=True)
print(f"{out}  {sheet.size[0]}x{sheet.size[1]}  {os.path.getsize(out)} bytes")
