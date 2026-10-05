# patches/

Base commit for everything here: **`6598829019c0746aa8e473b4ae1c980cbfa6ea4b`**
(upstream Mesa), the commit the driver work tree sits on.

## Apply matrix — tested, not assumed

Each patch was checked with `git apply --check` against **pristine copies of the
23 target files extracted from the base commit**. This is the observed result,
not a guess:

| Patch | Applies to base? | Notes |
|---|---|---|
| `0001-panvk-add-v9-to-build-matrix` | ❌ no | HISTORICAL. Bare filename paths; authored against a Mesa revision whose `foreach` list contained arch 11. Superseded by `0014`. |
| `0002-panvk-physical-device-proto-v9` | ❌ no | HISTORICAL. Bare filename paths. Superseded by `0014`. |
| `0003-kbase-exec-va-pages` | ❌ no | HISTORICAL. Written against an intermediate `kbase_kmod.c`, so context does not match upstream. Superseded by `0013`. |
| `0004-panvk-v9-graphics-draw-path-WIP` | ✅ yes | HISTORICAL-SUPERSEDED by `0010`. Do not combine. |
| `0005-panvk-v9-kbase-jm-submit-and-debug` | ✅ yes | HISTORICAL-SUPERSEDED by `0012`. Do not combine. |
| `0010-panvk-v9-jm-graphics-draw-path` | ✅ yes | **current** |
| `0011-panvk-v9-jm-compute-dispatch` | ✅ yes | **current** |
| `0012-panvk-v9-jm-queue-submit-and-raw-capture` | ✅ yes | **current** |
| `0013-panfrost-lib-kbase-and-fb-fixes` | ✅ yes | **current** — contains the EXEC_VA fix |
| `0014-panvk-v9-arch-enablement-common` | ✅ yes | **current** — contains `jm_archs`, `foreach`, `PER_ARCH_FUNCS(9)` |
| `0015-termux-android-build-fixes` | ✅ yes | **current** |
| `0020-panvk-v9-fau-count-from-shader-metadata` | ❌ by design | Applies to **exactly one** tree state: the side-A source `panvk_vX_cmd_draw.c.ab_fau_off`, i.e. the post-`0010` draw path with these two lines removed. Verified to apply cleanly there. It does **not** apply to bare upstream (the code does not exist until `0010`) and does **not** apply to the current tree either (the change is already present, so the hunk context fails). Already contained in `0010`. |
| `0030-phase4-restab-instrumentation` | ✅ yes | **current** — observation only. Adds an env-gated `PANVK_DEBUG_RESTAB` dump of `used_set_mask`, `first_unused_set`, `res_count`, `entry_bytes`, and every resource-table entry. Emits nothing and changes no descriptor when the variable is unset, so the validated baseline is unaffected — confirmed by an unchanged 512/4096 pixel result. Applies cleanly to the pristine base file; does **not** apply to the active tree because it is already present there. |
| `0031-v9-compute-fau-count` | ✅ yes | **current** — one line, `cfg.compute.fau_count = cs->fau.total_count` in the v9 compute dispatch. Without it the FAU pointer is valid and the count zero, so any constant living in FAU reads back as zero. Same defect class as the graphics FAU fix in `0010`. A/B: `0xA5A50000 \| id` returned plain `id` before, correct after. |
| `0032-v9-dispatch-precomp` | ✅ yes | **current** — ports `dispatch_precomp` to v9's single flat `COMPUTE_JOB.PAYLOAD` section. Bifrost uses three sections (`INVOCATION`, `PARAMETERS`, `DRAW`) that do not exist at v9. Unblocks every internal precomp helper. Note the stub it replaces was installed by this project's own patch `0011`, not inherited from upstream: upstream does not build a v9 Job Manager target at all (`jm_archs = [6, 7]`). Consequences measured, not assumed: `vkCmdDispatchIndirect` faulted the GPU before this, and `vkCmdFillBuffer` silently did nothing. |
| `0033-v9-draw-indirect-helper-kernel` | ✅ yes | **current** — adds a `PAN_ARCH == 9` block to `libpan/draw_helper.cl` with two kernels that patch a `MALLOC_VERTEX` job from an indirect buffer and promote its job header. Far smaller than the Bifrost equivalents because v9 packs the whole draw into one job. |
| `0034-v9-draw-indirect-entrypoint` | ✅ yes | **current** — implements `CmdDrawIndirect` and `CmdDrawIndexedIndirect` for v9, and fixes two pre-existing bugs found while doing it: `base_vertex_offset` was hardcoded to 0 so `vkCmdDraw` ignored `firstVertex`, and the `INDICES` address omitted `firstIndex * index_size` so `vkCmdDrawIndexed` ignored `firstIndex`. |
| `0035-v9-job-dependency-instrumentation` | ✅ yes | **current**, debug only — decodes `Index`, `Dependency 1`, `Dependency 2` and `Next` from the job header in the status dump, and adds `PANVK_INDIRECT_NODEP=1` to drop the indirect draw's dependency as a negative control. Added because an earlier note inferred *execution* order from a dump that only reports the order job pointers were recorded. The control produced an uncomfortable result worth reading: removing the dependency changes nothing observable in 40 runs, so ordering here comes from chain serialisation rather than from the dependency. See [phase4-open-questions.md](../docs/phase4-open-questions.md) §5.3. |
| `0036-v9-precomp-stub-ab-control` | ✅ yes | **current**, debug only — `PANVK_PRECOMP_STUB=1` makes the v9 `dispatch_precomp` return immediately, reproducing the empty stub this project's own patch `0011` installed so the shared object would link. Lets the before and after states be compared against one build. It is what showed `vkCmdFillBuffer` was silently doing nothing on v9 before `0032`, and correct across all four of its code paths after. |
| `0037-v9-indirect-chain-order-experiment` | ✅ yes | **current**, debug only — `PANVK_INDIRECT_CHAIN_REVERSE=1` queues the indirect draw job *before* its helper while keeping the dependency, turning it into a forward reference so chain order and the dependency disagree. Settles which one the hardware obeys. Answer: chain order. The draw faults with `0x10258` as a reserved job type and the helper never runs. Predicts the helper's index and asserts the prediction rather than trusting it. See [phase4-open-questions.md](../docs/phase4-open-questions.md) §5.3. |
| `0038-v9-indirect-draw-parameter-sysvals` | ✅ yes | **current** — the v9 indirect helpers now write `firstVertex`/`vertexOffset` and `firstInstance` into the draw's `first_vertex` and `base_instance` sysvals. Before this, `gl_InstanceIndex`, `gl_BaseInstance` and `gl_BaseVertex` saw the CPU placeholders on indirect draws while the direct path honoured them, so the two paths disagreed. Measured with `firstInstance = 2` and `firstVertex = 4`; indirect is now byte-identical to direct. `PANVK_INDIRECT_NO_SYSVAL=1` restores the old behaviour as a negative control. See [phase4-open-questions.md](../docs/phase4-open-questions.md) §5.4. |
| `0039-v9-instance-rate-attributes-first-instance` | ✅ yes | **current** — `firstInstance` was ignored for instance-rate vertex attributes on v9, on direct draws too, because nothing on the v9 path assigned `vi.base_instance`, which `emit_vs_attrib()` folds into the attribute offset. Sets it per draw, and makes the indirect helpers add `firstInstance * stride` to each instance-rate attribute descriptor's `Offset`. `PANVK_INDIRECT_NO_INSTATTR=1` is the control. See [phase4-open-questions.md](../docs/phase4-open-questions.md) §5.9. |
| `0040-v9-varying-shader-fau-count` | ✅ yes | **current** — one line. The `VARYING` shader environment had a FAU pointer but no `fau_count`, so any vertex shader with a FAU block of 4 or more words rendered black; two `vec4` varyings were enough. Same defect class as `0010` and `0031`. `PANVK_VARYING_NO_FAU_COUNT=1` is the control. |
| `0041-v9-kbase-fence-signal` | ✅ yes | **current**. Waits and signals in `jm/panvk_vX_gpu_queue.c` were handled as DRM syncobjs, whose ioctls fail silently on a kbase fd, so no fence was ever signalled and `vkWaitForFences` always timed out. Now uses `vk_sync_wait` / `vk_sync_signal` (submits are synchronous). `PANVK_KBASE_NO_SIGNAL=1` is the control. Found by CTS, see [`../docs/cts-wsi-texture.md`](../docs/cts-wsi-texture.md#0041-fences-and-semaphores-were-never-signalled-verified-hw). |
| `0042-v9-vs-layer-write-lowering` | ✅ yes | **current**. Runs Bifrost's `lower_layer_writes` on v9 too, plus a v9-only `layer_id` sysval set to 0. Without it, vk_meta's rect shader (writes `gl_Layer`) lost a triangular wedge, which broke graphics-path copies to OPTIMAL images. Mechanism not established. `PANVK_V9_HW_LAYER=1` is the control (compile time, needs `MESA_SHADER_CACHE_DISABLE=true`). |
| `0043-v9-zero-malloc-vertex-job` | ✅ yes | **current**. Zeroes the MALLOC_VERTEX job after allocation. Recycled pool memory left a previous clear colour in unwritten bytes, which gave `DATA_INVALID_FAULT` only when another render had run earlier in the process. `PANVK_MVJ_NO_ZERO=1` is the control. |
| `0044-v9-EXPERIMENTAL-multi-draw-indirect-count` | ✅ yes | 🧪 **EXPERIMENTAL**. Exposes `multiDrawIndirect` and `VK_KHR_draw_indirect_count` on v9 (upstream: v10+). Draws are unrolled one job per draw, `gl_DrawID` becomes a sysval, the helper kernel NULLs draws past the GPU-side count. `maxDrawIndirectCount` 65535. Controls `PANVK_MDI_NO_COUNT=1`, `PANVK_MDI_NO_DRAWID=1`. See [`../docs/v9-experimental-features.md`](../docs/v9-experimental-features.md). |
| `0045-v9-EXPERIMENTAL-robustness2-null-descriptor` | ✅ yes | 🧪 **EXPERIMENTAL**. Exposes `VK_EXT/KHR_robustness2` with `nullDescriptor` on v9. `robustBufferAccess2` only with `PANVK_V9_RBA2=1` (texel buffers fail). Control `PANVK_NULLDESC_POISON=1`. |
| `0046-v9-EXPERIMENTAL-sampler-minmax-emulation` | ✅ yes | 🧪 **EXPERIMENTAL**. `samplerFilterMinmax` on v9 by shader emulation: mode in sampler word 3, new NIR pass `panvk_v9_nir_lower_minmax.h`. **Cube maps broken**, adds a descriptor load and a branch to every float texture sample. Control `PANVK_V9_NO_MINMAX=1` (compile time). |
| `0047-v9-EXPERIMENTAL-sampler-minmax-cube` | ✅ on `0046` state | 🧪 **EXPERIMENTAL**. Cube / cube-array support for the `0046` min/max emulation. CTS cube subset 72P/72NS 3x, control 36 fail. |
| `0048-v9-layered-rendering-per-layer-draws` | ✅ on `0046` state | **current**, bug fix. v9 layered rendering: one draw per layer with `layer_id` sysval and `layer_offset`; frame-shader DCD per-layer offset only below v9. CTS layers 62/62 3x, control `PANVK_V9_LAYER0_ONLY=1` 17 fail. |
| `0049-v9-EXPERIMENTAL-cubic-filter-emulation` | ✅ on `0046` state | 🧪 **EXPERIMENTAL**. `VK_EXT_filter_cubic` by Catmull-Rom emulation in the shader. CTS 272P/192NS 3x, control 175 fail. |
| `0050-v9-EXPERIMENTAL-sampler-emulation-opt-in` | ✅ on `0046` state | 🧪 **EXPERIMENTAL**. Sampler emulation (`0046`/`0047`/`0049`) is off by default; `PANVK_V9_EMULATE_SAMPLER=1` turns it on. Shader-cache key includes the mode. |
| `0051-v9-trace-gate-and-job-chain-error` | ✅ on `0046` state | **current**. Debug prints behind `PANVK_V9_TRACE=1` (`src/panfrost/lib/pan_v9_trace.h`, new file); one `mesa_loge` line when a job chain fails. |
| `0052-android-hal-build-compat` | ✅ on `0046` state | **current**. Builds the Android HAL (`-Dplatforms=android -Dandroid-stub=true`) for Winlator. |
| `0053-android-v9-no-env-gate` | ✅ on `0046` state | **current**. Android builds expose v9 without `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER` (the Winlator launcher queries the driver without the container env). Control `PANVK_V9_REQUIRE_OPTIN=1`. |
| `0054-android-mtk-gralloc-dmabuf-fd-ahb-linear` | ✅ on `0046` state | **current**. MediaTek gralloc handle: dma-buf is fd[1], not fd[0]. AHB allocations linear when the fallback gralloc cannot report the modifier. Controls `PANVK_GRALLOC_FD0=1`, `PANVK_AHB_NO_LINEAR_FIX=1`. |
| `0055-kbase-sync-fd-import-export` | ✅ on `0046` state | **current**. `SYNC_FD` import/export for kbase CPU sync, runtime NULL sync-type guards. CTS 26P/2NS 3x, control `PANVK_KBASE_NO_SYNCFD=1`. |
| `0056-v9-jm-kbase-vkevent` | ✅ on `0046` state | **current**. JM kbase `VkEvent` as an atomic flag. CTS 19P/13NS 3x, control `PANVK_KBASE_EVENT_NO_SET=1` 3 fail. |
| `0057-v9-EXPERIMENTAL-reject-tess-gs-pipelines` | ✅ on `0046` state | 🧪 **EXPERIMENTAL**. v9 refuses pipelines with tessellation/geometry stages, and the runtime refuses links with a `VK_NULL_HANDLE` library. Fixes the AIO-Graphics-Test "GS Exploder" crash (3/3, controls crash). Control `PANVK_V9_ALLOW_TESS_GS=1`. |
| `0058-v9-dcd-no-fs-earlyzs-and-batch-split` | ✅ on `0057` state | **current**, 3 bug fixes from CTS. (1) No fragment shader: the v9 DCD dereferenced a NULL FS (SIGSEGV, `occlusion_query.*no_attachments*`). (2) Early-ZS kill/update and `shader_modifies_coverage` were never set on v9, so `discard`ed samples were counted by precise occlusion queries; control `PANVK_V9_DCD_NO_EARLYZS=1` 28 fail. (3) After a batch split inside a render pass the draw went to the closed batch (`record_many_draws_secondary_2`); control `PANVK_V9_STALE_BATCH=1`. Test knob `PANVK_V9_SPLIT_AT=n`. |
| `0059-v9-msaa-per-sample-a2c-and-shader-depth` | ✅ on `0058` state | **current**, 3 MSAA bugs from CTS. (1) `evaluate_per_sample` was never set on v9: shaders reading `gl_SampleID` / `gl_SamplePosition` / `interpolateAtSample` or `sampleShadingEnable` ran once per pixel. (2) `alpha_to_coverage` was never enabled. Control for both `PANVK_V9_NO_PER_SAMPLE=1` (86 fail). (3) The v9 depth/stencil descriptor never set `depth_source`, `stencil_from_shader`, depth clip or depth clamp: shader-written depth was lost with per-sample shading, `depthClamp`/`depthClipEnable` ignored. Control `PANVK_V9_ZSD_OLD=1` (8 fail). Test knob `PANVK_V9_NO_TILE_RESOLVE=1` (Z/S resolve through store + meta instead of the tile resolve shader). |
| `0060-v9-transform-feedback` | ✅ on `0059` state | **current**, 🧪 EXPERIMENTAL. `VK_EXT_transform_feedback` / `transformFeedback` on v9. The XFB variant of the vertex shader runs as a COMPUTE job in the batch's vertex/tiler chain (like gallium `jm_launch_xfb` on v9 and CSF `launch_xfb`). New libpan kernels `panlib_xfb_begin/add/end` do the counter loads, offset adds and stores that CSF does in the command stream, and `panlib_xfb_expand` rewrites line strips, triangle strips and fans into independent primitives. A vertex shader whose only outputs are XFB captures compiles to an empty hardware variant; the capture still runs. Limits as CSF: indirect draws are not captured, indexed draws are captured in linear order, 1 stream, no `transformFeedbackDraw` / queries. Control `PANVK_V9_XFB_SKIP=1` (81 of 82 fail). |
| `9001-termux-android-detection-fixes.UPSTREAM-THIRDPARTY` | ✅ yes | third-party, from LukeValen/panvk-mali-g52 |

## The set that actually reproduces the current driver

Apply these six, in this order, to a clean checkout of the base commit:

```sh
git checkout 6598829019c0746aa8e473b4ae1c980cbfa6ea4b
git apply patches/0015-termux-android-build-fixes.patch
git apply patches/0014-panvk-v9-arch-enablement-common.patch
git apply patches/0013-panfrost-lib-kbase-and-fb-fixes.patch
git apply patches/0011-panvk-v9-jm-compute-dispatch.patch
git apply patches/0012-panvk-v9-jm-queue-submit-and-raw-capture.patch
git apply patches/0010-panvk-v9-jm-graphics-draw-path.patch
```

**Verified, not asserted.** This sequence was replayed against pristine
base-commit copies of all 23 target files. Every patch applied without conflict,
and the result was then compared file-by-file against the live driver work tree:

```
identical: 23   differing: 0   (of 23)
```

So these six patches reproduce the exact source that built the ICD which
produced the logs in [`../evidence/`](../evidence/).

Do **not** add `0004`, `0005`, `0001`, `0002`, `0003` or `0020` to that list.
`0004`/`0005` are earlier drafts of the same files as `0010`/`0012` and will
conflict or double-apply. `0001`/`0002`/`0003` do not apply at all.
`0020` is already inside `0010`.

Never use a `patches/*.patch` glob — it sweeps in the historical ones.

## Patches 0047-0057: a real stack

Unlike the earlier working-tree extracts, `0047`-`0060` were generated
per change from timestamped backups and **stack in order** on the tree that
`0041`-`0046` describe (the driver state of commit `c080268`). Checked: that
state plus `0047`..`0057` applied (and `0058`-`0060` on top; checked 48/48 identical again after `0060`) in order equals the live driver tree,
`identical: 44  differing: 0  (of 44)`. Generator:
[`../tools/winlator/mkstagepatches.py`](../tools/winlator/mkstagepatches.py).

## Reproducing the FAU A/B experiment

`0020` exists so the causal change can be read as two lines instead of being
hunted for inside `0010`. To run the experiment, use the preserved source
variants rather than the patch — see
[`../docs/reproduction.md`](../docs/reproduction.md#5-reproduce-the-fau-ab-experiment).

## Caveats

These are working-tree extracts, not upstream submissions. They are not rebased,
not split per logical change, and their debug prints and comments are partly in
Indonesian. That is deliberate: the patches match the binary that produced the
logs in [`../evidence/`](../evidence/), and rewriting them would break that
correspondence.
