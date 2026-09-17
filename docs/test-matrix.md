# Test matrix

Every row is a test that exists in this repo. Result column is what the hardware
actually produced, with the artifact that shows it.

## Compute — through PanVK

| Test | What it checks | Result | Evidence |
|---|---|---|---|
| [`tests/compute/panvk_compute_test_g57.c`](../tests/compute/panvk_compute_test_g57.c) | SPIR-V compute, `data[0] = 777u`, readback | **VERIFIED** `0x309` = 777, 3x identical | [v9-compute-dispatch-validated.md](v9-compute-dispatch-validated.md) |
| [`tests/compute/panvk_spd_readback_test.c`](../tests/compute/panvk_spd_readback_test.c) | Shader Program Descriptor readback | **VERIFIED** | [`evidence/descriptors/shader_dump.txt`](../evidence/descriptors/shader_dump.txt) |
| [`tests/compute/panvk_spd_readback_test_fixed.c`](../tests/compute/panvk_spd_readback_test_fixed.c) | SPD readback, corrected variant | **VERIFIED** | [`evidence/descriptors/shader_dump2.txt`](../evidence/descriptors/shader_dump2.txt) |
| [`tests/compute/indirect_dispatch_test.c`](../tests/compute/indirect_dispatch_test.c) | `vkCmdDispatchIndirect`, earlier harness | superseded by the one below | — |
| [`tests/compute/dispatch_indirect_test.c`](../tests/compute/dispatch_indirect_test.c) | `vkCmdDispatchIndirect` on v9 | **VERIFIED-HW** | `evidence/logs/T4.4.3_after_*` |

## Phase 4 sub-phases

| Test | What it covers | Status | Evidence |
|---|---|---|---|
| [`tests/graphics/indexed_draw_test.c`](../tests/graphics/indexed_draw_test.c) | 4.1 indexed draws, `firstIndex`, `vertexOffset` | **VERIFIED-HW** | `evidence/logs/T4.1_*` |
| [`tests/graphics/restab_descset_test.c`](../tests/graphics/restab_descset_test.c) | 4.2 resource table, 1/2/4 and sparse descriptor sets | **VERIFIED-HW** | `evidence/logs/T4.2_*` |
| [`tests/graphics/mrt_shape_test.c`](../tests/graphics/mrt_shape_test.c) | 4.3 two render targets, square and circle vs CPU reference | **VERIFIED-HW** | `evidence/logs/T4.3_*` |
| [`tests/graphics/draw_indirect_test.c`](../tests/graphics/draw_indirect_test.c) | 4.4 indirect and indexed-indirect draw | **VERIFIED-HW** | `evidence/logs/T4.4.4_*`, `T4.4.5_*` |
| [`tests/graphics/indirect_probe_test.c`](../tests/graphics/indirect_probe_test.c) | 4.4 gate probe, recorded the pre-fix behaviour | **VERIFIED-HW** | `evidence/logs/indirect_T4.4.0_*` |

## Graphics — through PanVK

| Test | What it checks | Result | Evidence |
|---|---|---|---|
| [`tests/graphics/begin_end_rendering_test.c`](../tests/graphics/begin_end_rendering_test.c) | `vkCmdBeginRendering` / `EndRendering` do not crash, clear works | **VERIFIED** | clear-only runs below |
| [`tests/graphics/triangle_draw_test.c`](../tests/graphics/triangle_draw_test.c) | first draw attempt | **HISTORICAL** | [`evidence/logs/`](../evidence/logs/) |
| [`tests/graphics/triangle_draw_test_v2.c`](../tests/graphics/triangle_draw_test_v2.c) | 64x64 offscreen draw + pixel readback | **VERIFIED** 512/4096 partial triangle | [`PANVK_G57_triangle_fresh_20260917-004051.log`](../evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log) |
| [`tests/graphics/triangle_draw_test_mex.c`](../tests/graphics/triangle_draw_test_mex.c) | same against the `mex` reference lib | **HISTORICAL** comparison harness | — |
| `..._v2.c.HISTORICAL-fullscreen-baseline` | fullscreen variant used for A/B | **HISTORICAL-SUPERSEDED** as proof; still valid as A/B | [historical-superseded.md](historical-superseded.md) |

## FAU A/B series — the causal experiment

Same build, same test, one delta. Full analysis: [fau-root-cause.md](fau-root-cause.md).

