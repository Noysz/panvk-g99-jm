# Test matrix

Every row is a test that exists in this repo. Result column is what the hardware
actually produced, with the artifact that shows it.

## Compute — through PanVK

| Test | What it checks | Result | Evidence |
|---|---|---|---|
| [`tests/compute/panvk_compute_test_g57.c`](../tests/compute/panvk_compute_test_g57.c) | SPIR-V compute, `data[0] = 777u`, readback | **VERIFIED** `0x309` = 777, 3x identical | [v9-compute-dispatch-validated.md](v9-compute-dispatch-validated.md) |
| [`tests/compute/panvk_spd_readback_test.c`](../tests/compute/panvk_spd_readback_test.c) | Shader Program Descriptor readback | **VERIFIED** | [`evidence/descriptors/shader_dump.txt`](../evidence/descriptors/shader_dump.txt) |
| [`tests/compute/panvk_spd_readback_test_fixed.c`](../tests/compute/panvk_spd_readback_test_fixed.c) | SPD readback, corrected variant | **VERIFIED** | [`evidence/descriptors/shader_dump2.txt`](../evidence/descriptors/shader_dump2.txt) |
| [`tests/compute/texel_buffer_oob_test.c`](../tests/compute/texel_buffer_oob_test.c) | texel buffer reads/stores/atomics out of the view with robustBufferAccess2 (R32_UINT, RGBA8, RG32F) | **VERIFIED-HW** 3x PASS after `0071`, control `PANVK_V9_TEXEL_OOB_HW=1` FAIL | [`evidence/cts/phase13b/texel_oob_test`](../evidence/cts/phase13b/texel_oob_test) |
| [`tests/compute/indirect_dispatch_test.c`](../tests/compute/indirect_dispatch_test.c) | `vkCmdDispatchIndirect`, earlier harness | superseded by the one below | — |
| [`tests/compute/dispatch_indirect_test.c`](../tests/compute/dispatch_indirect_test.c) | `vkCmdDispatchIndirect` on v9 | **VERIFIED-HW** | `evidence/logs/T4.4.3_after_*` |

## Phase 4 sub-phases

