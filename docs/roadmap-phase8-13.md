# Roadmap after Phase 7 (2026-10-04)

Order agreed with the project owner: finish the open Phase 4-6 items, then
everything DXVK 1.10.3 through 2.x needs, then memory, then CPU/GPU cost.
Same rules as every earlier phase: a phase is done only with a falsifiable
test, 3 runs and a negative control. Every v9-only emulation is
🧪 EXPERIMENTAL.

## Where DXVK stands today (VERIFIED-HW, `dxvk_probe` through the Winlator Wrapper)

| DXVK | D3D11 | D3D9 | missing on this driver |
|---|---|---|---|
| 1.10.3 | ❌ | ❌ | `geometryShader`, `transformFeedback`, `geometryStreams`, `tessellationShader`, `variableMultisampleRate`, `vertexPipelineStoresAndAtomics` |
| 1.11.1-sarek | ❌ | ❌ | `geometryShader`, `tessellationShader`, `variableMultisampleRate`, `vertexPipelineStoresAndAtomics` |
| 1.12.1-sarek | ✅ FL 11_1 | ✅ | runs degraded without `geometryShader`, `tessellationShader`, `transformFeedback`, `depthBounds` |
| 2.3.1 | ❌ (Winlator build still starts at FL 10_1) | | `geometryShader`, `transformFeedback`, `geometryStreams` |

Update after `0060` + `0062` (drv11, 2026-10-05): `transformFeedback` and
`tessellationShader` are no longer missing.

| DXVK | missing now |
|---|---|
| 1.10.3 | `geometryShader`, `geometryStreams`, `variableMultisampleRate`, `vertexPipelineStoresAndAtomics` |
| 1.11.1-sarek | `geometryShader`, `variableMultisampleRate`, `vertexPipelineStoresAndAtomics` |
| 2.3.1 | `geometryShader`, `geometryStreams` (d3d11_tri passes at FL 10_1) |

Update after `0065` (drv15, 2026-10-06): `geometryStreams` is exposed, every
`dxvk_probe` profile is OK (1.10.3 and 1.11.1 FL 11_0, 1.12.1 and 2.3.1
FL 11_1).

Update after `0063` (drv12/drv13, 2026-10-05): `geometryShader` is no longer
missing. DXVK 1.10.3 D3D11 and D3D9 start (d3d11_tri FL 11_0), 1.11.1-sarek
FL 11_0, 2.3.1 FL 11_0 and only `geometryStreams` is missing for it.

Update after `0071` (drv23, 2026-10-07): builds from the Winlator components
list, d3d11_tri with no env var. DXVK 3.1.1, gplasync 2.7.1-1 and 3.1.1-1
need `robustBufferAccess2` (they refused the device before `0071`) and now
reach FL 11_1; sarek-async 1.10.9 / 1.11.0, gplasync 2.3.1-1, 2.3.1, 1.10.3
and 1.7.2 FL 11_1 as before. AIO Showcase, GS Exploder, Tessellation, D3D9
and D3D10 run on 3.1.1 and gplasync 2.7.1. `PANVK_V9_NO_RBA2=1`: the three
refuse the device again (`evidence/cts/phase13b/drv23`).

Clip/cull distance, `multiViewport` and BC textures are supplied by the
Winlator Wrapper, so they are not on this list, but they are also missing in
the driver itself.

## DXVK 1.x requirements (VERIFIED-SRC, `d3d11_device.cpp` / `d3d9_device.cpp` GetDeviceFeatures, tags v1.0-v1.10.3)

| Required by | DXVK 1.0-1.7 | DXVK 1.8-1.10.3 | Phase |
|---|---|---|---|
| `geometryShader`, every feature level, D3D11 and D3D9 (1.5+) | yes | yes | 11a |
| `transformFeedback` + `geometryStreams`, FL >= 10_0 | optional | yes | 11b |
| `tessellationShader`, FL >= 11_0 | yes | yes | 10 |
| `variableMultisampleRate`, `vertexPipelineStoresAndAtomics`, FL 11_1 | yes | yes | 9 |

So no DXVK 1.x version starts at all without geometry shaders. Order is
therefore 11a first, then 11b, 10 and 9:

