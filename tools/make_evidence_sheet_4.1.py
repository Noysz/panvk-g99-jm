#!/usr/bin/env python3
"""Contact sheet for Phase 4.1 (indexed draws).

Tiles are real 64x64 GPU readbacks, nearest-neighbour upscaled. Captions carry
the measured pixel count, centre pixel, quadrant fingerprint and the sha256 of
the framebuffer, so byte-equality claims between tiles are checkable from the
image itself.
"""
import os, hashlib
from PIL import Image, ImageDraw, ImageFont

W = "/data/data/com.termux/files/home/panvk-g57/phase4"
IMG = os.path.join(W, "img")
SCALE, PAD, CAP_H, TITLE_H, COLS = 5, 18, 112, 74, 3

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
        return ImageFont.truetype(
            _find("italic" if it else "regular"), sz)
    except Exception:
        return ImageFont.load_default()

F_TITLE, F_SUB, F_NAME, F_BODY, F_MONO = font(34), font(19, True), font(22), font(17), font(15)

TILES = [
    ("T4.2.1_baseline.ppm", "oracle", "vkCmdDraw, no index buffer",
     "the already-validated baseline", "pass"),
    ("idx_a16.ppm", "T4.1.1", "vkCmdDrawIndexed, UINT16, idx {0,1,2}",
     "must equal the oracle", "pass"),
    ("idx_a32.ppm", "T4.1.2", "same but UINT32",
     "must equal T4.1.1", "pass"),
    ("idx_b16.ppm", "T4.1.3", "idx {3,4,5} selects the other triangle",
     "must DIFFER from T4.1.1", "pass"),
    ("idx_voffset.ppm", "T4.1.6", "idx {0,1,2} with vertexOffset = 3",
     "must equal T4.1.3", "pass"),
    ("idx_degen.ppm", "T4.1.4", "idx {0,0,0}, degenerate triangle",
     "expect nothing drawn", "control"),
]

BG, CARD, INK, DIM = (22,24,28), (33,36,42), (238,240,244), (150,156,166)
OK, CTRL, LINE = (120,205,140), (232,176,92), (58,62,70)

tile = 64*SCALE
cw, ch = tile + PAD*2, tile + CAP_H + PAD*2
rows = (len(TILES)+COLS-1)//COLS
sw = COLS*cw + PAD*(COLS+1)
sh = TITLE_H + rows*ch + PAD*(rows+1) + 74

sheet = Image.new("RGB", (sw, sh), BG)
d = ImageDraw.Draw(sheet)
d.text((PAD+6, 16), "PanVK on Mali-G57 MC2 (Valhall v9 / Job Manager)", font=F_TITLE, fill=INK)
d.text((PAD+8, 52),
       "Phase 4.1 indexed draws - real 64x64 GPU readbacks, upscaled 5x. "
       "Three identical runs per case.", font=F_SUB, fill=DIM)

digests = {}
for i, (fn, tid, desc, claim, kind) in enumerate(TILES):
    r, c = divmod(i, COLS)
    x = PAD + c*(cw+PAD)
    y = TITLE_H + PAD + r*(ch+PAD)
    d.rounded_rectangle([x, y, x+cw, y+ch], 10, fill=CARD)

    p = os.path.join(IMG, fn)
    if os.path.exists(p):
        raw = open(p, "rb").read()
        dg = hashlib.sha256(raw).hexdigest()[:12]
        digests[tid] = dg
        nb = 0
        px = Image.open(p).convert("RGB")
        for pix in px.getdata():
            if pix[0] or pix[1] or pix[2]:
                nb += 1
        im = px.resize((tile, tile), Image.NEAREST)
    else:
        dg, nb = "missing", 0
        im = Image.new("RGB", (tile, tile), (70,30,30))
    sheet.paste(im, (x+PAD, y+PAD))
    d.rectangle([x+PAD, y+PAD, x+PAD+tile, y+PAD+tile], outline=LINE)

    ty = y + PAD + tile + 10
    accent = OK if kind == "pass" else CTRL
    d.text((x+PAD, ty), tid, font=F_NAME, fill=accent)
    tw = d.textlength(tid, font=F_NAME)
    d.text((x+PAD+tw+10, ty+4), "verified" if kind=="pass" else "negative control",
           font=F_MONO, fill=DIM)
    d.text((x+PAD, ty+27), desc, font=F_BODY, fill=INK)
    d.text((x+PAD, ty+48), claim, font=F_BODY, fill=accent)
    d.text((x+PAD, ty+69), f"{nb}/4096 non-black", font=F_MONO, fill=DIM)
    d.text((x+PAD, ty+87), f"sha256 {dg}", font=F_MONO, fill=DIM)

f1 = ("Equal sha256 means the framebuffers are byte-identical, not merely similar. "
      "oracle = T4.1.1 = T4.1.2, and T4.1.3 = T4.1.6.")
f2 = ("T4.1.6 is the load-bearing case: indices {0,1,2} with vertexOffset 3 must land on the "
      "same image as indices {3,4,5}.")
d.text((PAD+8, sh-56), f1, font=F_MONO, fill=DIM)
d.text((PAD+8, sh-34), f2, font=F_MONO, fill=DIM)

out = os.path.join(IMG, "phase4.1_evidence_sheet.png")
sheet.save(out, "PNG", optimize=True)
print(f"{out}  {sheet.size[0]}x{sheet.size[1]}  {os.path.getsize(out)} bytes")
print("digests:", digests)
