#!/data/data/com.termux/files/usr/bin/bash
# test_coreq0.sh
#
# Patch panvk_vX_gpu_queue_kbase.c: setelah submission normal vtc_jc/frag_jc
# (yang udah terbukti pake descriptor benar dari dump sebelumnya), resubmit
# ULANG job chain vtc_jc yang SAMA PERSIS (alamat sama, byte sama) tapi pake
# core_req=0x00 -- nguji temuan dari GL driver (src/gallium/drivers/panfrost/
# pan_jm.c: "reqs" buat vtc_jc itu 0 polos, bukan CS|T|V=0x16 yang dipakai
# PanVK sekarang).
#
# Ini TIDAK menggantikan submission asli -- cuma nambah 1 submit ekstra
# setelahnya, murni buat lihat exception_status yang dibalikin dan (lebih
# penting) apakah pixel akhirnya berubah dari clear-only setelah resubmit
# core_req=0x00 ini.
#
# Usage: ./test_coreq0.sh

set -e
set -o pipefail 2>/dev/null || true

cd ~/panvk-g57/mesa

cat > "$HOME/patch_coreq0_test.py" << 'PYEOF'
path = 'src/panfrost/vulkan/jm/panvk_vX_gpu_queue_kbase.c'
with open(path) as f:
    content = f.read()

old = '''   batch->issued = true;'''
new = '''   /* TEST core_req=0x00 untuk vtc_jc -- lihat catatan di test_coreq0.sh.
    * Resubmit job chain YANG SAMA (alamat sama, descriptor sama, sudah
    * terverifikasi benar dari dump sebelumnya), bukan job baru. Kalau
    * exception_status balik selain 0x1 (DONE), core_req=0x00 itu genuinely
    * tidak valid untuk MALLOC_VERTEX -- informasi ini sendiri berguna. */
   if (batch->vtc_jc.first_job) {
      int test_ret = panvk_kbase_submit_and_wait(
         dev->kmod.dev->fd, batch->vtc_jc.first_job, 0x00);
      mesa_logd("[PANVK_TEST_COREQ0] resubmit vtc_jc core_req=0x00 ret=%d",
                test_ret);
      fprintf(stderr, "[PANVK_TEST_COREQ0] resubmit vtc_jc core_req=0x00 ret=%d\\n",
              test_ret);
   }

   batch->issued = true;'''

n = content.count(old)
print('anchor count:', n)
assert n == 1, f"Anchor 'batch->issued = true;' ketemu {n}x, harus persis 1 -- cek manual, kemungkinan ada lebih dari satu fungsi dengan baris ini"
content = content.replace(old, new, 1)

with open(path, 'w') as f:
    f.write(content)
print('WRITTEN')
PYEOF

python3 "$HOME/patch_coreq0_test.py"

echo "=== verifikasi patch masuk ==="
grep -n "PANVK_TEST_COREQ0" src/panfrost/vulkan/jm/panvk_vX_gpu_queue_kbase.c

echo ""
echo "=== kalau grep di atas nunjukin match, lanjut build: ==="
echo "cd ~/panvk-g57/mesa/build && ninja src/panfrost/vulkan/libvulkan_panfrost.so 2>&1 | tail -20 && cd .."
echo ""
echo "=== lalu run (pakai binary triangle_draw_test_v2 yang biasa dipakai): ==="
echo "cd ~/mesa"
echo "export PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1"
echo "export VK_ICD_FILENAMES=/data/data/com.termux/files/home/panvk-g57/mesa/build/src/panfrost/vulkan/panfrost_devenv_icd.aarch64.json"
echo "./triangle_draw_test_v2 2>&1 | grep -A5 'PANVK_TEST_COREQ0\\|total piksel\\|CLEAR-ONLY\\|non-hitam'"