- after 11a: DXVK 1.0-1.7 D3D9 and D3D11 FL 10_x
- after 11b: DXVK 1.8-1.10.3 D3D9 and D3D11 FL 10_x
- after 10: FL 11_0 (most D3D11 games)
- after 9: FL 11_1, and DXVK-Sarek 1.11 complete (it needs GS, tessellation,
  `variableMultisampleRate`, `vertexPipelineStoresAndAtomics`)

## Phase 10/11 plan (VERIFIED-SRC, Mesa tree at `6598829`)

What already exists in the tree:
- PanVK CSF (v10+) has tessellation and transform feedback, both done in
  software through libpoly: VS/TCS run as compute, a tessellator kernel,
  then an indirect draw with TES as the hardware VS
  (`csf/panvk_vX_cmd_draw.c`: `launch_tess`, `launch_xfb`, `launch_gfx_cs`).
  Features are gated `PAN_ARCH >= 10`.
- `geometryShader` and `geometryStreams` are false on every Mali arch.
  The only GS reference is Asahi/Honeykrisp (`hk_cmd_draw.c`), on top of
  `src/poly/nir/poly_nir_lower_gs.c`.
- libpan for v9 already contains the libpoly tessellator kernels
  (`PANLIB_TESS_*`, `PANLIB_PREFIX_SUM_TESS`). There are no geometry kernels
  in libpan yet.
- JM can already queue precompiled compute jobs in the current batch
  (`jm/panvk_vX_cmd_precomp.c`), without splitting the render pass.

GS needs the same base as tessellation (graphics shaders run as compute
inside the render pass, poly heap, GPU-generated indirect draw). That base
has a working reference only for tessellation, so the build order is:

1. [x] JM gfx-compute helper: a graphics-stage shader variant as a COMPUTE
   job in the batch's vertex/tiler chain. First user: transform feedback
   (patch `0060`). CTS `transform_feedback.simple.*` 7899 cases: 187 pass,
   7712 not supported (geometry shader, streams, clip/cull distance,
   `transformFeedbackDraw`), 0 fail. Subset of 143 3x: 82 pass, 0 fail.
2. [x] Poly heap on JM, allocated lazily (patch `0062`): 128 MiB of VA,
   `ALLOC_ON_FAULT`, so physical pages only on use (RAM budget, see Phase 12).
3. [x] Tessellation on v9 (Phase 10, patches `0061` + `0062`). CTS
   `tessellation.*` 1114 3x: 148 pass, 16 fail (all `*_indirect`, indirect
   tess draws not implemented), 950 not supported (mostly `multiViewport`,
   vertex-pipeline stores, geometry shader). AIO "Tessellation" renders.
4. [x] Geometry shader (Phase 11a, patch `0063`): `poly_nir_lower_gs` in the
   PanVK compile, port of the Honeykrisp GS sequence for direct draws. CTS
   `geometry.*` 200 4x: 179 pass, 11 fail, 10 not supported. AIO "GS
   Exploder" renders.
4b. [x] Phase 11b (patches `0064`, `0065`): `geometryStreams` and XFB from
   a GS (GS XFB compute variant, serial `panlib_prefix_sum_geom`), GS after
   tessellation (TES SW variant), indirect draws with tessellation or a GS
   (`panlib_tess_setup_indirect`, `panlib_gs_setup_indirect`), XFB of
   adjacency topologies, TES point size. CTS `tessellation.*` 1114 3x:
   206 pass, 0 fail; XFB streams 31 pass, 0 fail; `geometry.*` unchanged.
   DXVK 2.3.1 passes `dxvk_probe` at FL 11_1.
   Next: XFB from a TES without a GS (8 `winding_patch_list` fails).
4c. [x] `0.0.9` (patch `0067`): cube layered rendering (preload of cube
   faces), `gl_PrimitiveID` to the FS, GS primitive ID after tessellation,
   primitive restart before a GS (`panlib_unroll_restart`), pipeline
   statistics queries counted by the driver (`panlib_stats_add`), and indirect
   draws tracked as tiler jobs (they were lost when a batch closed inside a
   render pass). CTS `geometry.*` 3x: 192 pass, 0 fail.
