#!/usr/bin/env python3
"""
Patch final buat diagnostic tiler_heap: dump 64 byte pertama dev->tiler_heap
di 3 titik - sebelum submit sama sekali, sesudah vtc_jc (vertex+tiler) submit
selesai, dan sesudah frag_jc (fragment) submit selesai. Juga print bo->flags
sebagai jaring pengaman (WB_MMAP dikonfirmasi TIDAK diminta di alokasi
tiler_heap sekarang -> secara teori nggak butuh cache-sync manual, tapi kalau
ternyata flags-nya beda dari dugaan, itu langsung kelihatan di output tanpa
perlu run ulang).

Anchor regex whitespace-fleksibel (semua paste sebelumnya kepotong indentasi
gara-gara OCR/copy dari layar HP) -- tetap wajib match count == 1 tiap anchor,
kalau nggak, SKIP dan kasih tau, nggak nyentuh file sama sekali.
"""
import re

path = "/data/data/com.termux/files/home/panvk-g57/mesa/src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c"

with open(path, "r") as f:
    content = f.read()

DUMP_FN = """
static void
panvk_debug_dump_tiler_heap(struct panvk_device *dev, const char *label)
{
   if (!dev->tiler_heap || !dev->tiler_heap->addr.host) {
      fprintf(stderr, "[PANVK_DEBUG_HEAP] %s: tiler_heap belum ada/belum ke-mmap\\n", label);
      return;
   }
   const uint8_t *p = (const uint8_t *)dev->tiler_heap->addr.host;
   fprintf(stderr, "[PANVK_DEBUG_HEAP] %s bo->flags=0x%x first64:", label,
           dev->tiler_heap->bo->flags);
   for (int i = 0; i < 64; i++)
      fprintf(stderr, " %02x", p[i]);
   fprintf(stderr, "\\n");
}
"""

# --- Titik 0: taruh fungsi helper di atas fungsi submit (anchor: definisi
# panvk_kbase_submit_and_wait, yang udah kita konfirmasi ada persis di file
# ini) ---
anchor0 = re.compile(r"\n(static\s+\w+\s*\n?panvk_kbase_submit_and_wait\()")
m0 = list(anchor0.finditer(content))
print(f"Anchor 0 (taruh helper function) ketemu {len(m0)}x")
if len(m0) != 1:
    print("SKIP - anchor 0 gagal. Kirim: grep -n -B2 'panvk_kbase_submit_and_wait(int fd' " + path)
    raise SystemExit(1)
content = content[:m0[0].start()] + "\n" + DUMP_FN + content[m0[0].start():]

# --- Titik 1: sebelum blok "if (batch->vtc_jc.first_job) {" ---
anchor1 = re.compile(r"[ \t]*if \(batch->vtc_jc\.first_job\) \{\n")
m1 = list(anchor1.finditer(content))
print(f"Anchor 1 (sebelum vtc_jc submit) ketemu {len(m1)}x")
if len(m1) != 1:
    print("SKIP - anchor 1 gagal. Kirim: grep -n 'if (batch->vtc_jc.first_job)' " + path)
    raise SystemExit(1)
indent1 = re.match(r"[ \t]*", m1[0].group()).group()
insert1 = f'{indent1}panvk_debug_dump_tiler_heap(dev, "SEBELUM submit apapun");\n'
content = content[:m1[0].start()] + insert1 + content[m1[0].start():]

# --- Titik 2: sesudah blok assert(!ret) punya vtc_jc ---
anchor2 = re.compile(
    r"(panvk_kbase_submit_and_wait\(dev->drm_fd, batch->vtc_jc\.first_job, 0x16\);\n"
    r"[ \t]*fprintf\(stderr, \"\[PANVK_DEBUG_SUBMIT\] vtc_jc ret=%d\\n\", ret\);\n"
    r"[ \t]*assert\(!ret\);\n)"
)
m2 = list(anchor2.finditer(content))
print(f"Anchor 2 (sesudah vtc_jc submit) ketemu {len(m2)}x")
if len(m2) != 1:
    print("SKIP - anchor 2 gagal. Kirim: grep -n -A3 'vtc_jc ret=%d' " + path)
    raise SystemExit(1)
indent2 = "      "
insert2 = f'{indent2}panvk_debug_dump_tiler_heap(dev, "SESUDAH vtc_jc (vertex+tiler)");\n'
content = content[:m2[0].end()] + insert2 + content[m2[0].end():]

# --- Titik 3: sesudah blok assert(!ret) punya frag_jc ---
anchor3 = re.compile(
    r"(panvk_kbase_submit_and_wait\(dev->drm_fd, batch->frag_jc\.first_job, 0x01\);\n"
    r"[ \t]*fprintf\(stderr, \"\[PANVK_DEBUG_SUBMIT\] frag_jc ret=%d\\n\", ret\);\n"
    r"[ \t]*assert\(!ret\);\n)"
)
m3 = list(anchor3.finditer(content))
print(f"Anchor 3 (sesudah frag_jc submit) ketemu {len(m3)}x")
if len(m3) != 1:
    print("SKIP - anchor 3 gagal. Kirim: grep -n -A3 'frag_jc ret=%d' " + path)
    raise SystemExit(1)
indent3 = "      "
insert3 = f'{indent3}panvk_debug_dump_tiler_heap(dev, "SESUDAH frag_jc (fragment)");\n'
content = content[:m3[0].end()] + insert3 + content[m3[0].end():]

with open(path, "w") as f:
    f.write(content)

print("")
print("SEMUA 4 ANCHOR APPLIED. Lanjut: ninja, lalu jalanin ulang triangle_draw_test_v2")
print("dan grep 'PANVK_DEBUG_HEAP' -- bandingin 64 byte SEBELUM vs SESUDAH vtc_jc vs")
print("SESUDAH frag_jc. Kalau SEBELUM dan SESUDAH vtc_jc BEDA -> tiler beneran nulis,")
print("lanjut fokus ke fragment stage. Kalau SAMA semua -> tiler genuinely gak nulis")
print("walau event_code=DONE, itu jadi petunjuk besar berikutnya.")
