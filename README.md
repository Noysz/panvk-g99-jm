# panvk-g99-jm

Investigation & testing notes: getting **PanVK** (Mesa's open-source Vulkan driver) working on **Mali-G57 MC2** (MediaTek Helio G99 / MT6789), a **Valhall Gen 1 / JM-frontend** GPU — Mesa arch bucket **v9**.

**Device:** Infinix Note 40 4G (X6853) — Mali-G57 MC2, GPU ID `9093`, live kernel driver `r54p1` (UAPI 11.46), kernel `6.12.38-android16`.

Most public PanVK testing/builds so far target **CSF** chips (G610/G615/G710/G720 — arch v10+). This repo tracks the v9/JM side specifically, since it's a different frontend (Job Manager, not Command Stream Frontend) and gets far less testing.

> ⚠️ Experimental reverse-engineering / bring-up project. No claim of Vulkan conformance or game compatibility is made anywhere in this repo unless explicitly marked as such with hardware evidence.

---

## Status (2026-10-10): FourFectVK 0.1.0 test builds, new upstream base

**Builds for Winlator: [`docs/test-builds.md`](docs/test-builds.md)**
(test1-test7, what each one changed, results, downloads in the
[`fourfectvk-g57-v0.1.0-tests`](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-tests)
pre-release). Latest: **test7**.

* **New base since test6.** FourFectVK now builds on upstream Mesa
  `feeadb5f` (26.3.0-devel) through **hafiz's rebase** of this project, with
  FourFectVK `0060`-`0077` ported on top:
  [`patches-upstream/`](patches-upstream/). The old series in
  [`patches/`](patches/) (`0001`-`0077`) is kept for history and builds
  test1-test5.
* **GPU faults gone in the games that had them.** Tomb Raider and Little
  Nightmares II ended with kbase `0x4`/`0x42` job failures on test4-test5
  (flat or missing textures, pink models, missing menu text). On test6 the
  logs of 5 Tomb Raider runs and 1 LN2 run show 0 failed GPU jobs and the
  pictures are right. Which change of the new base removed the fault is not
  isolated yet.
* **D3D11:** tessellation, geometry shaders and transform feedback stay on
  for v9 (DXVK 1.10.3 / 1.11 / 2.x start); robustBufferAccess2 for DXVK 2.4+
  and 3.x.
* **Checked for every test build:** VK-GL-CTS regression of 7127 cases with
  0 lost, and a Winlator-stack device check
  ([`evidence/cts/phase14/`](evidence/cts/phase14/) for test6-test7).
* **Open:** frame rate in heavy games (Tomb Raider about 10 fps at medium
  settings, GPU-bound); test7 adds `PANVK_FRAME_PROF` to measure where the
  time goes. Other Valhall GPUs (G68, G57 MC3) are untested.

**Thanks to hafiz** for the upstream rebase, the chained and asynchronous
kbase submission with real kernel fences, forward pixel kill on v9 and the
multisampled storage work: test6 is the build where the graphics problems of
test1-test5 went away.

---

## Status (2026-10-04): graphics, texturing, WSI and a spinning cube on v9, EXPERIMENTAL v9-only features, and real DXVK through the Winlator stack.

**Full labelled status: [`docs/STATUS.md`](docs/STATUS.md)** (up to 2026-09-17). Newer work:
[`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md), [`docs/phase5-7.md`](docs/phase5-7.md),
[`docs/v9-experimental-features.md`](docs/v9-experimental-features.md),
[`docs/phase7-winlator-dxvk.md`](docs/phase7-winlator-dxvk.md).

| Area | State |
|---|---|
| Kernel / kbase surface | ✅ **VERIFIED** |
| Native v9/JM compute dispatch + readback | ✅ **VERIFIED** |
| Offscreen graphics: vertex buffers, varyings, indexed/indirect draws, MRT, depth, stencil, blending, 4x MSAA | ✅ **VERIFIED-HW** (own harnesses vs CPU models, 3x + negative controls) |
| Texture sampling (2D RGBA8, nearest/linear, 4 address modes, AFBC and linear) | ✅ **VERIFIED-HW** |
| Fences / semaphores on kbase | ✅ **VERIFIED-HW** after patch `0041` (were never signalled before) |
| WSI: X11 swapchain on Termux:X11, spinning cube, 30,000 frames with resize/recreate | ✅ **VERIFIED-HW** (on-screen pixels checked) |
| VK-GL-CTS on device | 🚧 small subsets only, see table below |
| `multiDrawIndirect`, `drawIndirectCount` on v9 | 🧪 **EXPERIMENTAL** (patch `0044`, CTS subset passes) |
| `VK_EXT_robustness2` `nullDescriptor` and `robustBufferAccess2` on v9 | 🧪 **EXPERIMENTAL** (patches `0045`, `0071`). `robustBufferAccess2` on by default since `0071` (texel buffer out-of-range values fixed in the shader); out-of-range vertex fetch still open |
| Sampler min/max reduction on v9 (emulated in the shader) | 🧪 **EXPERIMENTAL** (patch `0046`). 1D/2D/3D pass in CTS, **cube maps broken** |
| Sampler min/max on cube maps, cubic filtering (opt-in `PANVK_V9_EMULATE_SAMPLER=1`) | 🧪 **EXPERIMENTAL** (patches `0047`, `0049`, `0050`), CTS subsets pass 3x |
| Winlator: adrenotools import, launcher query, Wrapper device, AHB swapchain | ✅ **VERIFIED-HW** with the APK's own libraries (patches `0052`-`0056`), not inside the app |
| DXVK (real DLLs from Winlator, Wine 11.16) | ✅ **VERIFIED-HW** for a D3D11 test program: 1.12.1-sarek FL 11_1, v2.3.1 FL 10_1. 1.10.3 / 1.11 need `geometryShader` and do not start |
| Geometry shaders, tessellation, transform feedback on v9 | ❌ missing. 🧪 such pipelines are refused instead of crashing (patch `0057`) |
| Game / application support, conformance | ❌ **NOT CLAIMED** (no game tested) |

> 🧪 **EXPERIMENTAL** means the feature is exposed on v9 although upstream panvk
> only exposes it on v10+, and it works by emulation in the driver. It is
> checked against a CTS subset with a negative control, nothing more. It is
> not conformant, has known gaps, and may be slow. Details and what is broken:
> [`docs/v9-experimental-features.md`](docs/v9-experimental-features.md).

CTS on device (VK-GL-CTS `31807ad`, local build; results in [`evidence/cts/`](evidence/cts/)):

| group | Pass | Fail | NotSupported |
|---|---|---|---|
| `api.smoke` | 8 | 0 | 0 |
| `draw.renderpass.simple_draw` | 4 | 0 | 0 |
| `draw.renderpass.indirect_draw` (with 🧪 `0044`) | 372 | 0 | 0 |
| `texture.filtering.2d.formats.r8g8b8a8_unorm` | 6 | 0 | 12 |
| `texture.mipmap.2d.basic` | 36 | 0 | 36 |
| `pipeline.monolithic.sampler` 2D RGBA8 (with 🧪 `0046`) | 73 | 0 | 97 |
| `pipeline.monolithic.sampler` min/max subset, 1D/2D/3D (🧪 `0046`) | 132 | 0 | 0 |
| same, cube / cube array (🧪 `0046`) | 4 | **12** | 0 |
| `pipeline.monolithic.blend.format.r8g8b8a8_unorm` | 100 | 0 | 0 |
| `pipeline.monolithic.stencil` D24S8, every 20th case | 209 | 0 | 0 |
| `robustness2` null-descriptor subset (🧪 `0045`) | 16 | 0 | 10 |

Three driver bugs were found by CTS that the project's own tests had missed
(patches `0041`-`0043`, [`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md#2-fixes-found-by-cts)).

## Status (2026-09-17, historical): compute **and** offscreen graphics execute on v9. No WSI/present.

| Area | State |
|---|---|
| Kernel / kbase surface | ✅ **VERIFIED** |
| Native v9/JM compute dispatch + readback | ✅ **VERIFIED** |
| Native v9/JM MALLOC_VERTEX + FRAGMENT execution | ✅ **VERIFIED** |
| Offscreen partial-triangle rasterization + readback | ✅ **VERIFIED** |
| FAU count root cause for that workload | ✅ **VERIFIED** |
| Raw-JM `WRITE_VALUE` atom submission | ✅ **VERIFIED** |
| Descriptor / resource-table audit | 🚧 **IN PROGRESS** (superseded: done in Phase 4) |
| WSI / present / swapchain | ❌ at the time (superseded: see 2026-10-04 above) |

### The headline result

A draw call submitted through PanVK rasterizes a **partial** triangle into a
64x64 offscreen `VkImage`, read back correctly on the CPU:

```
total non-black pixels: 512 / 4096      <- 12.5% coverage, a real shape
corner (0,0)   = 0,0,0,255              <- still clear
center (32,32) = 255,0,0,255            <- triangle
```

Evidence: [`evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log`](evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log),
framebuffer [`evidence/framebuffer/panvk_triangle.png`](evidence/framebuffer/panvk_triangle.png).

> **This proves offscreen rasterization and readback. It does NOT prove WSI,
> present, or general application support.** The partial coverage matters: a
> fullscreen result cannot be distinguished from a mis-scaled clear or a blit.
> See [`docs/known-limitations.md`](docs/known-limitations.md).

### Root cause of the previous "clear-only" result

The v9 **Shader Environment FAU count** was left at zero. The job chain reported
`DONE` on both MALLOC_VERTEX and FRAGMENT, but the shaders received no
fast-access uniforms. A/B with a single-line delta:

| `fau_count` | non-black pixels |
|---|---|
| absent | **0 / 4096** |
| from shader metadata | **4096 / 4096** |

Counts come from `fau.total_count` — this workload reports VS `4` / FS `3`, so
**never hardcode 4 and 3**. Full analysis incl. descriptor-level stage
attribution: [`docs/fau-root-cause.md`](docs/fau-root-cause.md).

### Documentation index

| Doc | Contents |
|---|---|
| [`docs/STATUS.md`](docs/STATUS.md) | labelled status of everything |
| [`docs/INVENTORY.md`](docs/INVENTORY.md) | what was examined, published, and deliberately excluded |
| [`docs/hardware-runtime.md`](docs/hardware-runtime.md) | GPU ID, gpuprops, kbase, EXEC_VA |
| [`docs/compute-progress.md`](docs/compute-progress.md) | compute path + enablement fixes |
| [`docs/graphics-progress.md`](docs/graphics-progress.md) | draw path, job chain, captured descriptors |
| [`docs/raw-jm-progress.md`](docs/raw-jm-progress.md) | raw ioctl, atom size/stride, event codes |
| [`docs/fau-root-cause.md`](docs/fau-root-cause.md) | the causal FAU-count defect |
| [`docs/resource-table-findings.md`](docs/resource-table-findings.md) | `res_count`, `RESOURCE` layout |
| [`docs/test-matrix.md`](docs/test-matrix.md) | every test and its result |
| [`docs/known-limitations.md`](docs/known-limitations.md) | **read before quoting anything** |
| [`docs/reproduction.md`](docs/reproduction.md) | exact build and run commands |
| [`docs/historical-superseded.md`](docs/historical-superseded.md) | wrong turns, kept with corrections |
| [`docs/phase4-open-questions.md`](docs/phase4-open-questions.md) | Phase 4 self-audit and open questions |
| [`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md) | CTS on device, fixes 0041-0043, spinning cube over X11, texture sampling |
| [`docs/phase5-7.md`](docs/phase5-7.md) | stencil, blending, CTS subsets, sustained WSI, DXVK gap |
| [`docs/roadmap-phase8-13.md`](docs/roadmap-phase8-13.md) | next phases: open Phase 4-6 items, DXVK 1.10.3-2.x features, memory, CPU/GPU |
| [`docs/phase7-winlator-dxvk.md`](docs/phase7-winlator-dxvk.md) | Winlator import/launcher/Wrapper fixes, real DXVK, AIO-Graphics-Test crash fix |
| [`docs/v9-experimental-features.md`](docs/v9-experimental-features.md) | 🧪 **EXPERIMENTAL** v9-only features (0044-0046) and what is broken |

Earlier sections §1–§7 below are the original chronological investigation log and
are kept as written. Where a conclusion in them has since been overturned, the
correction is in
[`docs/historical-superseded.md`](docs/historical-superseded.md).

---

## Status as of 2026-09-09 (historical): native compute dispatch working, graphics not yet ported

Where things stood before vs. then:

1. **Kernel side — not a blocker, was already confirmed working.** `/dev/mali0` responds correctly to the full ioctl chain, and the GPU property table is readable with no context at all (§1, §1b).
2. **Userspace side — was the blocker, now largely resolved for compute.** PanVK had no v9 backend at all as of the first finding in this repo (§4). Since then, a v9 `jm/` command-buffer path has been built out far enough that **a real compute shader now dispatches through PanVK and produces a correct, reproducible GPU result** — see §7. This clears Phase 3's falsifiable target from the Roadmap below. **Graphics (draw calls) is still unimplemented** — see §7 for exactly what's stubbed.

## 1. Raw kbase ioctl test — ✅ fully working

Wrote a minimal C program that talks to `/dev/mali0` directly via `ioctl()`, bypassing Mesa/PanVK entirely, to confirm the kernel driver itself is healthy:

```c
// version check
KBASE_IOCTL_VERSION_CHECK  → UAPI major=11 minor=46   ✅
// context + memory
KBASE_IOCTL_SET_FLAGS      → OK, context active        ✅
KBASE_IOCTL_MEM_ALLOC      → OK, gpu_va=0x41000         ✅
mmap()                     → OK                          ✅
write 0xAB × 16384 bytes, read back → matches            ✅ (CPU↔GPU coherency confirmed)
```

**Conclusion: the kernel-side JM driver is fully responsive and correct.** Version check, context activation, memory allocation, mmap, and read/write coherency all work exactly as expected. Whatever's blocking PanVK is not a kernel/device compatibility problem.

### 1b. `GET_GPUPROPS` works with **no context** — ✅ the enumeration path is viable

Follow-up probe ([`tests/test_kbase3.c`](tests/raw-jm/test_kbase3.c), full output in [`results/gpuprops-g57-r54p1.txt`](results/gpuprops-g57-r54p1.txt), writeup in [`docs/gpuprops-without-context.md`](docs/gpuprops-without-context.md)):

```
KBASE_IOCTL_GET_GPUPROPS before VERSION_CHECK  → 749 bytes, 83 props   ✅
KBASE_IOCTL_GET_GPUPROPS before SET_FLAGS      → 749 bytes, 83 props   ✅
KBASE_IOCTL_GET_GPUPROPS with context          → 749 bytes, 83 props   ✅ (identical)
flags != 0 → EINVAL, size < required → EINVAL                          ✅ (both as predicted)
```

This matters because it is exactly the state `vkEnumeratePhysicalDevices` runs in. The handler sits in kbase's *pre-setup* ioctl group (`mali_kbase_core_linux.c:1855-1894`, above the `setup_state == KBASE_FILE_COMPLETE` gate at :1896), so **a `kbase_kmod.c` backend can fill `pan_kmod_dev_props` with no context, no allocation, and no job submission.** The writeup includes the full `KBASE_GPUPROP_* → pan_kmod_dev_props` mapping and the field offsets verified against this device's own disassembly.

Two gotchas worth repeating here: `SHADER_PRESENT` is `0x5` on this MC2 part (bits 0 and 2, **not** contiguous — use popcount, not `mask + 1`), and `GPU_FREQ_KHZ_MAX` is a hardcoded default in the kernel, not a real value.

Also mapped the whole ioctl surface while in there — [`docs/kbase-uapi-r54p1.md`](docs/kbase-uapi-r54p1.md): 33 ioctls, all 33 matching ARM's public GPL header by name *and* direction, zero MediaTek-custom ones. So `kbase_kmod.c` can be written against ARM's public headers with no struct reverse-engineering.

## 2. PanVK-G720 (wonderkast02) test result — ❌ 0 extensions

Tested `wonderkast02`'s `PanVK-G720-0.1.0-alpha.2` build (Mesa 26.3.0-devel) as the Vulkan ICD in WinlatorMali:

- `Available Extensions: 0`
- `GPU Name: Device` (generic fallback, not "Mali-G57 MC2")

Binary analysis of `libvulkan_panfrost.so` shows compiled arch buckets: `v6, v7, v10, v12, v13, v14, v19` — **v9 is not compiled in**. Both `jm/` and `csf/` command-buffer backends exist in the binary, but with no v9 entry-point table, GPU ID `9093` has nothing to bind to → falls back to the "Unknown gpu_id" path.

wonderkast02's own repo ([`panvk-g720-kbase-csf`](https://github.com/wonderkast02/panvk-g720-kbase-csf)) has since moved much further on the **CSF** side — native `kbase_kmod.c` against real Kbase/CSF (not a wrapper), full graphics pipeline, MSAA, tessellation, even Wine/Box64/DXVK bring-up. Worth reading end to end: it's the clearest public example of what a *complete* bring-up on this general family (kbase → pan_kmod → PanVK) looks like, and its "Próximos passos" / PoC-milestone structure is what §Roadmap below is modeled on. The CSF/JM split means none of that command-buffer work transfers to v9 directly, but the kbase-bring-up methodology (ioctl validation → GPUPROPS → context → memory → job/queue submission → PanVK) transfers exactly.

## 3. Cross-reference: LukeValen's native G52 (v7) build — same failure shape (at the time)

[`LukeValen/panvk-mali-g52`](https://github.com/LukeValen/panvk-mali-g52) — native on-device Termux build of the same Mesa 26.3.0-devel, on a Mali-G52 MC2 (Bifrost/JM, v7, same kbase UAPI generation — 11.38 there vs 11.46 here).

At the time this was tested: `vkCreateInstance` succeeded, but **`vkEnumeratePhysicalDevices` returned 0 devices**, even with `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` set. Luke's own repo has since progressed well past this point (raw JM job-submission harness, order-of-operations fix for `EXEC_INIT` also independently found there — see §7).

As §Status above notes, this wasn't a v7-specific bug — `pan_kmod` simply had no kbase backend at all, on any arch, so `vkEnumeratePhysicalDevices` never looked at `/dev/mali0` in stock Mesa.

## 4. Upstream Mesa status for v9

- Igalia, [PanVK Extension Sprint: Mesa 26.1](https://christian-gmeiner.info/2026-04-20-panvk-extensions/) (Apr 2026) — active work explicitly scoped to v9+ GPUs.
- [Mesa/Panfrost docs](https://docs.mesa3d.org/drivers/panfrost.html) — PanVK is "conformant on Mali-G610, non-conformant on other GPUs" (not "unsupported").
- [DeepWiki PanVK architecture overview](https://deepwiki.com/bminor/mesa-mesa/2.4-panvk-(arm-mali-vulkan-driver)) — lists Bifrost/Valhall-JM (v9) as one of the supported generation groups in the arch-dispatch design.

~~v9 is being actively worked on upstream; its absence from the G720 build looks like scope choice for that specific build, not a gap in Mesa itself.~~

**Correction (2026-09-05):** that guess was wrong. It *is* a gap in Mesa itself, not a packaging choice. In stock Mesa `src/panfrost/vulkan/meson.build`, `jm_archs = [6, 7]` and the build loop is `foreach arch : [6, 7, 10, 11, 12, 13, 14]` — v9 is excluded on purpose, because **PanVK's `jm/` command-buffer backend is Bifrost-only, written entirely against `PAN_ARCH < 9`**. It is not a JM-generic backend that merely forgot v9.

Adding v9 to both lists compiles 19 of 24 objects and then fails with 69 errors: the `jm/` sources reach for struct members and helpers that the shared headers gate behind `#if PAN_ARCH < 9`, and for genxml descriptors (`Renderer State`, `Attribute Buffer`, `Invocation`) that **do not exist at v9** — Valhall replaced them with `Shader Program`/SPD and `Resource` tables, and changed the Compute/Tiler job section layouts outright.

Full evidence, error breakdown, and the two-line patch: [`docs/why-v9-is-a-port.md`](docs/why-v9-is-a-port.md). **Update:** the 69-error wall above has since been worked through far enough for compute to work — see §7.

> **Provenance note on "19 of 24 objects / 69 errors":** these figures come from the original 2026-09-05 build attempt, whose log was not retained. The build logs that *are* published measure something different and should not be read as the source of those numbers: [`evidence/historical/build_v9_full.HISTORICAL.log`](evidence/historical/build_v9_full.HISTORICAL.log) contains **100 unique `error:` messages** and stops at `[842/1029]`, and [`evidence/historical/build_v9_check.HISTORICAL.log`](evidence/historical/build_v9_check.HISTORICAL.log) contains **16**. The qualitative conclusion — `jm/` is Bifrost-only and v9 needs a port — is unaffected and is what the logs do support. See [`docs/INVENTORY.md`](docs/INVENTORY.md).

## 5. Kernel driver source

The kernel-side `mali_kbase` driver is released by ARM under **GPLv2** (separate from the closed userspace blob) — this is not reverse-engineered.

- Official: https://developer.arm.com/downloads/-/mali-drivers/valhall-kernel
- GitHub mirror w/ version history (R38P1 → R48P0, stale since Apr 2024 — live device driver here is R54P1, so check ARM's page directly for anything newer): https://github.com/ExtremeXT/valhall_drivers
  - `driver/product/kernel/drivers/gpu/arm/midgard/` — full `jm/` + `csf/` source

Live module extracted directly from this device's running `vendor_dlkm` (not an old firmware dump) — file itself is explicitly labeled JM by MediaTek:
```
mali_kbase_mt6789_a16w_jm.ko   → version=r54p1-12eac0 (UK version 11.46)
                                  vermagic=6.12.38-android16-5-...
```
(companion modules: `mali_mgm_mt6789_a16w_jm.ko`, `mali_prot_alloc_mt6789_a16w_jm.ko`)

## 6. Native Mesa build via Termux — ✅ builds, and it answers the v9 question

Following Luke's approach (native on-device build, no PC/NDK), using his [Termux/Android detection patch](https://github.com/LukeValen/panvk-mali-g52/blob/main/patches/termux-android-detection-fixes.patch). The `meson setup` dependency issues (libdrm, cutils/WSI, Python packaging/mako, LLVMSPIRVLib) are all resolved; Mesa 26.3.0-devel now builds on-device with clang 21.1.8 / NDK r29 (`aarch64-unknown-linux-android24`), producing a ~20 MB unstripped `libvulkan_panfrost.so`.

The question this was meant to settle — *does a from-source build, not arch-trimmed like the G720 binary, surface v9 automatically?* — **No, not without the patches in §7.**

```
$ for v in 6 7 9 10 11 12 13 14; do
    printf 'panvk_v%-2s : %s\n' $v \
      "$(nm --defined-only libvulkan_panfrost.so | grep -c "panvk_v${v}_")"
  done
panvk_v6  : 106     panvk_v10 : 128     panvk_v13 : 128
panvk_v7  : 106     panvk_v11 : 128     panvk_v14 : 126
panvk_v9  :   0     panvk_v12 : 128
```

⚠️ Use `nm --defined-only`, **not** `nm -D`. Per-arch libs are built with `gnu_symbol_visibility : 'hidden'` (`src/panfrost/vulkan/meson.build:239`), so `nm -D` reports zero for *every* arch and tells you nothing.

Trying to force v9 in is what produced the finding in §4 — see [`docs/why-v9-is-a-port.md`](docs/why-v9-is-a-port.md). That work has since progressed to a working compute path — see §7.

An alternative build path worth trying if the Termux route stalls: `leegao`'s [`mesa-funnymdzz`](https://github.com/leegao/mesa-funnymdzz) (forked from [`funnymdzz/mesa`](https://github.com/funnymdzz/mesa), and the base wonderkast02 built from) cross-compiles from a PC with the real Android NDK instead of building natively on-device. Its [`setup.sh`](https://github.com/leegao/mesa-funnymdzz/blob/ci/setup.sh) takes a different approach to the same libcutils/liblog/WSI problem Luke's patch solves by editing source: it generates **stub `.pc` files** for `cutils`, `hardware`, `log`, `sync`, `nativewindow`, `ui`, etc. via `pkg-config`, builds host-side codegen tools first (`mesa_clc`, `vtn_bindgen2`, `panfrost_compile` — these must run on the *build* machine, not the target), then cross-compiles the real target build against a `--cross-file`. Its explicit option `-Dpanfrost-kmds=kbase,panthor` is the flag that selects which `pan_kmod` backend(s) get built — this is the base the working v9 backend in §7 is built on.

**Toolchain trap for anyone building on Termux + proot:** if you configure the build under Termux (Termux clang, bionic) and then run `ninja` from inside a proot distro, `cc` resolves to the distro's glibc gcc and you silently mix ABIs — `/usr/bin` precedes `/data/data/com.termux/files/usr/bin` in PATH there. Prefix every invocation with `PATH=/data/data/com.termux/files/usr/bin:$PATH`. Termux clang itself runs fine under proot.

## 7. v9 command-buffer backend — native compute dispatch validated (2026-09-09)

Full writeup, exact bugs found (with patches), and validation output: [`docs/v9-compute-dispatch-validated.md`](docs/v9-compute-dispatch-validated.md).

Short version: on top of the `jm_archs`/build-matrix fix in `docs/why-v9-is-a-port.md`, physical-device enumeration was wired up for v9, the top-level `panvk_arch_dispatch`/`panvk_arch_dispatch_ret` routing was fixed (was hitting `UNREACHABLE`/UB for v9, not a clean failure), several `PAN_ARCH` boundary-condition gaps were patched, and a native v9 compute-dispatch path was written using v9's own `Compute Job`/`Compute Payload` genxml struct (simpler than Bifrost's split layout). Graphics (`CmdDraw*`) is stubbed as a no-op for v9 for now — not yet ported.

Two bugs specific to this session, both with exact patches in `patches/`:
- `panvk_physical_device.c` was missing the v9 prototype declaration (`patches/0002-...patch`) — definitions compiled fine, but the generic file couldn't see them.
- `KBASE_IOCTL_MEM_EXEC_INIT`'s `va_pages` was hardcoded to `4` (16 KB) — that's the *entire* EXEC_VA zone size, not a per-allocation value, so it filled up on the first real shader-binary allocation. Raised to `1024` (4 MB) (`patches/0003-...patch`).

**Falsifiable claim:** a compute shader (`data[0] = 777u`) dispatched via `vkCmdDispatch`, submitted via `vkQueueSubmit`, waited on via `vkQueueWaitIdle`, and read back correctly via mapped memory — reproduced 3 times in a row with byte-identical output, including the full intermediate `kbase MEM_ALLOC` request sequence. Not a raw-ioctl harness result — this goes through real PanVK end to end.

---

## Roadmap

Modeled on wonderkast02's PoC-milestone structure — small, independently checkable claims, no "it works" until there's a specific test proving it. Each phase lists what would falsify it.

- [x] **Phase 0 — Kbase/JM bring-up (raw ioctl, no Mesa).** `/dev/mali0` open, version check, `SET_FLAGS`, `MEM_ALLOC`, `mmap`, CPU↔GPU coherency, `GET_GPUPROPS` with and without a context. *(§1, §1b — done)*
- [x] **Phase 1 — Understand why v9 has no backend.** Not a missing meson entry; `jm/` is structurally Bifrost-only (genxml descriptors that don't exist at v9, gated helpers). *(§4 — done)*
- [x] **Phase 2 — `pan_kmod` kbase backend (`kbase_kmod.c`).** `vkEnumeratePhysicalDevices` returns 1 device on this G57 (v9) with correct name/ID/memory heaps. *(done, folded into §7's fixes — the arch-dispatch/EXEC_INIT bugs found there were blocking this too)*
- [x] **Phase 3 — Minimal v9 command-buffer backend.** **Falsifiable target met:** one compute shader dispatches and produces a verifiable result via readback, reproduced 3x. *(§7 — done)*
- [x] **Phase 4 — Graphics pipeline (partial).** **Falsifiable target met:** vertex + fragment on an offscreen render target with CPU readback — a 64x64 partial triangle, 512/4096 non-black, centre red, corner clear. The v9 equivalent of wonderkast02's "triângulo offscreen + readback" milestone. Draw entry points are **no longer stubs**. Root cause of the preceding clear-only result was the missing v9 Shader Environment FAU count. *([`docs/graphics-progress.md`](docs/graphics-progress.md), [`docs/fau-root-cause.md`](docs/fau-root-cause.md) — done for this workload)*
  - **Sub-phases since closed:** 4.1 indexed draws, 4.2 multi-descriptor-set resource tables, 4.3 multiple render targets, 4.4 indirect draw and indirect dispatch — all VERIFIED-HW with negative controls and 3x repetition. See [`docs/phase4-open-questions.md`](docs/phase4-open-questions.md) for what each result does *not* cover, including a self-audit for false positives.
  - Still open within Phase 4: MSAA, depth/stencil validation, multi-layer rendering, tiled/AFBC layouts, occlusion queries, secondary command buffers.
  - **2026-10-04:** occlusion queries (434/441, 7 NS), secondary command buffers (17/19, 2 NS) and input assembly (111 pass, 87 NS) now 0 fail on CTS, 3x, after patch `0058`. See [`docs/roadmap-phase8-13.md`](docs/roadmap-phase8-13.md).
- [x] **Phase 5 — Texture sampling, depth/stencil, blending, MSAA.** Same shape as wonderkast02 §"PanVK nativo", ported to v9's descriptor layout. Depth/stencil and blend *descriptors* are emitted and captured today, but nothing validates their behaviour — see [`docs/known-limitations.md`](docs/known-limitations.md).
  - **5a texture sampling: done.** Nearest and linear filtering, four address modes, OPTIMAL (AFBC) and LINEAR textures, 48/48 against a CPU model with error 0, negative control fails all cases. See [`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md#4-texture-sampling-phase-5a-verified-hw).
  - **5b stencil, 5c blending: done.** 12 stencil cases on D24S8/D32S8/S8 read back through the stencil test itself, 13 blend states, all against CPU models, controls fail. CTS texture/sampler/blend/stencil subsets: 0 fail. See [`docs/phase5-7.md`](docs/phase5-7.md). Anisotropy, independent and dual-source blend, logic op, 4x/8x MSAA with sample shading, alpha-to-coverage and depth/stencil resolve: CTS subsets 0 fail, 3x, after patch `0059` (2026-10-04, [`docs/roadmap-phase8-13.md`](docs/roadmap-phase8-13.md)).
- [x] **Phase 6 — WSI / swapchain.** Termux:X11 or native Android surface, vkcube-equivalent, sustained frame test.
  - **Spinning cube presented on Termux:X11 through `VK_KHR_xcb_surface` + `VK_KHR_swapchain`**, 49-56 fps, on-screen pixels read back from the X server and matched to a CPU rasterizer (0 bad, 3x, negative control fails). See [`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md#3-spinning-cube).
  - **Sustained run and swapchain recreation: done.** 30,000 frames with a window resize and swapchain recreate every 500 frames (60 swapchains), 3x, on-screen check 0 bad, RSS flat. X11 only; Android native surface untested. See [`docs/phase5-7.md`](docs/phase5-7.md#4-sustained-wsi-and-swapchain-recreation-phase-6-verified-hw).
- [x] **Phase 7 — Wine/Box64/DXVK bring-up (optional, stretch).** Only after Phase 4 is solid — wonderkast02's G720 LAB findings on missing features (`geometryShader`, `textureCompressionBC`, etc.) likely apply here too and are worth re-checking against this hardware's real feature bits rather than assumed.
  - **Gap analysis done, bring-up not attempted.** Against DXVK master's required-feature list this driver is missing `geometryShader`, `multiDrawIndirect`, `multiViewport`, `shaderClipDistance`, `shaderCullDistance`, `textureCompressionBC` and `VK_EXT_robustness2`, so DXVK will not create a device. See [`docs/phase5-7.md`](docs/phase5-7.md#5-dxvk-requirement-gap-phase-7-verified-src--reported-features).
  - **Bring-up done for D3D11 (2026-10-04).** `multiDrawIndirect` and `robustness2` came from `0044`/`0045`, clip/cull distance and BC are handled by the Winlator Wrapper. Real DXVK 1.12.1-sarek (FL 11_1) and 2.3.1 (FL 10_1) render a D3D11 test program through Wine 11.16 + Winlator Wrapper + adrenotools, 3x with readback and controls. Winlator test builds: `FourFectVK-G57-alpha3`. Still missing: geometry shaders, tessellation, transform feedback. See [`docs/phase7-winlator-dxvk.md`](docs/phase7-winlator-dxvk.md).
  - **🧪 EXPERIMENTAL v9 feature work started** to close that gap from the driver side: `multiDrawIndirect`/`drawIndirectCount` (`0044`), `nullDescriptor` (`0045`), sampler min/max by shader emulation (`0046`, cube maps broken). See [`docs/v9-experimental-features.md`](docs/v9-experimental-features.md).

No phase here claims Vulkan conformance or "games will run". CTS now runs on the device, but only on a few small groups (see the CTS table in the status section), and it found three driver bugs (patches 0041-0043) that this project's own tests had missed. See [`docs/cts-wsi-texture.md`](docs/cts-wsi-texture.md).

Also still open, not yet on this list: `vkCmdDispatchIndirect` for v9 (direct dispatch only so far), and the practical size ceiling of the 4 MB EXEC_VA zone for larger/more complex shaders than the single-buffer test in §7.

---

## Open questions / help wanted

- Does the `vkEnumeratePhysicalDevices` → 0 devices issue reproduce on **any** JM-arch PanVK build (v6/v7) using stock upstream Mesa (no kbase backend), or is it specific to something in how each of us built/packaged it? *(Resolved for this repo's own build — see §7 — but worth confirming against other builds.)*
- Is anyone already working on a PanVK v9 `jm/` backend upstream (graphics, not just compute)? Igalia's extension sprint is scoped to "v9+", but that phrasing may only mean v10+ in practice — worth confirming before duplicating the draw-call porting effort.
- `pan_kmod_dev_props.afbc_features` has no `KBASE_GPUPROP_*` equivalent that I could find. `panfrost_kmod.c` gets it from `DRM_PANFROST_PARAM_AFBC_FEATURES`. Where does kbase expose it — or is it meant to be derived from the GPU ID?
- Anyone with a Mali-G31/G51/G57/G68/G77/G78 device (Bifrost or Valhall-JM) willing to run the same raw-ioctl tests + a PanVK build, to compare notes, especially on the graphics-path porting work ahead?

## Repo layout

```
docs/STATUS.md                          labelled status of everything  <- start here
docs/INVENTORY.md                       what was examined / published / excluded
docs/hardware-runtime.md                GPU ID, gpuprops, kbase, EXEC_VA sizing
docs/compute-progress.md                v9 compute path + enablement fixes
docs/graphics-progress.md               v9 draw path, job chain, captured descriptors
docs/raw-jm-progress.md                 raw ioctl, atom size/stride, event codes
docs/fau-root-cause.md                  the causal FAU-count defect + A/B evidence
docs/resource-table-findings.md          res_count / RESOURCE layout / table packing
docs/test-matrix.md                     every test and its hardware result
docs/known-limitations.md               read before quoting anything from this repo
docs/reproduction.md                    exact build and run commands
docs/historical-superseded.md           wrong turns, kept with explicit corrections
docs/kbase-uapi-r54p1.md                33 dispatched ioctls, method, version negotiation
docs/gpuprops-without-context.md        GET_GPUPROPS w/o a context + pan_kmod_dev_props mapping
docs/why-v9-is-a-port.md                why the 2-line meson patch isn't enough
docs/v9-compute-dispatch-validated.md   original v9 compute-dispatch writeup
docs/porting-log.md                     chronological working log

tests/compute/                          compute harnesses (dispatch, SPD readback, indirect)
tests/graphics/                         triangle draw + begin/end rendering harnesses
tests/raw-jm/                           direct /dev/mali0 ioctl harnesses
tests/shaders/                          GLSL + prebuilt SPIR-V used by the harnesses
tests/test_kbase2.c, test_kbase3.c      early ioctl / GET_GPUPROPS probes

evidence/logs/                          run logs incl. the full FAU A/B series
evidence/descriptors/                   pandecode dumps + raw descriptor hex captures
evidence/framebuffer/                   panvk_triangle.png (real output, lossless from the original PPM) + PNG upscale
evidence/schema/                        resource-table / genxml audit output
evidence/builds/                        build provenance for each FAU A/B side
evidence/historical/                    pre-port build failures, early draw attempts
evidence/MANIFEST.md                    SHA-256 of every evidence artifact
results/gpuprops-g57-r54p1.txt          raw output of test_kbase3 on this device

patches/0001..0003                      enablement: meson arch, v9 prototypes, EXEC_VA
patches/0004..0005                      HISTORICAL WIP draw path (superseded by 0010/0012)
patches/0010..0015                      current v9 JM draw/compute/queue/arch/build patches
patches/0020                            the FAU-count fix in isolation
patches/9001                            third-party Termux/Android detection fixes

tools/                                  bring-up scripts; NOT a supported build path
```

## Credits / prior art

- **hafiz** — rebased this project onto upstream Mesa (`patches-upstream/0001`), and wrote the chained and asynchronous kbase JM submission with sync_file fences, forward pixel kill on v9, multisampled storage images and the Android HAL / AFBC swapchain work. FourFectVK test6 and later are built on his base.
- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) — CSF/G720 bring-up this repo's methodology and roadmap structure is modeled on.
- [LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52) — native Termux build + Android-detection patch used in §6; the v7/G52 cross-reference in §3; independently found the same `EXEC_INIT`-before-`JIT_INIT` ordering fix referenced in §7.
- [leegao/mesa-funnymdzz](https://github.com/leegao/mesa-funnymdzz) (forked from [funnymdzz/mesa](https://github.com/funnymdzz/mesa)) — cross-compile tooling and stub-`.pc` approach referenced in §6; the base wonderkast02 built from, and the base the §7 work is built on.
- Icecream95 and the Panfrost/PanVK contributors — the underlying reverse-engineering and driver work all of this sits on top of.

Related: [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf), [LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52)
