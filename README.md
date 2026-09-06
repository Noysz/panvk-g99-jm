# panvk-g99-jm

Investigation & testing notes: getting **PanVK** (Mesa's open-source Vulkan driver) working on **Mali-G57 MC2** (MediaTek Helio G99 / MT6789), a **Valhall Gen 1 / JM-frontend** GPU — Mesa arch bucket **v9**.

**Device:** Infinix Note 40 4G (X6853) — Mali-G57 MC2, GPU ID `9093`, live kernel driver `r54p1` (UAPI 11.46), kernel `6.12.38-android16`.

Most public PanVK testing/builds so far target **CSF** chips (G610/G615/G710/G720 — arch v10+). This repo tracks the v9/JM side specifically, since it's a different frontend (Job Manager, not Command Stream Frontend) and gets far less testing.

> ⚠️ Experimental reverse-engineering / bring-up project. No claim of Vulkan conformance or game compatibility is made anywhere in this repo unless explicitly marked as such with hardware evidence.

---

## Status: kernel driver confirmed working. For v9 there is no PanVK userspace to enumerate *with* — see §4.

Two separate things were suspected to be the blocker; they turned out to be different problems:

1. **Kernel side — not a blocker.** `/dev/mali0` responds correctly to the full ioctl chain, and the GPU property table is readable with no context at all (§1, §1b).
2. **Userspace side — the actual blocker for v9.** PanVK has no v9 backend. Not "v9 wasn't packaged", but "v9 was never implemented" — the `jm/` backend is Bifrost-only (§4). Separately, LukeValen's v7 build *is* compiled in and still enumerates 0 devices (§3), which is a second, independent bug — `pan_kmod` has no kbase backend at all, only `panfrost_kmod.c` and `panthor_kmod.c`, so enumeration goes through `drmGetDevices2` on `/dev/dri/*` and never touches `/dev/mali0`.

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

Follow-up probe ([`tests/test_kbase3.c`](tests/test_kbase3.c), full output in [`results/gpuprops-g57-r54p1.txt`](results/gpuprops-g57-r54p1.txt), writeup in [`docs/gpuprops-without-context.md`](docs/gpuprops-without-context.md)):

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

## 3. Cross-reference: LukeValen's native G52 (v7) build — same failure shape

[`LukeValen/panvk-mali-g52`](https://github.com/LukeValen/panvk-mali-g52) — native on-device Termux build of the same Mesa 26.3.0-devel, on a Mali-G52 MC2 (Bifrost/JM, v7, same kbase UAPI generation — 11.38 there vs 11.46 here).

Result: `vkCreateInstance` succeeds, but **`vkEnumeratePhysicalDevices` returns 0 devices**, even with `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` set.

This is a different arch bucket (v7 is compiled in for G52, unlike v9 for G57) but hits the **same enumeration failure shape**. As §Status above notes, this isn't a v7-specific bug either — `pan_kmod` simply has no kbase backend at all yet, on any arch, so `vkEnumeratePhysicalDevices` never looks at `/dev/mali0` in stock Mesa. Building `kbase_kmod.c` (§Roadmap, Phase 2) should fix Luke's v7 case immediately, independent of the v9 command-buffer work.

## 4. Upstream Mesa status for v9

- Igalia, [PanVK Extension Sprint: Mesa 26.1](https://christian-gmeiner.info/2026-04-20-panvk-extensions/) (Apr 2026) — active work explicitly scoped to v9+ GPUs.
- [Mesa/Panfrost docs](https://docs.mesa3d.org/drivers/panfrost.html) — PanVK is "conformant on Mali-G610, non-conformant on other GPUs" (not "unsupported").
- [DeepWiki PanVK architecture overview](https://deepwiki.com/bminor/mesa-mesa/2.4-panvk-(arm-mali-vulkan-driver)) — lists Bifrost/Valhall-JM (v9) as one of the supported generation groups in the arch-dispatch design.

~~v9 is being actively worked on upstream; its absence from the G720 build looks like scope choice for that specific build, not a gap in Mesa itself.~~

**Correction (2026-09-05) — that guess was wrong, and this is the main finding of the repo so far.** It *is* a gap in Mesa itself, not a packaging choice. In stock Mesa `src/panfrost/vulkan/meson.build`, `jm_archs = [6, 7]` and the build loop is `foreach arch : [6, 7, 10, 11, 12, 13, 14]` — v9 is excluded on purpose, because **PanVK's `jm/` command-buffer backend is Bifrost-only, written entirely against `PAN_ARCH < 9`**. It is not a JM-generic backend that merely forgot v9.

Adding v9 to both lists compiles 19 of 24 objects and then fails with 69 errors: the `jm/` sources reach for struct members and helpers that the shared headers gate behind `#if PAN_ARCH < 9`, and for genxml descriptors (`Renderer State`, `Attribute Buffer`, `Invocation`) that **do not exist at v9** — Valhall replaced them with `Shader Program`/SPD and `Resource` tables, and changed the Compute/Tiler job section layouts outright.

Full evidence, error breakdown, and the two-line patch: [`docs/why-v9-is-a-port.md`](docs/why-v9-is-a-port.md).

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

The question this was meant to settle — *does a from-source build, not arch-trimmed like the G720 binary, surface v9 automatically?* — **No.**

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

Trying to force v9 in is what produced the finding in §4 — see [`docs/why-v9-is-a-port.md`](docs/why-v9-is-a-port.md). Consequence: the enumeration wall Luke hit on v7 can't be compared against v9 yet, because there is no v9 build to hit it with.

An alternative build path worth trying if the Termux route stalls: `leegao`'s [`mesa-funnymdzz`](https://github.com/leegao/mesa-funnymdzz) (forked from [`funnymdzz/mesa`](https://github.com/funnymdzz/mesa), and the base wonderkast02 built from) cross-compiles from a PC with the real Android NDK instead of building natively on-device. Its [`setup.sh`](https://github.com/leegao/mesa-funnymdzz/blob/ci/setup.sh) takes a different approach to the same libcutils/liblog/WSI problem Luke's patch solves by editing source: it generates **stub `.pc` files** for `cutils`, `hardware`, `log`, `sync`, `nativewindow`, `ui`, etc. via `pkg-config`, builds host-side codegen tools first (`mesa_clc`, `vtn_bindgen2`, `panfrost_compile` — these must run on the *build* machine, not the target), then cross-compiles the real target build against a `--cross-file`. Its explicit option `-Dpanfrost-kmds=kbase,panthor` is the flag that selects which `pan_kmod` backend(s) get built — that's the option Phase 2 below needs once `kbase_kmod.c` exists.

**Toolchain trap for anyone building on Termux + proot:** if you configure the build under Termux (Termux clang, bionic) and then run `ninja` from inside a proot distro, `cc` resolves to the distro's glibc gcc and you silently mix ABIs — `/usr/bin` precedes `/data/data/com.termux/files/usr/bin` in PATH there. Prefix every invocation with `PATH=/data/data/com.termux/files/usr/bin:$PATH`. Termux clang itself runs fine under proot.

---

## Roadmap

Modeled on wonderkast02's PoC-milestone structure — small, independently checkable claims, no "it works" until there's a specific test proving it. Each phase lists what would falsify it.

- [x] **Phase 0 — Kbase/JM bring-up (raw ioctl, no Mesa).** `/dev/mali0` open, version check, `SET_FLAGS`, `MEM_ALLOC`, `mmap`, CPU↔GPU coherency, `GET_GPUPROPS` with and without a context. *(§1, §1b — done)*
- [x] **Phase 1 — Understand why v9 has no backend.** Not a missing meson entry; `jm/` is structurally Bifrost-only (genxml descriptors that don't exist at v9, gated helpers). *(§4 — done)*
- [ ] **Phase 2 — `pan_kmod` kbase backend (`kbase_kmod.c`).** Wire `GET_GPUPROPS` → `pan_kmod_dev_props` (mapping already done in §1b) so `pan_kmod_dev_create()` can open `/dev/mali0` and populate device props with *no* v9 command-buffer code involved yet. **Falsifiable target:** `vkEnumeratePhysicalDevices` returns 1 device (name, ID, memory heaps correct) on both this G57 (v9) and Luke's G52 (v7) — extensions can still legitimately be 0 past this point, since no command-buffer backend exists for either arch to advertise real capability against.
  - `afbc_features` mapping is still open (see below) — may need a fallback default rather than blocking this phase.
- [ ] **Phase 3 — Minimal v9 command-buffer backend.** Port only what's needed for `vkCreateDevice` + a trivial compute dispatch: Shader Program/SPD descriptors, Resource tables, v9 Compute job layout. Reference: gallium's `pan_cmdstream.c` v9 paths (Panfrost OpenGL already solved this for compute/3D on this exact arch — porting known-working reference code, not reverse-engineering from scratch).
  - **Falsifiable target:** one compute shader dispatches and produces a verifiable result via readback, matching the pattern in wonderkast02's own compute milestone.
- [ ] **Phase 4 — Graphics pipeline.** Vertex + fragment, offscreen render target, readback — the v9 equivalent of wonderkast02's "triângulo offscreen + readback" milestone.
- [ ] **Phase 5 — Texture sampling, depth/stencil, blending, MSAA.** Same shape as wonderkast02 §"PanVK nativo", ported to v9's descriptor layout.
- [ ] **Phase 6 — WSI / swapchain.** Termux:X11 or native Android surface, vkcube-equivalent, sustained frame test.
- [ ] **Phase 7 — Wine/Box64/DXVK bring-up (optional, stretch).** Only after Phase 4 is solid — wonderkast02's G720 LAB findings on missing features (`geometryShader`, `textureCompressionBC`, etc.) likely apply here too and are worth re-checking against this hardware's real feature bits rather than assumed.

No phase here claims Vulkan conformance or "games will run" — that would need CTS, which is out of scope until well past Phase 5.

---

## Open questions / help wanted

- Does the `vkEnumeratePhysicalDevices` → 0 devices issue reproduce on **any** JM-arch PanVK build (v6/v7), or is it specific to something in how each of us built/packaged it? (v9 is out of the running until someone ports it — §4.)
- ~~If v9 gets added to a future PanVK-G720-style build, does it hit the same wall?~~ **Answered: v9 can't simply "be added".** It needs a Valhall-JM command-buffer backend written from scratch, using gallium's `pan_cmdstream.c` v9 paths as the reference. See [`docs/why-v9-is-a-port.md`](docs/why-v9-is-a-port.md).
- Is anyone already working on a PanVK v9 `jm/` backend upstream? Igalia's extension sprint is scoped to "v9+", but that phrasing may only mean v10+ in practice — worth confirming before duplicating effort.
- `pan_kmod_dev_props.afbc_features` has no `KBASE_GPUPROP_*` equivalent that I could find. `panfrost_kmod.c` gets it from `DRM_PANFROST_PARAM_AFBC_FEATURES`. Where does kbase expose it — or is it meant to be derived from the GPU ID?
- Anyone with a Mali-G31/G51/G57/G68/G77/G78 device (Bifrost or Valhall-JM) willing to run the same raw-ioctl tests + a PanVK build, to compare notes?

## Repo layout

```
docs/kbase-uapi-r54p1.md          33 dispatched ioctls, method, version negotiation
docs/gpuprops-without-context.md  GET_GPUPROPS w/o a context + pan_kmod_dev_props mapping
docs/why-v9-is-a-port.md          why the 2-line meson patch isn't enough
tests/test_kbase2.c               version check, set_flags, mem_alloc, mmap, coherency
tests/test_kbase3.c               GET_GPUPROPS probe (incl. negative tests)
results/gpuprops-g57-r54p1.txt    raw output of test_kbase3 on this device
patches/0001-panvk-add-v9-...     the meson patch (necessary, not sufficient)
```

## Credits / prior art

- [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf) — CSF/G720 bring-up this repo's methodology and roadmap structure is modeled on.
- [LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52) — native Termux build + Android-detection patch used in §6; the v7/G52 cross-reference in §3.
- [leegao/mesa-funnymdzz](https://github.com/leegao/mesa-funnymdzz) (forked from [funnymdzz/mesa](https://github.com/funnymdzz/mesa)) — cross-compile tooling and stub-`.pc` approach referenced in §6; the base wonderkast02 built from.
- Icecream95 and the Panfrost/PanVK contributors — the underlying reverse-engineering and driver work all of this sits on top of.

Related: [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf), [LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52)