5. Expose the features, DXVK 1.10.3 / 1.11 / 2.3.1 through the Wrapper.

## Phase 8 — close the open Phase 4-6 items

- [x] 8.1 Occlusion queries: no-colour-attachment crash, precise count with
  `discard` (patch `0058`). CTS `query_pool.occlusion_query` 434/441, 7 NS, 3x.
- [x] 8.2 Secondary command buffers / batch split inside a render pass
  (patch `0058`). CTS `api.command_buffers` secondary subset 17/19, 2 NS, 3x.
- [x] 8.3 Input assembly / primitive restart: CTS 111 pass, 87 NS, 0 fail.
- [x] 8.4 Phase 5 leftovers (patch `0059`). CTS 3x, 0 fail:
  anisotropy 64/128 (64 NS), dual-source blend 167/202 (35 NS), blend
  formats 133/161 (28 NS), logic op 176/224 (48 NS), MSAA 356/708 (352 NS),
  multisample shader builtins 52/95 (43 NS), multisample interpolation
  72/124 (52 NS), depth/stencil resolve 4x/8x 619/941 (322 NS). Three bugs
  fixed: per-sample shading, alpha-to-coverage, shader depth source.
- [ ] 8.5 Phase 6 leftover: Android native surface outside Winlator.
- [ ] 8.6 Memory growth seen in games (RAM 60% -> 96% then the app is
  killed). Measure first (per-pool / per-BO accounting), fix in Phase 12.
  Measured and first fixes in `0070` (12.1-12.3); still open until a game
  that was killed (Little Nightmares, RE3) is confirmed on the new build.

- [x] 8.7 Command buffers submitted more than once (`0072`): only the first
  submit drew, every later one faulted (kbase `0x58`) because the MALLOC_VERTEX
  job keeps the vertex packet pointer the hardware wrote into it. Native
  Vulkan apps that record once and submit every frame (vkcube, the
  AIO-Graphics-Test Vulkan cube) showed nothing. `cube_test` `CUBE_RESUBMIT=1`
  3x PASS, control FAIL.

## Phase 9 — small features DXVK asks for

- [ ] 9.1 `depthBounds`: moved out of `0.0.8`, planned as an opt-in
  emulation (env var, off by default). The v9 genxml has no depth-bounds
  field. The emulation reads the stored depth from the tile buffer in the
  fragment shader and discards outside the bounds, which forces late
  depth/stencil for the draws that run it. Because the test enable is dynamic
  state, draws with the test enabled get a separate FS variant so the other
  draws keep early-ZS. Only DXVK-Sarek 1.12 asks for it, as an optional
  feature it runs without (D3D9 `NVDB` depth-bounds hack).
- [x] 9.2 `variableMultisampleRate` (`0066`): without attachments the frame
  takes its sample count from the first draw; a draw with another
  `rasterizationSamples` starts a new batch. CTS
  `pipeline.monolithic.multisample.variable_rate|mixed_count` 96 pass, 0 fail
  (3x; 954 not supported: sample-count combinations the driver does not
  report). Control `PANVK_V9_NO_VMSR`.
- [x] 9.3 `vertexPipelineStoresAndAtomics` (`0066`): IDVS shades vertex IDs
  in groups of 4 and runs the position and varying shaders separately, so
  memory writes went to the position shader only and are gated on the vertex
  being real (direct draws: vertex index < vertex count; tessellation: the
  padding points the tessellator now writes are skipped). GS memory writes
  stay in the MAIN compute pass only. CTS `glsl.atomic_operations` vertex /
  tess / geometry 264 pass, 0 fail; vertex-store tessellation list 314 pass,
  0 fail, 1 timeout (`tess_factor_barrier_bug`, 524288 instances) (3x).
  Controls `PANVK_V9_NO_VPS`, `PANVK_V9_NO_SFX_FILTER`,
  `PAN_V9_IDVS_STORES_BOTH`, `PANVK_V9_GS_SFX_ALL`.
