# panvk-g99-jm

Investigation & testing notes: getting **PanVK** (Mesa's open-source Vulkan driver) working on **Mali-G57 MC2** (MediaTek Helio G99 / MT6789), a **Valhall Gen 1 / JM-frontend** GPU — Mesa arch bucket **v9**.

**Device:** Infinix Note 40 4G (X6853) — Mali-G57 MC2, GPU ID `9093`, live kernel driver `r54p1` (UAPI 11.46), kernel `6.12.38-android16`.

Most public PanVK testing/builds so far target **CSF** chips (G610/G615/G710/G720 — arch v10+). This repo tracks the v9/JM side specifically, since it's a different frontend (Job Manager, not Command Stream Frontend) and gets far less testing.

---

## Status: kernel driver confirmed working. PanVK userspace enumeration is the blocker.

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

## 2. PanVK-G720 (wonderkast02) test result — ❌ 0 extensions

Tested `wonderkast02`'s `PanVK-G720-0.1.0-alpha.2` build (Mesa 26.3.0-devel) as the Vulkan ICD in WinlatorMali:

- `Available Extensions: 0`
- `GPU Name: Device` (generic fallback, not "Mali-G57 MC2")

Binary analysis of `libvulkan_panfrost.so` shows compiled arch buckets: `v6, v7, v10, v12, v13, v14, v19` — **v9 is not compiled in**. Both `jm/` and `csf/` command-buffer backends exist in the binary, but with no v9 entry-point table, GPU ID `9093` has nothing to bind to → falls back to the "Unknown gpu_id" path.

## 3. Cross-reference: LukeValen's native G52 (v7) build — same failure shape

[`LukeValen/panvk-mali-g52`](https://github.com/LukeValen/panvk-mali-g52) — native on-device Termux build of the same Mesa 26.3.0-devel, on a Mali-G52 MC2 (Bifrost/JM, v7, same kbase UAPI generation — 11.38 there vs 11.46 here).

Result: `vkCreateInstance` succeeds, but **`vkEnumeratePhysicalDevices` returns 0 devices**, even with `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` set.

This is a different arch bucket (v7 is compiled in for G52, unlike v9 for G57) but hits the **same enumeration failure shape**. Combined with the kernel-level test above, this points toward a shared PanVK **userspace enumeration bug** affecting JM architectures generally — not a per-device or per-arch-bucket issue.

## 4. Upstream Mesa status for v9

- Igalia, [PanVK Extension Sprint: Mesa 26.1](https://christian-gmeiner.info/2026-04-20-panvk-extensions/) (Apr 2026) — active work explicitly scoped to v9+ GPUs.
- [Mesa/Panfrost docs](https://docs.mesa3d.org/drivers/panfrost.html) — PanVK is "conformant on Mali-G610, non-conformant on other GPUs" (not "unsupported").
- [DeepWiki PanVK architecture overview](https://deepwiki.com/bminor/mesa-mesa/2.4-panvk-(arm-mali-vulkan-driver)) — lists Bifrost/Valhall-JM (v9) as one of the supported generation groups in the arch-dispatch design.

v9 is being actively worked on upstream; its absence from the G720 build looks like scope choice for that specific build, not a gap in Mesa itself.

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

## 6. Attempting a native Mesa build via Termux (in progress)

Following Luke's approach (native on-device build, no PC/NDK), using his [Termux/Android detection patch](https://github.com/LukeValen/panvk-mali-g52/blob/main/patches/termux-android-detection-fixes.patch). Currently working through `meson setup` dependency issues (libdrm, cutils/WSI, Python packaging/mako, LLVMSPIRVLib) on a fresh Mesa clone. Will update here once it builds — the goal is to see whether a from-source build (not arch-trimmed like the G720 binary) surfaces v9 automatically and whether it hits the same enumeration wall Luke found on v7.

---

## Open questions / help wanted

- Does the `vkEnumeratePhysicalDevices` → 0 devices issue reproduce on **any** JM-arch PanVK build (v6/v7/v9), or is it specific to something in how each of us built/packaged it?
- If v9 gets added to a future PanVK-G720-style build, does it hit the same wall?
- Anyone with a Mali-G31/G51/G57/G68/G77/G78 device (Bifrost or Valhall-JM) willing to run the same raw-ioctl test + a PanVK build, to compare notes?

Related: [wonderkast02/panvk-g720-kbase-csf](https://github.com/wonderkast02/panvk-g720-kbase-csf), [LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52)