| Run | VS `fau_count` | FS `fau_count` | non-black | Verdict | Log |
|---|---|---|---|---|---|
| A | 0 | 0 | 0 / 4096 | CLEAR-ONLY | [`ab_A_run.log`](../evidence/logs/ab_A_run.log) |
| A2 | 0 | 0 | 0 / 4096 | CLEAR-ONLY (repeat) | [`ab_A2_run.log`](../evidence/logs/ab_A2_run.log) |
| fs_only | 0 | set | 0 / 4096 | CLEAR-ONLY | [`fs_only_run.log`](../evidence/logs/fs_only_run.log) |
| vs_only | set | 0 | 4096 / 4096 | renders | [`vs_only_run.log`](../evidence/logs/vs_only_run.log) |
| B | 4 | 3 | 4096 / 4096 | renders | [`ab_B_run.log`](../evidence/logs/ab_B_run.log) |
| B run2 | 4 | 3 | 4096 / 4096 | renders (repeat) | [`ab_B_run2.log`](../evidence/logs/ab_B_run2.log) |
| B run3 | 4 | 3 | 4096 / 4096 | renders (repeat) | [`ab_B_run3.log`](../evidence/logs/ab_B_run3.log) |
| B final | 4 | 3 | 4096 / 4096 | renders (repeat) | [`b_final_run.log`](../evidence/logs/b_final_run.log) |

Build provenance for side A: [`ab_A_sha256.txt`](../evidence/logs/ab_A_sha256.txt)
pins both the source file and the resulting `libvulkan_panfrost.so`.

Descriptor-level confirmation:
[`ab_A_pandecode.ctx-0.txt`](../evidence/descriptors/ab_A_pandecode.ctx-0.txt) (`FAU count: 0`),
[`ab_B_pandecode.ctx-0.txt`](../evidence/descriptors/ab_B_pandecode.ctx-0.txt) (`FAU count: 3` + both FAU blocks),
[`vs_only_pandecode.ctx-0.txt`](../evidence/descriptors/vs_only_pandecode.ctx-0.txt) (`FAU count: 0` yet vertex FAU block present).

## Raw JM — direct ioctl, no Mesa

| Test | What it checks | Result |
|---|---|---|
| [`tests/raw-jm/kbase_handshake.c`](../tests/raw-jm/kbase_handshake.c) | `VERSION_CHECK` + `SET_FLAGS` | **VERIFIED** UAPI 11.46 |
| [`tests/raw-jm/test_kbase.c`](../tests/raw-jm/test_kbase.c) | ioctl chain, mmap, coherency | **VERIFIED** |
| [`tests/raw-jm/test_kbase2.c`](../tests/raw-jm/test_kbase2.c) | extended ioctl surface | **VERIFIED** |
| [`tests/test_kbase3.c`](../tests/raw-jm/test_kbase3.c) | `GET_GPUPROPS`, 83 props, no-context path | **VERIFIED** 749 B |
| [`tests/raw-jm/kbase_gpuprops_test.c`](../tests/raw-jm/kbase_gpuprops_test.c) | gpuprops decode | **VERIFIED** |
| [`tests/raw-jm/kbase_submit_test.c`](../tests/raw-jm/kbase_submit_test.c) | atom submission | **VERIFIED** |
| [`tests/raw-jm/kbase_write_value_core_req_test.c`](../tests/raw-jm/kbase_write_value_core_req_test.c) | `WRITE_VALUE` atom, stride 64 | **VERIFIED** `event=0x4`, `target=0x2a2a2a2a` — [log](../evidence/logs/raw_jm_write_value_20260917-004008.log) |
| [`tests/raw-jm/kbase_write_value_core_req_test_stride56.c`](../tests/raw-jm/kbase_write_value_core_req_test_stride56.c) | stride 56 | **HISTORICAL-SUPERSEDED** |
| [`tests/raw-jm/verify_atom_size.c`](../tests/raw-jm/verify_atom_size.c) | `sizeof(base_jd_atom_v2)` | **VERIFIED** 56 |
| [`tests/raw-jm/verify_atom_size_v2.c`](../tests/raw-jm/verify_atom_size_v2.c) | same, refined | **VERIFIED** 56 |
| [`tests/raw-jm/kbase_fau_self.c`](../tests/raw-jm/kbase_fau_self.c) | FAU behaviour outside Mesa | **IN PROGRESS** |

## Shaders used

[`tests/shaders/`](../tests/shaders/) — `triangle.vert` / `triangle.frag`
(+ `.spv`), `write_value.comp`, `write_id.comp`,
`triangle_debug_ssbo.vert` (SSBO-instrumented VS for reading back computed
positions), plus the `HISTORICAL-fullscreen-baseline` variants.

## Not covered by any test

MSAA, multiple render targets, depth/stencil *testing*, indexed draws, indirect
draws, tiled/AFBC image layouts, WSI/present, multi-descriptor-set resource
tables, multi-submit synchronization, and anything resembling a real application.
See [known-limitations.md](known-limitations.md).
