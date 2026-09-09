# v9 compute dispatch — validated end-to-end (2026-09-09)

This documents Phase 3 (see Roadmap in README) reaching its falsifiable target: a compute shader dispatched through real PanVK, executed on the GPU, and read back correctly — on Mali-G57 MC2 (v9/JM), reproduced 3 times with identical results.

## Prerequisite work

`§4`/`docs/why-v9-is-a-port.md` established that `jm/` is structurally Bifrost-only. Since that finding, the following was done to reach a working v9 command-buffer path (not covered in detail here — see repo's working notes / patches directory for the exhaustive list):

- Added `9` to the `meson.build` arch-build matrix (`patches/0001-...patch`) — `libpanvk_v9.a` now compiles.
- Added `case 9` to the arch-support switch in `panvk_physical_device.c` (DRM path and kbase path) — physical device no longer unconditionally rejected.
- Added `case 9` to `panvk_arch_dispatch` / `panvk_arch_dispatch_ret` in `panvk_macros.h` — this is the top-level switch that routes generic Vulkan entrypoints (including `vkCreateDevice` itself) to the right per-arch function. Without this, hitting the `default: UNREACHABLE(...)` case for v9 compiles to `__builtin_unreachable()` under `NDEBUG` — genuine undefined behavior, not a clean failure, which manifested as execution jumping into unrelated CSF-only code.
- Fixed several `#if PAN_ARCH < 9` / `#if PAN_ARCH >= 10` boundary conditions that left v9 itself unhandled (e.g. `panvk_cmd_graphics_state.tsd`, `panvk_rendering_state::fb`).
- Wrote a native v9 compute-dispatch path (`jm/panvk_vX_cmd_dispatch.c`, `PAN_ARCH >= 9`) using v9's native `Compute Job` / flat `Compute Payload` struct — simpler than Bifrost's split `Invocation`/`Parameters`/`Draw` structs, and reusing the SPD-style `Shader Environment` sub-struct already used on the fragment side.
- Graphics entrypoints (`CmdDraw*`, `CmdBeginRendering`, `CmdEndRendering`) stubbed as no-ops for `PAN_ARCH >= 9` so the library could link — **graphics does not work yet on v9, only compute.**

## Bugs found this session (with exact patches)

### 1. `panvk_physical_device.c` — missing v9 prototype declaration

`libpanvk_v9.a` compiled fine (definitions existed), but `panvk_physical_device.c` failed with `implicit-function-declaration` for `panvk_v9_get_physical_device_extensions`, `..._features`, `..._properties`, `..._create_device`, `..._destroy_device`. Root cause: a macro generator (`PER_ARCH_FUNCS(_ver)`) invoked manually per arch, omitting `9`:

```c
PER_ARCH_FUNCS(6);
PER_ARCH_FUNCS(7);
PER_ARCH_FUNCS(10);   // 9 missing
PER_ARCH_FUNCS(12);
PER_ARCH_FUNCS(13);
PER_ARCH_FUNCS(14);
```

Fix: `patches/0002-panvk-physical-device-proto-v9.patch`.

### 2. `KBASE_IOCTL_MEM_EXEC_INIT` — `va_pages` too small

A prior fix had dropped `va_pages` from `0x100000` (rejected by this device with EPERM) to `4` — which happened to pass `EXEC_INIT`, but `4` (16 KB) turned out to be the **total size of the entire EXEC_VA zone** for all future executable allocations, not a per-call value. The first real executable allocation (the compiled shader binary, itself requesting `va_pages=4`) then failed with `ENOMEM` — the zone was already exhausted by internal overhead.

Raised to `va_pages = 1024` (4 MB). Fix: `patches/0003-kbase-exec-va-pages.patch`.

## Validation

Minimal custom Vulkan compute test (`vkCreateInstance` → `vkCreateDevice` → storage buffer → shader module (original GLSL compute shader, `data[0] = 777u;`) → compute pipeline → command buffer → `vkQueueSubmit` → `vkQueueWaitIdle` → read back):

```
buffer initialized with 0xAA, first u32=0xaaaaaaaa
...
vkCreateComputePipelines ok
...
vkQueueSubmit ok
vkQueueWaitIdle ok
buffer after dispatch, first u32=0x00000309 (expected 0x00000309 = 777)
```

Run 3 times in a row, byte-for-byte identical output each time — including the full sequence of intermediate `kbase MEM_ALLOC` requests (sizes/flags), not just the final result. This rules out a race condition or a one-off fluke: the GPU is genuinely compiling and executing the SPIR-V-derived shader and writing the correct result back to CPU-visible memory.

## What this proves vs. what's still open

**Proven:** the full chain — SPIR-V → compiled to native Mali-G57 ISA → executable memory allocated via kbase → job submitted → GPU executes → result written back — works correctly on v9/JM through real PanVK, not a raw-ioctl harness outside Mesa.

**Not yet covered:**
- Graphics (draw calls) — still stubbed as no-ops, needs a from-scratch port of `jm/panvk_vX_cmd_draw.c` to v9's unified resource-table descriptor model (v9 doesn't have Bifrost's separate `img`/`ubo`/`texture`/`sampler` tables).
- `vkCmdDispatchIndirect` — not implemented for v9 yet, direct dispatch only.
- Larger/more complex compute workloads than this single-buffer test — the practical ceiling of the 4 MB EXEC_VA zone for bigger shaders is unknown.