- [x] 9.4 `shaderClipDistance` / `shaderCullDistance` in the driver
  (`0066`): Mali has no clip-distance hardware. The fragment shader discards
  where an enabled interpolated clip distance is negative; cull distances
  travel as `d < 0 ? 1 : 0` masks and a primitive is dropped where the mask is
  1 with zero derivatives (the idea of asahi's `agx_nir_lower_cull_distance`).
  CTS `clipping.*` + transform-feedback clip/cull 584 pass, 6 fail (3x; the 6
  are `depth_clip_control_tese`, stream output straight from a TES). Control
  `PANVK_V9_NO_CLIP_CULL`.
- [x] 9.5 `multiViewport` (`0066`): 16 viewports. Mali has one viewport per
  draw, so a draw whose last pre-rasterization stage writes
  `gl_ViewportIndex` is recorded once per viewport and the other viewports'
  vertices are moved out of the way. CTS 223-case list 138 pass, 0 fail (3x);
  tessellation cases gated on `multiViewport` 248 pass, 0 fail (the other 320
  need `shaderFloat64`). Control `PANVK_V9_NO_MULTIVIEWPORT`.
- [x] Found on the way (`0066`): the v9 primitive descriptor kept the depth
  cull bits on, so `depthClamp` / `depthClipEnable = false` still dropped
  primitives outside [0, 1] (CTS `draw.renderpass.depth_clamp.*`, 30 cases).
  Control `PANVK_V9_DEPTH_CULL_ALWAYS`.

- [x] 9.6 `robustBufferAccess2` on by default (`0071`): texel buffer reads out
  of range return 0 and alpha 1 / 0 by format, as Vulkan requires. DXVK 2.4+
  and 3.x refuse a device without it; with `0071` DXVK 3.1.1, gplasync
  2.7.1 and 3.1.1 create FL 11_1 with no env var. Cost: AIO Tessellation
  -5 % on DXVK 2.3.1 (which turns the feature on when it is there), Showcase
  and Draw 1024 unchanged; `PANVK_V9_NO_RBA2=1` hides it. Still open:
  out-of-range vertex attribute fetch (28 CTS cases) and null-descriptor
  sampled images in a vertex shader (25).

## Phase 10 — tessellation on v9

Upstream panvk exposes `tessellationShader` on v10+ using the `poly`
lowering (TCS as compute, tessellator, TES as hardware vertex shader). Port
to the v9 JM path: compute jobs for TCS and the tessellator, then a
MALLOC_VERTEX draw of the tessellated output.
Target: CTS `tessellation` subset + AIO "Tessellation" scene renders.
Unlocks: DXVK-Sarek 1.11 (with Phase 9 items).

## Phase 11 — geometry shaders and transform feedback on v9

`poly_nir_lower_gs` (used by asahi) turns GS into compute + an index
buffer. Port to v9 JM, then `transformFeedback` (+ `geometryStreams`) on top
of the same compute path.
Target: CTS `geometry` + `transform_feedback` subsets, AIO "GS Exploder".
Unlocks: DXVK 1.10.3, full DXVK 2.x feature level 11_x.

## Phase 12 — memory

Allocation accounting, pool sizing and reuse (desc/varying/tls pools, tiler
heap), BO cache, freeing per-batch memory on cmdbuf reset, memory budget
reporting (`VK_EXT_memory_budget`). Target: flat RSS over a long DXVK run
and over a real game session.

- [x] 12.1 Accounting (`0070`): `PANVK_MEM_PROF=1|<file>` prints kbase BOs
  (VA, committed, growable, lazy backed), `VkDeviceMemory`, command pool
  cache, tiler/poly heaps, RSS and MemAvailable every 2 s. Result: no driver
  leak; RAM grows because kbase backed whole DXVK chunks at allocation.
- [x] 12.2 Lazy device-local memory (`0070`): device-local, not
  host-visible, not exported allocations >= 8 MB get kbase commit 0 +
  `BASE_MEM_GROW_ON_GPF`. AIO backed device memory 404 -> 138 MB, RSS max
  ~600 -> ~330-360 MB, same FPS. Control `PANVK_KBASE_DEVMEM_EAGER=1`.
  Host-visible memory cannot be lazy (a CPU access to a page without backing
  is a crash), so DXVK's host-visible chunks stay committed.
- [x] 12.3 Heap size (`0070`): RAM / 3 clamped to [2 GiB, 75 %], 2.50 GiB on
  the G99 instead of 5.63 GiB, so DXVK frees empty chunks and games pick
  smaller texture budgets. Override `PANVK_KBASE_HEAP_PERCENT=N`.
- [x] 12.4 Command pool BO cache (`0073`): it kept the peak of every pool
  until the pool was destroyed (2 -> 98 MB over a 10-minute AIO session in
  one D3D11 device). Now capped at 48 MB per device
  (`PANVK_POOL_CACHE_MAX_MB`, `0` = no cap); `pool_cache_test` 79 -> 48 MB.
- [ ] 12.5 Real game session with the profile on (30 min), Little
  Nightmares / RE3 no longer killed.
- [x] 12.6 Scratch per command buffer (`0074`): every JM batch with a
  scratch-using shader had its own 1-3 MB TLS buffer; NFS Most Wanted (DX9)
  held 2.1 GB after 5 s and was killed at the race start. One TLS/WLS buffer
  per command buffer now; `tls_batch_test` +300 MB -> +1.6 MB.

## Phase 13 — CPU and GPU cost

CPU: per-draw descriptor emission, the one-job-per-draw MALLOC_VERTEX path,
synchronous submit-and-wait per batch in the kbase queue. GPU: early-ZS /
FPK, tile size, AFBC, needless preloads. Target: frame-time traces before and
after each change, same scene, 3 runs.

- [x] 13.1 Asynchronous kbase JM submission (`0068`): one `JOB_SUBMIT` per
  `vkQueueSubmit`, ORDER `pre_dep` chain instead of a CPU wait per job chain,
  fences/semaphores pending on the submission seqno, X11 present fence wait
  in the present thread. AIO Showcase +39%, Draw 1024 +88% (3 rounds), GPU
  busy 95-98%. Control `PANVK_KBASE_SYNC_SUBMIT=1`. Released in `0.0.10`
  with `0069` (switch values `0`/`false`/`off`/`no` = off). CTS regression
  30 groups, 7127 cases, lost 0.
- [ ] 13.2 Showcase is GPU bound now: fragment/shader cost (early-ZS, FPK,
  preloads) is next. Vertex/tiler of batch N+1 still waits for the fragment
  job of batch N (shared tiler heap); overlapping them needs a heap per
  batch in flight.
- [x] 13.3 Stability: a null image view in a combined image sampler went into
  the sampler sub-descriptor, the texture slot kept stale data (`0075`).
  robustness2 `l_rb2` 951 P / 28 F (was 926 / 53), regression lost 0.
  Control `PANVK_NULL_TEX_SAMPLER_SLOT=1`.
- [x] 13.4 Fault diagnostics for game logs (`0076`, off by default):
  `PANVK_FAULT_REPORT=1` (failed chain, job types, status, fault pointer and
  its BO) and `PANVK_DBG_FREE_DELAY_MS` (freed BOs stay mapped, tells a
  use-after-free apart). `tests/graphics/fault_diag_test.c`.
- [x] 13.5 Little Nightmares II GPU faults (kbase 0x4 then 0x42 = an MMU fault
  kills the context's jobs): find the job type and the bug class with the
  `0076` switches on the user's device. test4 runs: the fragment job of a
  166-226 draw render pass, every run; not a recently freed BO (fault stays
  with `PANVK_DBG_FREE_DELAY_MS=2000`), not `0074` (stays with
  `PANVK_TLS_PER_BATCH=1`), not kbase soft-stop of long jobs
  (`longjob_test`), no memory pressure (MemAvailable >= 830 MB). Next: the
  `0077` pointer audit on the device, plus `PANVK_KBASE_DEVMEM_EAGER=1` and
  `PANVK_KBASE_SYNC_SUBMIT=1` runs.
  test5 (`0077` audit): 0 suspect addresses in the audited failed batches
  (the first 3 of each run), so no stale or wrong pointer from the driver
  in those; Tomb Raider failed about 1000
  chains per second. **test6 (new base, `patches-upstream/`): 0 failed GPU
  jobs in 5 Tomb Raider runs and 1 LN2 run, pictures right.** The change of
  the new base that removed it is not isolated (candidates: chained and
  asynchronous submission 8.9/8.15, upstream Mesa since `6598829`).
