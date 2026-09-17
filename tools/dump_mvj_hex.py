#!/usr/bin/env python3
"""
Dump 384 byte mentah dari job descriptor Malloc Vertex Job PERSIS setelah
semua section (PRIMITIVE, INSTANCE_COUNT, ALLOCATION, TILER, SCISSOR,
PRIMITIVE_SIZE, INDICES, DRAW, POSITION, VARYING) selesai di-pack -- biar
kita bisa cocokin manual ke offset byte yang udah kita konfirmasi dari
genxml (Header@0, Primitive@32, Draw@128, Position@256, Varying@320),
bukan nebak dari tool decode eksternal yang belum tentu wired buat v9.
"""

path = "/data/data/com.termux/files/home/panvk-g57/mesa/src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c"

with open(path, "r") as f:
    content = f.read()

anchor = (
    "   draw->jobs.idvs = ptr;\n"
    "   return VK_SUCCESS;\n"
    "}\n"
)

count = content.count(anchor)
print(f"Anchor ketemu {count}x")

if count != 1:
    print("SKIP - anchor tidak ketemu tepat 1x. Kirim balik:")
    print("  grep -n -B2 'draw->jobs.idvs = ptr;' " + path)
    raise SystemExit(1)

debug_print = (
    "\n"
    "   /* PATCH DEBUG v9 (sementara) -- dump 384 byte mentah job descriptor\n"
    "    * Malloc Vertex Job, biar dicocokin manual ke layout genxml.\n"
    "    * HAPUS setelah bug ketemu. */\n"
    "   fprintf(stderr, \"[PANVK_DEBUG_MVJ] dump 384 byte job descriptor:\\n\");\n"
    "   {\n"
    "      const uint8_t *dbg_bytes = (const uint8_t *)ptr.cpu;\n"
    "      for (int dbg_row = 0; dbg_row < 384; dbg_row += 16) {\n"
    "         fprintf(stderr, \"[PANVK_DEBUG_MVJ] +%03d:\", dbg_row);\n"
    "         for (int dbg_col = 0; dbg_col < 16; dbg_col++)\n"
    "            fprintf(stderr, \" %02x\", dbg_bytes[dbg_row + dbg_col]);\n"
    "         fprintf(stderr, \"\\n\");\n"
    "      }\n"
    "   }\n"
    "\n"
    + anchor
)

new_content = content.replace(anchor, debug_print, 1)

with open(path, "w") as f:
    f.write(new_content)

print("APPLIED.")
print("")
print("Cara baca hasilnya nanti (offset byte, dari genxml yang udah kita")
print("konfirmasi -- semua PALING KIRI di tiap baris +NNN adalah offset byte):")
print("  +000..+01F (0-31)   = Job Header (32 byte)")
print("  +020..+02F (32-47)  = Primitive")
print("  +030..+033 (48-51)  = Instance Count")
print("  +034..+037 (52-55)  = Allocation")
print("  +038..+03F (56-63)  = Tiler (harus ADDRESS non-nol, cek 8 byte little-endian)")
print("  +068..+06F (104-111)= Scissor")
print("  +070..+077 (112-119)= Primitive Size")
print("  +078..+07F (120-127)= Indices")
print("  +080..+0FF (128-255)= Draw (128 byte)")
print("  +100..+13F (256-319)= Position (Shader Environment, 64 byte)")
print("  +140..+17F (320-383)= Varying (Shader Environment, 64 byte)")
print("")
print("Yang PALING PENTING dicek pertama: byte di +038 (Tiler address) harus")
print("non-nol (8 byte little-endian) -- kalau nol, Tiler Context gak ke-set,")
print("itu bisa jelasin kenapa render silent-fail. Kedua: +108..+10F (Shader")
print("addr di dalam Position) juga harus non-nol.")