| Test | What it covers | Status | Evidence |
|---|---|---|---|
| [`tests/graphics/indexed_draw_test.c`](../tests/graphics/indexed_draw_test.c) | 4.1 indexed draws, `firstIndex`, `vertexOffset` | **VERIFIED-HW** | `evidence/logs/T4.1_*` |
| [`tests/graphics/restab_descset_test.c`](../tests/graphics/restab_descset_test.c) | 4.2 resource table, 1/2/4 and sparse descriptor sets | **VERIFIED-HW** | `evidence/logs/T4.2_*` |
| [`tests/graphics/desc_types_test.c`](../tests/graphics/desc_types_test.c) | storage, dynamic and arrayed buffer descriptors, mixed in one set | **VERIFIED-HW** | `evidence/logs/T4.5.18_*`, `T4.5.20_*` |
| [`tests/graphics/vbo_varying_test.c`](../tests/graphics/vbo_varying_test.c) | vertex buffers, instance-rate attributes, varyings, direct and indirect, against a CPU reference rasterizer | **VERIFIED-HW** | `evidence/logs/T4.6.*` |
| [`tests/graphics/depth_test.c`](../tests/graphics/depth_test.c) | depth test, compare ops, clear value, write enable, D32F and D16, against a CPU model | **VERIFIED-HW** | `evidence/logs/T4.7.1_*`, `T4.7.3_*` |
| [`tests/graphics/msaa_test.c`](../tests/graphics/msaa_test.c) | 4x MSAA with AVERAGE resolve, sample mask, against a per-sample CPU model | **VERIFIED-HW** | `evidence/logs/T4.7.4_*`, `T4.7.6_*` |
| [`tests/graphics/fence_test.c`](../tests/graphics/fence_test.c) | `vkWaitForFences` after real, empty and no-command-buffer submits | **VERIFIED-HW** after `0041` | `evidence/logs/T4.8.1_*` |
| [`tests/graphics/copy_test.c`](../tests/graphics/copy_test.c) | buffer/image copies, linear and OPTIMAL, round trip | **VERIFIED-HW** after `0042` | `evidence/logs/T4.8.2_*` |
| [`tests/graphics/cube_test.c`](../tests/graphics/cube_test.c) | spinning cube, push-constant MVP, depth, per-frame CPU rasterizer check | **VERIFIED-HW**; `CUBE_RESUBMIT=1` (record once, submit every frame): 3x PASS after `0072`, control `PANVK_V9_NO_REISSUE_VA_RESET=1` FAIL | `evidence/logs/T4.9.1_*`, `T4.9.2_*`, [`evidence/cts/phase13b/resubmit_test`](../evidence/cts/phase13b/resubmit_test) |
| [`tests/phase7/vk_map32_test.c`](../tests/phase7/vk_map32_test.c) | 32-bit Windows program (wine wow64): host-visible memory mapped below 4 GB, written and read back | **VERIFIED-HW** PASS on 0.0.9, 0.0.10, 0.1.0-test3 through the Winlator Wrapper; `WRAPPER_DISABLE_PLACED=1` FAIL | [`evidence/cts/phase13b/wow64_map32`](../evidence/cts/phase13b/wow64_map32) |
| [`tests/graphics/fault_frag_test.c`](../tests/graphics/fault_frag_test.c) | fragment job reading a texture whose memory is freed right after submit (invalid usage on purpose): fragment side of `PANVK_FAULT_REPORT` and the `0077` audit | **VERIFIED-HW** valid order ALL_CORRECT; use-after-free WRONG, event 0x4 on the fragment chain; with `PANVK_DBG_FREE_DELAY_MS=3000` ALL_CORRECT; audit 3x: texture plane is the only suspect | [`evidence/cts/phase13c/fault_diag`](../evidence/cts/phase13c/fault_diag) |
| [`tests/graphics/longjob_test.c`](../tests/graphics/longjob_test.c) | fragment and vertex jobs of 1-4 s, stopped and resumed by kbase, every pixel checked against a CPU model | **VERIFIED-HW** frag 0.9 s and 3.7 s, vert 1.5 s: 3x PASS each | [`evidence/cts/phase13c/longjob`](../evidence/cts/phase13c/longjob) |
| `PANVK_FAULT_AUDIT_ALL=1` on own tests and 11 CTS lists (`0077`) | false alarms of the pointer audit on valid work | **VERIFIED-HW** 0 suspects, CTS results identical to the regression | [`evidence/cts/phase13c/audit_all`](../evidence/cts/phase13c/audit_all) |
| [`tests/graphics/fault_diag_test.c`](../tests/graphics/fault_diag_test.c) | compute dispatch reading a buffer that is freed right after submit (invalid usage on purpose): `PANVK_FAULT_REPORT`, `PANVK_DBG_FREE_DELAY_MS` (`0076`) | **VERIFIED-HW** use-after-free 3x WRONG with kbase event 0x4; with `PANVK_DBG_FREE_DELAY_MS=2000` 3x ALL_CORRECT; valid order ALL_CORRECT | [`evidence/cts/phase13c/fault_diag`](../evidence/cts/phase13c/fault_diag) |
| CTS robustness2 pair: an r32f graphics case, then `r32i...sampled_image...null_descriptor.samples_1.1d.vert` (`0075`) | null image view in a combined image sampler | **VERIFIED-HW** 3x Pass/Pass; control `PANVK_NULL_TEX_SAMPLER_SLOT=1` Pass/Fail; `l_rb2` 951 P / 28 F (was 926 / 53) | [`evidence/cts/phase13c/nulltex`](../evidence/cts/phase13c/nulltex) |
| [`tests/graphics/tls_batch_test.c`](../tests/graphics/tls_batch_test.c) | scratch (TLS) memory of one command buffer with 200 scratch-using dispatches, data check | **VERIFIED-HW** 3x PASS after `0074` (+1.6 MB), control `PANVK_TLS_PER_BATCH=1` +300 MB FAIL | [`evidence/cts/phase13b/tls_batch_test`](../evidence/cts/phase13b/tls_batch_test) |
| [`tests/graphics/pool_cache_test.c`](../tests/graphics/pool_cache_test.c) | command pool BO cache after `vkResetCommandPool` on 8 pools, 2 rounds, counter check | see [`evidence/cts/phase13b/pool_cache_test`](../evidence/cts/phase13b/pool_cache_test) | patch `0073` |
| [`tests/graphics/lazy_mem_test.c`](../tests/graphics/lazy_mem_test.c) | lazy device-local memory: backed size follows GPU use, data correct | **VERIFIED-HW** 3x PASS (`0070`), control `PANVK_KBASE_DEVMEM_EAGER=1` | [`evidence/cts/phase12/lazy_mem_test`](../evidence/cts/phase12/lazy_mem_test) |
| [`tests/graphics/wsi_cube.c`](../tests/graphics/wsi_cube.c) | same cube through an X11 swapchain, window read back and checked | **VERIFIED-HW** | `evidence/logs/T4.9.3_*` |
| [`tests/graphics/tex_test.c`](../tests/graphics/tex_test.c) | texture sampling, filters and address modes, OPTIMAL and LINEAR | **VERIFIED-HW** | `evidence/logs/T5.1_*` |
| [`tests/graphics/stencil_test.c`](../tests/graphics/stencil_test.c) | stencil ops, compare, masks, depth-fail, D24S8/D32S8/S8, read back through 256 EQUAL probes | **VERIFIED-HW** | `evidence/logs/T5.2_*` |
| [`tests/graphics/blend_test.c`](../tests/graphics/blend_test.c) | 13 blend states vs CPU model | **VERIFIED-HW** | `evidence/logs/T5.3_*` |
| [`tests/graphics/wsi_cube.c`](../tests/graphics/wsi_cube.c) `CUBE_RESIZE_EVERY` | resize + swapchain recreate, 30,000-frame runs, RSS | **VERIFIED-HW** | `evidence/logs/T6.1_*`, `T6.2_*` |
| [`tests/phase7/featq.c`](../tests/phase7/featq.c) | 1.1/1.2/1.3 and extension features for the DXVK gap | reported values | [`evidence/phase7/`](../evidence/phase7/) |
| [`tests/cts/percase.sh`](../tests/cts/percase.sh) + CTS `indirect_draw` | 🧪 multi-draw (`0044`) | 372/372 3x, controls 134/138 fail | [`evidence/cts/indirect_draw/`](../evidence/cts/indirect_draw/) |
| CTS robustness2 null-descriptor subset | 🧪 `nullDescriptor` (`0045`) | 16 pass 3x, poison control 8 fail | [`evidence/cts/nulldesc/`](../evidence/cts/nulldesc/) |
| CTS sampler min/max subset | 🧪 min/max emulation (`0046`) | 132/132 3x, control 72 fail; cube 12/16 FAIL | [`evidence/cts/minmax/`](../evidence/cts/minmax/) |
| [`tests/cts/percase.sh`](../tests/cts/percase.sh) + CTS `indirect_draw` | 🧪 multi-draw (`0044`) | 372/372 3x, controls 134/138 fail | [`evidence/cts/indirect_draw/`](../evidence/cts/indirect_draw/) |
| CTS robustness2 null-descriptor subset | 🧪 `nullDescriptor` (`0045`) | 16 pass 3x, poison control 8 fail | [`evidence/cts/nulldesc/`](../evidence/cts/nulldesc/) |
| CTS sampler min/max subset | 🧪 min/max emulation (`0046`) | 132/132 3x, control 72 fail; cube 12/16 FAIL | [`evidence/cts/minmax/`](../evidence/cts/minmax/) |
| [`tests/cts/run_cts.py`](../tests/cts/run_cts.py) | VK-GL-CTS groups `api.smoke`, `simple_draw`, `indirect_draw` | 8/8, 4/4, 86 pass 0 fail | [`evidence/cts/`](../evidence/cts/) |
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

Mipmap selection in this project's own tests (CTS mipmap cases pass), secondary command
buffers, multi-layer rendering, multi-queue, and anything resembling a real
application. (MSAA, MRT, depth, stencil, blending, indexed and indirect draws,
AFBC, WSI and multi-descriptor-set tables are covered above.)
See [known-limitations.md](known-limitations.md).
