# Known limitations

Read this before quoting anything from this repo.

## What the triangle result does and does not prove

**Does prove:** a Vulkan draw call, submitted through PanVK on Mali-G57 MC2
(v9/JM), causes the GPU to rasterize a partial triangle into an offscreen
`VkImage` in device memory, and the CPU reads back the expected pixels —
512/4096 non-black, centre `(32,32) = 255,0,0,255`, corner `(0,0) = 0,0,0,255`.

**Does NOT prove:**

* **WSI / presentation.** No swapchain, no surface, no `vkQueuePresentKHR`.
  Nothing has ever been displayed on the screen by this driver. The 512x512 PNG
  in `evidence/framebuffer/` is an upscale of a CPU-read buffer, not a
  screenshot.
* **General application support.** One hand-written test with one triangle, one
  linear render target, **zero application descriptor sets**, no textures, no
  depth testing. Runtime measurement shows `used_set_mask = 0x0`, so the
  descriptor-set binding path is not exercised at all by this test — see
  [resource-table-findings.md](resource-table-findings.md).
* **Vulkan conformance.** No CTS run of any kind.
* **Game or emulator compatibility.** Not attempted.
* **Performance.** Never measured.
* **Stability.** No stress, no long-run, no multi-frame, no multi-threaded test.

## NOT IMPLEMENTED

| Area | Note |
|---|---|
| WSI / swapchain / present | no code path |
| Application descriptor sets | **VERIFIED-HW** for uniform buffers across 1, 2 and 4 sets plus non-contiguous sets 0 and 3. Other descriptor types untested — see [phase4-open-questions.md](phase4-open-questions.md) §3.2 |
| `vkCmdDispatchIndirect` (v9) | **VERIFIED-HW**. Was faulting the GPU before `dispatch_precomp` was ported to v9 |
| Indexed draws | **VERIFIED-HW** including `firstIndex` and `vertexOffset`. `firstIndex` was silently ignored until fixed |
| Indirect draws | **VERIFIED-HW** for `vkCmdDrawIndirect` and `vkCmdDrawIndexedIndirect`, software-emulated. `firstInstance` still ignored; varying/vertex-buffer workloads out of scope |
| MSAA | `multisample_enable` emitted false; untested |
| Multiple render targets | **VERIFIED-HW** for 2 attachments, square and circle, against a CPU reference rasterizer, plus byte-exact attachment independence over the whole allocation. 3–8 attachments and mixed formats untested |
| Depth/stencil testing | descriptors emitted, never validated by a test |
| Tiled / AFBC image layouts | linear only |
| Queries / occlusion | `Occlusion query: Disabled` |
| Secondary command buffers | untested |
| Multi-queue / multi-submit sync | untested |

## Deliberately left at defaults, not guessed

Three `DEPTH_STENCIL` fields have no explicit counterpart in the Bifrost code
used as reference and are left at their genxml defaults rather than invented:

* `depth_cull_enable` (default `true`)
* `depth_clamp_mode` (default `[0,1]`)
* `depth_source` (default `Fixed function`)

If depth behaviour turns out wrong on v9, these are the first place to look. They
are **UNVERIFIED**, not correct-by-construction.

## Self-audit for false positives

Every Phase 4 sub-phase was re-examined afterwards specifically looking for
results that could be right by accident. Two genuine misreadings were found and
corrected during the work, and several gaps remain open. Read
[phase4-open-questions.md](phase4-open-questions.md) before quoting any Phase 4
result.

## Numbers that must not be hardcoded

| Value | Current | Why it varies |
|---|---|---|
| VS FAU count | 4 | property of the compiled shader |
| FS FAU count | 3 | property of the compiled shader |
| `res_count` | 4 | `ALIGN_POT(1 + first_unused_set, 4)` |
| resource table size | 64 B/stage | `res_count` x 16 B |
| EXEC_VA `va_pages` | 1024 (4 MB) | sized for current shaders only |

Each is correct for the current workload and wrong in general. See
[fau-root-cause.md](fau-root-cause.md) and
[resource-table-findings.md](resource-table-findings.md).

## Unresolved questions

* Why a `WRITE_VALUE` atom whose write is visibly present in memory reports
  `event_code 0x4` (`TERMINATED`) rather than `0x01` (`DONE`).
* Why `JOB_SUBMIT` requires `stride = 64` when `sizeof(base_jd_atom_v2) == 56`.
* The meaning of reserved resource-table indices 61 and 62.
* Whether `RESOURCE`'s `align="64"` is a hardware requirement or genxml
  conservatism.
* The practical shader-size ceiling of the 4 MB EXEC_VA zone.
* Whether the fragment-stage FAU count is required for shaders that actually
  consume fragment uniforms — the current test's FS writes a constant, so the
  question is untested. The fix programs it regardless.

## Debug output caveats

The driver still emits a stale conclusion in its heap dump:

```
mincore=absent -- UNREADABLE (I/O error) => pages not committed, tiler wrote nothing
```

The `=> pages not committed, tiler wrote nothing` half is **wrong** and appears
in runs that render correctly. See
[historical-superseded.md](historical-superseded.md#1-tiler-heap-is-unreadable-therefore-the-tiler-did-no-work).

Debug prints and comments in the patches are partly in Indonesian. They are
preserved verbatim rather than rewritten, so the patches match what was actually
built and tested.

## Repo scope

This repo is an evidence record, not a distribution. The patches are extracted
from a working tree against upstream Mesa commit
`6598829019c0746aa8e473b4ae1c980cbfa6ea4b`; they are not upstream-submission
quality and are not rebased. See [reproduction.md](reproduction.md).
