# Reproduction

These are the commands that actually produced the evidence in this repo, taken
from the shell history of the runs, not reconstructed.

## Prerequisites

* Mali-G57 MC2 / Valhall v9 device with ARM `kbase` and a readable `/dev/mali0`
  (`crw-rw-rw-` on the test device — an unrooted device will likely need
  different access).
* Termux (aarch64), building **natively on the device**. Not cross-compiled.
* `clang`, `meson`, `ninja`, `glslangValidator` (`pkg install vulkan-tools`).

## Layout used

| Path | Role |
|---|---|
| `~/panvk-g57/mesa` | driver work tree — the patches apply here |
| `~/panvk-g57/mesa/build` | meson build dir |
| `~/mesa` | test harness working dir (also a Mesa checkout) |
| `~/panvk-g57/raw-jm` | raw ioctl harnesses |

Upstream base commit for the patches: `6598829019c0746aa8e473b4ae1c980cbfa6ea4b`.

## 1. Apply the patches

Six patches reproduce the current driver. **Do not use a `patches/00*.patch`
glob** — it sweeps in historical patches that either do not apply or conflict.

```sh
cd ~/panvk-g57/mesa
git checkout 6598829019c0746aa8e473b4ae1c980cbfa6ea4b

P=/path/to/panvk-g99-jm/patches
git apply $P/0015-termux-android-build-fixes.patch
git apply $P/0014-panvk-v9-arch-enablement-common.patch
git apply $P/0013-panfrost-lib-kbase-and-fb-fixes.patch
git apply $P/0011-panvk-v9-jm-compute-dispatch.patch
git apply $P/0012-panvk-v9-jm-queue-submit-and-raw-capture.patch
git apply $P/0010-panvk-v9-jm-graphics-draw-path.patch
```

This sequence was replayed against pristine base-commit copies of all 23 target
files: all six applied without conflict, and the result then matched the live
driver work tree exactly — `identical: 23, differing: 0`.

Excluded on purpose:

* `0001`, `0002`, `0003` — HISTORICAL, they **do not apply** to this base commit
  (bare filename paths, or context from a different Mesa revision). Their content
  is carried by `0014` and `0013`.
* `0004`, `0005` — earlier drafts of the same files as `0010`/`0012`; applying
  both conflicts.
* `0020` — the FAU-count fix in isolation, already inside `0010`. Only needed for
  the A/B experiment, and the preserved source variants are easier — see step 5.

Full per-patch apply matrix: [`../patches/README.md`](../patches/README.md).

## 2. Build the driver

```sh
cd ~/panvk-g57/mesa/build
ninja src/panfrost/vulkan/libvulkan_panfrost.so
```

Target only the ICD — a full `ninja` builds far more than needed.

## 3. Run the graphics test

The harness does **not** go through the Vulkan loader. It `dlopen`s the ICD
directly and resolves `vk_icdGetInstanceProcAddr`, so no ICD JSON and no
`VK_ICD_FILENAMES` are involved. The path is overridable:

```sh
cd ~/mesa
clang -O2 -o triangle_draw_test_v2 triangle_draw_test_v2.c -ldl

# uses the committed default path — reproduces the recorded run exactly
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./triangle_draw_test_v2 2>&1 | tee run.log

# or point it at your own tree
PANVK_ICD_SO=/your/path/libvulkan_panfrost.so \
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./triangle_draw_test_v2 2>&1 | tee run.log
```

`PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` is required; PanVK refuses to initialise
without it.

Expected tail:

```
total non-black pixels: 512 of 4096
corner (0,0)   = 0,0,0,255
center (32,32) = 255,0,0,255
SUCCESS: partial triangle rasterized
```

It also writes `panvk_triangle.ppm` (P6, 64x64) in the working directory.

## 4. Capture descriptors

```sh
cd ~/mesa
export PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1
export PANVK_DEBUG=trace
export PANDECODE_DUMP_FILE="$HOME/mydump"
./triangle_draw_test_v2 2>&1 | tee "$HOME/myrun.log"
```

`PANVK_DEBUG=trace` produces the `[PANVK_DEBUG_*]` lines (MVJ / FBD / FRAGJOB /
VSFAU / FSFAU / TILERCTX / job status). `PANDECODE_DUMP_FILE` produces the
decoded `...ctx-0.0000` descriptor dump.

Useful filter:

```sh
grep -E 'VSFAU|FSFAU|vtc_jc ret|frag_jc ret|JOBSTATUS|total piksel|corner|center|SUCCESS' myrun.log
```

Note the pixel-summary lines are in Indonesian in the harness output
(`total piksel non-hitam` = total non-black pixels).

## 5. Reproduce the FAU A/B experiment

Two source variants are preserved verbatim in the driver tree next to
`panvk_vX_cmd_draw.c`:

* `panvk_vX_cmd_draw.c.ab_fau_off` — side A, no `fau_count`
* `panvk_vX_cmd_draw.c.ab_fau_on` — side B, `fau_count` from shader metadata

```sh
cd ~/panvk-g57/mesa/src/panfrost/vulkan/jm
cp panvk_vX_cmd_draw.c.ab_fau_off panvk_vX_cmd_draw.c     # side A
cd ~/panvk-g57/mesa/build && ninja src/panfrost/vulkan/libvulkan_panfrost.so
cd ~/mesa && PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./triangle_draw_test_v2 | tee A.log
# -> 0 / 4096 non-black, CLEAR-ONLY

cd ~/panvk-g57/mesa/src/panfrost/vulkan/jm
cp panvk_vX_cmd_draw.c.ab_fau_on panvk_vX_cmd_draw.c      # side B
cd ~/panvk-g57/mesa/build && ninja src/panfrost/vulkan/libvulkan_panfrost.so
cd ~/mesa && PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./triangle_draw_test_v2 | tee B.log
# -> renders
```

The A/B variants use the fullscreen triangle, so side B gives 4096/4096. To get
the 512/4096 partial result use the current `triangle_draw_test_v2.c` from
[`tests/graphics/`](../tests/graphics/), not the
`HISTORICAL-fullscreen-baseline` variant.

Restore the full current driver afterwards — `git diff` in the work tree, or
re-apply `0010`.

## 6. Run the compute test

```sh
cd ~/mesa
clang -O2 -o compute_test panvk_compute_test_g57.c -ldl
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./compute_test
# expect: buffer after dispatch, first u32=0x00000309   (777)
```

## 7. Run the raw-JM tests

No Mesa involved:

```sh
cd ~/panvk-g57/raw-jm
clang -O2 -o kbase_write_value_core_req_test kbase_write_value_core_req_test.c
./kbase_write_value_core_req_test
```

Expected:

```
WRITE_VALUE core_req=0x2 submitted, atom_size=56 stride=64
event=0x4, target=0x2a2a2a2a
```

`target=0x2a2a2a2a` is the point — it means the atom's write reached memory.

## Shaders

```sh
glslangValidator -V tests/shaders/triangle.vert -o triangle.vert.spv
glslangValidator -V tests/shaders/triangle.frag -o triangle.frag.spv
glslangValidator -V tests/shaders/write_value.comp -o write_value.spv
```

Pre-built `.spv` files are committed so a mismatched glslang version cannot
silently change the workload.

## Caveats

* The `tools/*.py` scripts mutate driver source in place and are **not** a
  supported build path. Use `patches/`. See
  [historical-superseded.md](historical-superseded.md).
* `tools/build.sh` references `$HOME/funnymdzz-mesa/include` and targets the
  indirect-dispatch test, which is **NOT IMPLEMENTED** on v9. Kept for
  chronology.
