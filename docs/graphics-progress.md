# Graphics progress — v9 / JM

## Summary

**VERIFIED:** a Vulkan draw submitted through PanVK on v9/JM executes on the
GPU and rasterizes a partial triangle into an offscreen `VkImage`, which is then
read back correctly on the CPU.

**NOT IMPLEMENTED:** WSI, swapchain, present, and general application support.
See [known-limitations.md](known-limitations.md) before quoting any of this.

## Job chain — VERIFIED

v9 requires **`MALLOC_VERTEX_JOB`**. It is neither Bifrost's
`COMPUTE_JOB` + `TILER_JOB` pair nor `INDEXED_VERTEX_JOB`.

One batch holds two job descriptors. The driver re-reads their headers after
each of the two `JOB_SUBMIT` calls, so the same two jobs appear twice in the log
with different status:

| After submitting | job[0] type 11 (MALLOC_VERTEX) | job[1] type 9 (FRAGMENT) |
|---|---|---|
| `vtc_jc` (vertex + tiler) | `0x01 (DONE)` | `0x00 (NOT_STARTED)` |
| `frag_jc` (fragment) | `0x01 (DONE)` | `0x01 (DONE)` |

`first_incomplete_task = 0` and `fault_pointer = 0x0` throughout. Reading the
headers *after* the wait is the only way to distinguish "the GPU ran this and
wrote nothing" from "the GPU refused this" — the pre-submit dump shows zeros in
both cases.

Job type is read out of the header as `(word4 >> 1) & 0x7f`. The raw capture
confirms the encoding directly: MVJ header byte `0x16` → `0x16 >> 1` = 11
(MALLOC_VERTEX), fragment job header byte `0x12` → `0x12 >> 1` = 9 (FRAGMENT).

Status decode is not "non-zero means error" — see
[raw-jm-progress.md](raw-jm-progress.md#event-codes) for the event table.

## The controlled partial-triangle result — VERIFIED

Evidence: [`evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log`](../evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log),
duplicated as [`evidence/framebuffer/panvk_visual.log`](../evidence/framebuffer/panvk_visual.log).

Target: 64x64 `VkImage`, linear, `rowPitch = 256`, format code 84,
`rt_count = 1`, attachment base `0x7b36ed8000`, `surf_stride = 16384`.

```
--- pixel summary, 64x64 image (rowPitch=256) ---
total non-black pixels: 512 of 4096

--- targeted validation ---
corner (0,0)   = 0,0,0,255
center (32,32) = 255,0,0,255

SUCCESS: partial triangle rasterized -- center red, corner still clear
```

### Why "partial" is the load-bearing word

512 / 4096 = **12.5%** coverage. The earlier A/B runs produced 4096/4096 — full
coverage — because that triangle covered the whole viewport. A fullscreen red
result is ambiguous: a mis-scaled clear, a full-surface blit, or a
degenerate-but-huge triangle all look identical to it. A 12.5% shape with a
**clear corner and a red centre** cannot be produced by any of those. That is
the reason a second, deliberately smaller workload was run.

Framebuffer artifacts:

* [`evidence/framebuffer/panvk_triangle.ppm`](../evidence/framebuffer/panvk_triangle.ppm)
  — P6, 64x64, maxval 255. The actual GPU output.
* [`evidence/framebuffer/panvk_triangle_512.png`](../evidence/framebuffer/panvk_triangle_512.png)
  — 512x512 PNG, an 8x upscale of the same buffer for viewing. Not a separate
  render.

## Captured descriptors — VERIFIED

All byte-level, dumped from the live driver at submit time. Raw capture:
[`evidence/descriptors/PANVK_G57_raw_capture_20260917-012821.log`](../evidence/descriptors/PANVK_G57_raw_capture_20260917-012821.log);
FBD + fragment job:
[`evidence/descriptors/PANVK_G57_fbd_fragjob_20260917-011942.log`](../evidence/descriptors/PANVK_G57_fbd_fragjob_20260917-011942.log).

| Descriptor | Size | Note |
|---|---|---|
| MALLOC_VERTEX job | **384 B** | header type 11 confirmed |
| Framebuffer Descriptor (FBD) | **192 B** | `tagged_ptr = base \| 1`, stride 192 |
| Fragment Job | **64 B** | header type 9 |
| Tiler Context | **192 B** | |
| Tiler Heap | **32 B** | |
| Depth/stencil | **32 B** | |
| Blend | **16 B** | 1 RT |
| VS shader binary | **256 B** | native Mali ISA |
| FS shader binary | **128 B** | native Mali ISA |
| VS FAU | 4 words / **32 B** | at `...3d0` |
| FS FAU | 3 words / **24 B** | at `...3f0` |

The FBD's `tagged_ptr` low bit being set (`0x7ca0971001` for base
`0x7ca0971000`) is the FBD-type tag, not a misaligned pointer.

## What was fixed to get here

Ordered by how much they mattered.

1. **FAU count on the Shader Environment** — the causal defect for this
   workload. Full analysis: [fau-root-cause.md](fau-root-cause.md).
   Patch [`0020`](../patches/0020-panvk-v9-fau-count-from-shader-metadata.patch).
2. **`MALLOC_VERTEX_JOB` encoder** — written for v9; Bifrost's split
   compute+tiler encoding does not apply.
3. **Blend descriptor allocated and filled earlier**, before
   `cmd_prepare_draw_sysvals` / push-uniform setup. Previously the FAU could
   capture a stale `blend_descs[]` value because sysvals were read before
   `blend_emit_descs()` ran.
4. **`Draw.flags_0` early-ZS / forward-pixel-kill state.** On Bifrost these live
   in `RENDERER_STATE.properties`; on Valhall they moved into DCD Flags 0.
   All-zero is **not** neutral — the v9 Pixel Kill enum is
   `Force Early = 0`, `Weak Early = 2`, `Force Late = 3`, so a zeroed
   `flags_0` requests `FORCE_EARLY` on both `pixel_kill_operation` and
   `zs_update_operation`. That is an aggressive kill policy, not a default.
   Now driven by `pan_earlyzs_get()`, mirroring `build_dcd_flags()` in
   `csf/panvk_vX_cmd_draw.c`.
5. **Depth/stencil, viewport, vertex-array and primitive translation** for v9.
   Three `DEPTH_STENCIL` fields (`depth_cull_enable`, `depth_clamp_mode`,
   `depth_source`) have no explicit counterpart in the Bifrost reference and are
   deliberately left at their genxml defaults rather than guessed.
6. **Provoking-vertex mode** committed to `FIRST` (the Vulkan default) once the
   first FBDs/TDs are emitted.

Patches: [`0010`](../patches/0010-panvk-v9-jm-graphics-draw-path.patch) (full
draw path), [`0014`](../patches/0014-panvk-v9-arch-enablement-common.patch)
(arch enablement), [`0013`](../patches/0013-panfrost-lib-kbase-and-fb-fixes.patch)
(`pan_fb` / `pan_desc` / kbase).

## Open on the graphics side

* **IN PROGRESS** — resource-table audit, see
  [resource-table-findings.md](resource-table-findings.md).
* **NOT IMPLEMENTED** — WSI / swapchain / present.
* **NOT IMPLEMENTED** — MSAA, multiple render targets, depth/stencil *testing*
  as a validated path (fields are emitted, not validated by a test).
* **NOT IMPLEMENTED** — indexed and indirect draws as validated paths.
* **UNTESTED** — anything beyond a single triangle into a single linear RT.
