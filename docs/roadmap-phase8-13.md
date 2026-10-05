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
2. Poly heap on JM, allocated lazily and smaller than the CSF 128 MiB
   (RAM budget, see Phase 12).
3. Tessellation on v9 (Phase 10): compile the SW VS / TCS / TES variants on
   v9, port `launch_tess`, lift the 0057 guard for tess. CTS `tessellation.*`.
4. Geometry shader (Phase 11a): `poly_nir_lower_gs` in the PanVK compile,
   geometry kernels in libpan, port of the Honeykrisp GS sequence, then
   `geometryStreams`. CTS `geometry.*`, AIO "GS Exploder".
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

## Phase 9 — small features DXVK asks for

- [ ] 9.1 `depthBounds` (hardware has a depth-bounds test on Valhall? check
  genxml first; otherwise shader emulation).
- [ ] 9.2 `variableMultisampleRate`.
- [ ] 9.3 `vertexPipelineStoresAndAtomics` (upstream only on v13; check what
  v9 needs: VS/TES side effects with IDVS).
- [ ] 9.4 `shaderClipDistance` / `shaderCullDistance` in the driver (today
  only via the Wrapper).
- [ ] 9.5 `multiViewport`.

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

## Phase 13 — CPU and GPU cost

CPU: per-draw descriptor emission, the one-job-per-draw MALLOC_VERTEX path,
synchronous submit-and-wait per batch in the kbase queue. GPU: early-ZS /
FPK, tile size, AFBC, needless preloads. Target: frame-time traces before and
after each change, same scene, 3 runs.
