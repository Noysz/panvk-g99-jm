# Driver Environment Variables

Reference for FourFectVK test9 on Mali-G57 MC2 / Helio G99. For the short test sequence, use the [test9 README](test9/README-v0.1.0-test9.txt).

Set each name and value in Winlator's environment fields, then restart the game/container. Change one control per comparison. Defaults below apply to test9; application rules are listed separately.

[Rendering and memory](#rendering-and-memory) | [Logging](#logging) | [Submission and platform](#submission-and-platform) | [Shader cache](#shader-cache) | [Application overrides](#application-overrides) | [Maintainer controls](#maintainer-controls) | [Other backends](#other-backends)

## Reading The Tables

| Type | Values |
|---|---|
| Flag | Use `1` to enable and `0` to disable, unless the row specifies otherwise. |
| Boolean | Use `true` / `false`; the listed default applies when unset. |
| Presence | Any value, including `0`, activates it. Remove the variable to disable. |
| Value | Number, path or token list; units and special values are listed per option. |

`PANVK_V9_NO_FPK=0` still disables FPK. Remove it to restore FPK. `PANVK_V9_CLIP_KILL_FPK=1` is a separate comparison control that restores conservative clip metadata.

## Rendering And Memory

| Variable | Type / default | Function |
|---|---|---|
| `PANVK_UBO_PUSH` | Value; default 32 words | Push constant-offset UBO words into FAU registers through a GPU gather. Unset/empty or 1 selects 32; 0 disables; numeric budgets clamp to 0-64. This is not a general FPS guarantee. |
| `PAN_LATE_VECTORIZE` | Boolean; default false | Run a second UBO vectorization pass after late shader optimization. Matrix timing improved at the tested scope; gameplay benefit and lower register pressure are not established. |
| `PANVK_DEFER_OQ` | Flag; default off | Defer occlusion-query availability until the producing fragment batch, avoiding eligible query-end splits. Does not remove required consumer ordering. Repeatable gameplay gain is unproven. |
| `PANVK_V9_DEPTH_BOUNDS` | Flag; default off | Expose opt-in depth-bounds emulation on v9. The app must enable/use it. Keep off for the baseline; it is not hardware-native depth bounds. |
| `PANVK_V9_TIMESTAMP` | Flag; default off except app rule | Select timestamp writes/capability together with pan_v9_timestamps. Advertised capability also requires a successful counter/frequency query. Naruto's rule can keep it on when this variable is 0. |
| `PANVK_V9_EMULATE_SAMPLER` | Boolean; default false | Expose v9 shader emulation of cubic and min/max filtering. Changes available features; not part of the routine test9 gameplay profile. |
| `PANVK_JM_OVERLAP` | Boolean; default false | Allow eligible pass pairs to overlap using separate scratch/heap banks. Requires chained submission. Unresolved live-fault lifetime gate: leave 0 in public gameplay tests. |
| `PANVK_TILER_HEAP_INCREMENTAL` | Boolean; default false | Request incremental backing for supported tiler heaps, initially 2 MiB with descriptor bounds matching the backing. No saving demonstrated in the tested workloads; leave 0 for baseline. |
| `PANVK_KBASE_DEVMEM_EAGER` | Flag; default off | Back eligible device-local allocations at allocation time instead of lazy first use. Can raise memory consumption; use as an allocation-path comparison. |
| `PANVK_KBASE_DEVMEM_LAZY_MIN_KB` | Value; default 8192 KiB | Minimum size for eligible lazy device-local allocations. 0 makes every eligible size lazy; it does NOT disable lazy allocation. Only non-negative sizes are meaningful. |
| `PANVK_POOL_CACHE_MAX_MB` | Value; default 48 MiB | Limit cached command-pool BO memory across the device, retaining a minimum BO per pool. 0 removes the cap and can increase retained memory. |
| `PANVK_KBASE_HEAP_PERCENT` | Value; default heuristic | Override reported heap to 1-100 percent of RAM. Default targets RAM/3, at least 2 GiB and at most 75 percent. It changes reporting/app budgeting, not physical RAM; invalid values use the heuristic. |
| `PANVK_KBASE_EXEC_VA_MB` | Value; default 256 MiB | Reserve executable GPU virtual address space for shaders. Positive overrides cap at 4096 MiB; non-positive values retain the default. A refused request retries at 4 MiB. Not an upfront RAM allocation. |
| `PANVK_V9_POLY_HEAP_MB` | Value; default 128 MiB | Virtual heap size for tessellation/geometry work. 0 selects the default again. Do not treat a larger value as a verified OOM fix. |
| `PANVK_TLS_PER_BATCH` | Flag; default off | Use separate scratch per batch instead of reusing command-buffer scratch. A memory/lifetime control that may consume more memory. |
| `PANVK_TILER_HMASK` | Value; default automatic | Override the low 8 bits of the tiler hierarchy mask; accepts decimal or 0x-prefixed hex. The top level is forced on. 0 means no override. No demonstrated general game benefit. |
| `PANVK_V9_NO_TILER_CHAIN` | Flag; default off | Disable the normal tiler-job chain dependency as an ordering experiment. Leave unset for normal gameplay; not a recommended speed option. |
| `PANVK_V9_CLIP_KILL_FPK` | Flag; default off | Negative/comparison control for K2: 1 keeps conservative clip/cull shader metadata; 0/unset allows the clip-aware FPK/early-Z analysis. It is not the global FPK switch. |
| `PANVK_V9_NO_FPK` | Presence; default absent | Disable forward pixel kill on v9. Any value including 0 disables it; remove the variable to restore FPK. Do not mix its effect with the separate K2 control. |
| `PANVK_V9_NO_CLIP_CULL` | Flag; default off except app rule | Hide clip/cull-distance features and their shader emulation. Tomb Raider also selects this through driconf. Restoring features requires this flag off AND pan_disable_clip_cull_distance=false. |
| `PANVK_NO_FOURFECT_NAME` | Flag; default off | Suppress the FourFectVK version suffix in the device name. This changes identification, not rendering or speed. |

## Logging

| Variable | Type / default | Function |
|---|---|---|
| `PANVK_MEM_PROF` | Value; default off | Memory log. 1 writes to stderr; a value containing / is opened for append, falling back to stderr on failure. Reports memory accounting/process samples around two-second windows. |
| `PANVK_FRAME_PROF` | Value; default off | Frame/workload profiling. 1 uses stderr; a path containing / appends to a file. Reports GPU busy, CPU, submit/wait and work counts; not an FPS capture in Winlator. |
| `PANVK_FRAME_PROF_PASSES` | Flag; default off | Add render-pass group statistics when PANVK_FRAME_PROF is enabled. Extra instrumentation can affect timing. |
| `PANVK_FAULT_REPORT` | Flag plus low-level tracking; default off | Log failed kbase job details and address checks. Use 1 or remove it: the low-level tracker treats text such as false differently from pan_env_flag. 0 disables both paths. |
| `PANVK_FAULT_AUDIT_ALL` | Flag plus low-level tracking; default off | Audit every submitted batch's pointers, not only failures. Expensive. Use 1 or remove; avoid textual false/off because the low-level tracker only recognizes empty/0 here. |
| `PANVK_DBG_FREE_DELAY_MS` | Value; default 0 | Keep freed GPU buffers mapped for the requested milliseconds for a lifetime diagnostic. Increases retained memory and changes fault behavior; not a fix. |
| `PANVK_HEAP_LOG` | Value/Presence; default absent | Append heap/job diagnostics to the supplied path. Mere presence also disables chained submission, even if the value is empty or 0. Remove it for normal timing. |
| `PANVK_V9_TRACE` | Boolean; default false | Verbose v9 descriptor/submission tracing. Also disables chained submission for immediate decoding. Keep false in performance runs. |
| `PANVK_DEBUG_RESTAB` | Flag; default off | Print resource-table state through the v9 trace path. Requires the corresponding trace output to be enabled; not a performance setting. |
| `PANVK_KBASE_FENCE_TRACE` | Flag; default off | Log imported/exported fence paths and queue statistics. Useful for a requested synchronization report. |
| `PANVK_DEBUG` | Value; default no flags | Comma-separated PanVK debug tokens. See the debug-token list below; some tokens change allocation, rendering or synchronization and are not passive logging. |
| `BIFROST_MESA_DEBUG` | Value; default no flags | Comma-separated compiler debug tokens. See the compiler-token list below. Shader dumps and forced compiler paths can change timing or correctness. |
| `BIFROST_MESA_DUMP_DIR` | Value; default unset | Directory for binary shader/NIR dumps when the dump path is used. Must be writable. These files can be large; do not use in normal timing runs. |
| `PAN_NIR_DUMP_UBO` | Value; default off | Append final NIR for the first 24 shaders with at least 16 dynamic-offset UBO loads. A compiler diagnostic, not an optimization switch. |
| `PAN_CPU_TRACE` | Value; default unset | Enable named CPU trace categories such as lib.kmod, lib.desc, lib.afbc, lib or vk.csf. Category/backend and trace-build support determine useful output; vk.csf is not JM tracing. |
| `PANVK_ANB_TRACE` | Presence; default absent | Log Android native-buffer/AHB imports and layout decisions. Any value, including 0, enables the logging; remove to disable. |

<details>
<summary>PANVK_DEBUG and BIFROST_MESA_DEBUG tokens</summary>

### PANVK_DEBUG

```text
Use comma-separated tokens, for example startup,nir. Leave unset for normal
timing. Several tokens change execution or apply only to another backend:

  startup: log driver/device initialization.
  nir: print shader NIR during compilation.
  trace: decode submitted work; changes mapping and synchronization behavior.
  sync: force synchronization in supported backend/async-bind paths.
  noafbc: disable AFBC image layout selection.
  linear: force linear image layouts where this override is accepted.
  dump: dump GPU state; disables normal JM chaining and changes BO mappings.
  cs: CSF command-stream diagnostics, not G57 JM command tracing.
  copy_gfx: force graphics-based copies where the meta copy path supports it.
  force_simultaneous: force CSF simultaneous-use command-buffer handling.
  implicit_others_inv: CSF cache-invalidation experiment.
  force_blackhole: force blackhole handling for sparse buffers/images.
  wsi_no_afbc: reject AFBC WSI modifiers in the driver's native WSI.
  no_wb_mmap: disable write-back CPU mapping/memory-type paths.
  no_user_mmap_sync: disable userspace CPU mapping synchronization capability.
  cached_before_coherent: change memory-type order to prefer cached memory.
  no_extended_va_range: limit reported address space to 32 bits.
  hsr_prepass: permit the v13+ CSF hidden-surface-removal prepass, not G57.
  no_crc: disable image CRC/transaction-elimination use.
  kbase_diag: extra CSF kbase submission/event diagnostics, not JM tracing.
```

### BIFROST_MESA_DEBUG

Logging: shaders (NIR/MIR dumps), shaderdb (compiler statistics), verbose
(verbose disassembly), internal (include internal shaders), statsabs
(absolute statistics), statsfull (detailed statistics), debuginfo (compiler
debug information). Output depends on the compiled shader path.

Compiler experiments: nosched (trivial bundling), nopsched (disable pressure
scheduling), inorder (in-order bundling), novalidate (skip IR validation),
noopt (skip optimization passes), noidvs (disable IDVS), nosb (disable
scoreboarding), nopreload (disable message preloading), spill (exercise
register spilling), nossara (disable SSA register allocation). These can
change or break generated code. Leave them unset unless requested.

</details>

## Submission And Platform

| Variable | Type / default | Function |
|---|---|---|
| `PANVK_KBASE_WAIT_SUBMIT` | Flag; default off | Wait for submitted GPU work instead of returning through the asynchronous path. Changes scheduling; diagnostic only for timing comparisons. |
| `PANVK_KBASE_SYNC_SUBMIT` | Flag; default off | Use synchronous per-batch/job-chain submission. Enables separate geometry/fragment timing with the frame profiler, at a substantial scheduling cost. |
| `PANVK_KBASE_CPU_FENCE_WAIT` | Flag; default off | Wait for imported fences on the CPU instead of the usual GPU fence-wait path. Diagnostic fallback; can block the submitting thread. |
| `PANVK_KBASE_CPU_FENCE_EXPORT` | Flag; default off | Wait on the CPU when exporting pending fences. Changes asynchronous behavior; use only for a named synchronization test. |
| `PANVK_KBASE_USER_CACHE_SYNC` | Value; default userspace where supported | Exactly 0 requests kernel-mediated cache synchronization. Other values, including false/off, do not request that override. Unsupported userspace operations fall back through the backend. |
| `PANVK_KBASE_DMA_HEAP` | Value; default per memory type | Override the dma-heap path. On JM v9, default uncached allocations use /dev/dma_heap/system-uncached and cached allocations /dev/dma_heap/system. An override is used for both opens; compatibility/import checks still apply. |
| `PANVK_KBASE_DVFS` | Value; default no action | Try to change kbase sysfs frequency/governor: none, auto, default, max, or an available numeric frequency. Requires writable sysfs; may be ignored for permissions. Leave unset for routine tests and record any explicit clock policy. |
| `PANVK_KBASE_DRI3` | Value; default platform selection | Native X11 WSI: 0 disables dma-buf DRI3, raw/termux selects private raw-FD transport, another nonzero string selects ordinary dma-buf DRI3 if supported. Unset consults WSI_X11_TERMUX. Not a Winlator wrapper tuning guarantee. |
| `PANVK_KBASE_WSI_SYNC_PRESENT` | Flag; default off | Native X11 WSI: wait in the presenting thread/call rather than defer the wait to its present worker. Does not control a separate wrapper's WSI implementation. |
| `PAN_I_WANT_A_BROKEN_VULKAN_DRIVER` | Presence; normally unnecessary on G57 | Allow driver enumeration on architectures that require experimental opt-in. Any value satisfies the presence check. G57 normally bypasses this gate unless PANVK_V9_REQUIRE_OPTIN is set. |
| `PANVK_V9_REQUIRE_OPTIN` | Flag; default off | Restore the G57 enumeration opt-in requirement. Used as a negative launcher control; do not set for normal installation. |
| `PANVK_ANB_AFBC` | Presence; default absent | Allow the Android native-buffer path to use reported AFBC layout when the gralloc backend can describe it. Presence including 0 enables the request; fallback gralloc still forces linear handling. |
| `PANVK_ANB_NO_LINEAR_FIX` | Presence; default absent | Disable the Android native-buffer linear-layout correction. Can misinterpret imported buffers. Remove, rather than set 0, to keep the correction. |
| `PANVK_AHB_NO_LINEAR_FIX` | Presence; default absent | Disable the Android hardware-buffer linear-layout correction, also affecting the native-buffer path. Diagnostic only; any value activates it. |

## Shader Cache

These are Mesa cache controls, separate from DXVK's cache.

| Variable | Default | Function |
|---|---|---|
| `MESA_SHADER_CACHE_DISABLE` | `false` | `true` disables the disk cache. Caching also needs a writable path. |
| `MESA_SHADER_CACHE_DIR` | Automatic | Writable Android directory. If unset, Mesa searches `XDG_CACHE_HOME`, `HOME/.cache` and the platform fallback. Use separate directories for separate test profiles. |
| `MESA_SHADER_CACHE_SHOW_STATS` | `false` | Print cache statistics on normal shutdown. |
| `MESA_SHADER_CACHE_MAX_SIZE` | 1 GiB | Size limit, e.g. `256M` or `1G`. A bare number means GiB; `0` restores the default. |

`MESA_GLSL_CACHE_DISABLE` is ignored by this build's Android cache-enable path. Use `MESA_SHADER_CACHE_DISABLE`.

## Application Overrides

These case-sensitive, lower-case names are Mesa driconf environment overrides. They are not DXVK configuration keys.

| Variable | Default | Function |
|---|---|---|
| `pan_v9_timestamps` | `false`; `true` for `NSUNS2.exe` | Select timestamps together with `PANVK_V9_TIMESTAMP`. Either can enable them. To force off, set both `pan_v9_timestamps=false` and `PANVK_V9_TIMESTAMP=0`. |
| `pan_disable_clip_cull_distance` | `false`; `true` for `TombRaider.exe` | Hide clip/cull distance. To restore it, set this to `false` and remove `PANVK_V9_NO_CLIP_CULL`. This can change the rendered geometry. |
| `heap_memory_percent` | `0.0` | Nonzero fraction from 0.0 to 1.0 overrides the heap heuristic and takes precedence over `PANVK_KBASE_HEAP_PERCENT`. |
| `force_vk_vendor` | `0` | Override the reported vendor ID; 0 keeps it unchanged. |
| `force_vk_devicename` | Unset | Override the device name. A wrapper can replace it again. |
| `pan_force_enable_shader_atomics` | `false` | Force feature reporting for diagnostics; it does not implement missing support. |
| `pan_enable_vertex_pipeline_stores_atomics` | `false` | v13+ feature selection. G57 has separate v9 handling. |
| `pan_compute_core_mask`, `pan_fragment_core_mask` | All available cores | Scheduling use is in CSF, not JM. Invalid effective masks can reject the device. |

<details>
<summary>Native X11/WSI overrides</summary>

A Winlator wrapper can handle presentation itself; these options govern the driver's native WSI.

| Variable | Default | Function |
|---|---|---|
| `adaptive_sync` | `true` | Request adaptive synchronization where available. |
| `vk_x11_override_min_image_count` | `0` | Override the reported minimum image count (1-999); 0 keeps the default. |
| `vk_x11_strict_image_count` | `false` | Use the application's requested image count. |
| `vk_x11_ensure_min_image_count` | `false` | Enforce the driver's minimum image count. |
| `vk_google_display_timing` | `false` | Expose display timing even on surface types without guaranteed conformant behavior. |
| `vk_wsi_force_bgra8_unorm_first` | `false` | Report BGRA8_UNORM first. |
| `vk_wsi_force_swapchain_to_current_extent` | `false` | Use the surface's current extent. |
| `vk_wsi_disable_unordered_submits` | `false` | Disable unordered WSI submissions. |
| `vk_x11_ignore_suboptimal` | `false` | Suppress X11 VK_SUBOPTIMAL_KHR results. |

</details>

## Maintainer Controls

These controls bypass fixes or deliberately alter execution for tests. Leave them unset for gameplay.

<details>
<summary>Show maintainer controls</summary>

| Variable | Type / default | Function |
|---|---|---|
| `PANVK_DEFER_OQ_EARLY_CONTROL` | Flag; default off | Publish deferred occlusion availability too early to test whether the oracle catches the ordering error. Requires the deferred path; not a tuning option. |
| `PANVK_KBASE_CTRL_EXPORT_EARLY` | Flag; default off | Deliberately export a fence without the normal completion ordering. Only for the fence negative-control test. |
| `PANVK_KBASE_CTRL_NO_FENCE_WAIT_DEP` | Flag; default off | Remove the imported-fence wait dependency from the submission chain. Intentionally invalid ordering. |
| `PANVK_KBASE_EVENT_NO_SET` | Flag; default off | Drop a completed kbase event-set operation. Tests must detect the missing event signal. |
| `PANVK_KBASE_NO_SIGNAL` | Flag; default off | Suppress normal fence signaling. Can cause waits to hang; never use as a game option. |
| `PANVK_KBASE_NO_SYNCFD` | Flag; default off | Remove SYNC_FD import/export support. Used to test feature/profile rejection. |
| `PANVK_KBASE_SYNCOBJ_COPY` | Presence; default absent | Restore the old sync-payload copy hook instead of the generic empty-submit/export path. Any value activates this old-path control. |
| `PANVK_ANB_MOD_XOR` | Value; default absent | Hex mask XORed into imported AFBC modifiers to test detection of wrong layout. Deliberately corrupts layout interpretation; not a user option. |
| `PANVK_GRALLOC_FD0` | Presence; default absent | Force gralloc handle data[0] rather than selecting a valid buffer FD in fallback handling. Any value activates the control. |
| `PANVK_NULLDESC_POISON` | Flag; default off | Write invalid 0xff null-descriptor bytes to prove a null-descriptor test reads the slot. Deliberately invalid rendering input. |
| `PANVK_NULL_TEX_SAMPLER_SLOT` | Flag; default off | Restore the old empty combined-texture write to the sampler half instead of the texture half. Reintroduces the targeted null-texture error. |
| `PANVK_PRECOMP_STUB` | Flag; default off | Skip internal precompiled-kernel dispatch, reproducing the former stub. Breaks paths that require those kernels. |
| `PANVK_INDIRECT_CHAIN_REVERSE` | Flag; default off | Reverse indirect helper/draw ordering so the draw can see stale CPU placeholders. Deliberate ordering control. |
| `PANVK_INDIRECT_NODEP` | Flag; default off | Remove the indirect draw's dependency on the helper that patches its parameters. |
| `PANVK_INDIRECT_NO_INSTATTR` | Flag; default off | Skip indirect instance-attribute offset adjustment. Tests must catch incorrect per-instance fetches. |
| `PANVK_INDIRECT_NO_SYSVAL` | Flag; default off | Withhold indirect first-vertex/base-instance system-value patch pointers. |
| `PANVK_MDI_NO_COUNT` | Flag; default off | Ignore the indirect count buffer and execute up to maxDrawCount instead. |
| `PANVK_MDI_NO_DRAWID` | Flag; default off | Force multi-draw gl_DrawID to zero for every draw. |
| `PANVK_MVJ_NO_ZERO` | Flag; default off | Skip zero-initialization of the malloc-vertex job descriptor. Not a memory optimization. |
| `PANVK_V9_ALLOW_TESS_GS` | Flag; default off | Bypass shader-stage and null pipeline-library guards. Tessellation/GS already have implemented default paths; this is not needed to enable them. |
| `PANVK_V9_ALL_LAYERS` | Flag; default off | Replicate a draw to all framebuffer layers even when the shader does not write Layer. Restores incorrect old layer behavior. |
| `PANVK_V9_ATTR_OOB_HW` | Flag; default off | Bypass the software correction for out-of-range vertex alpha, keeping raw hardware attribute results. Useful only as a robustness negative control. |
| `PANVK_V9_DCD_NO_EARLYZS` | Flag; default off | Skip the normal early-depth/stencil and pixel-kill selection. Reproduces old descriptor behavior; not a safe early-Z toggle. |
| `PANVK_V9_DCD_PER_LAYER` | Flag; default off | Offset preload draw-descriptor pointers separately per layer, restoring the former layered preload behavior. |
| `PANVK_V9_DEPTH_CULL_ALWAYS` | Flag; default off | Keep low/high depth culling enabled even when the application's depth-clip state disables it. |
| `PANVK_V9_EARLY_COVERAGE` | Flag; default off | Force the experimental early-test coverage lowering independently of depth-bounds use. Depth-bounds-enabled shaders select required handling separately. Maintainer probe only. |
| `PANVK_V9_EMU_NO_HASH` | Flag; default off | Omit the additional v9 compiler-control hash block from shader-cache keys. Deliberately tests stale cache reuse; do not use in games. |
| `PANVK_V9_FORCE_INDIRECT` | Flag; default off | Route eligible direct draws through the indirect helper path for comparison. Changes the workload and is not a speed recommendation. |
| `PANVK_V9_GS_NO_INDIRECT` | Flag; default off | Skip indirect draws involving geometry shaders. |
| `PANVK_V9_GS_NO_UNROLL` | Flag; default off | Skip indexed primitive-restart unrolling before geometry processing. |
| `PANVK_V9_GS_NO_XFB` | Flag; default off | Skip the geometry-shader transform-feedback path. |
| `PANVK_V9_GS_PRIM_ID_RAW` | Flag; default off | Skip TES patch-ID/GS primitive-ID correction, keeping the raw identifier path. |
| `PANVK_V9_GS_RAST_BASE0` | Flag; default off | Skip the geometry raster-output base correction and keep base zero. |
| `PANVK_V9_GS_SFX_ALL` | Flag; default off | Keep API side effects in auxiliary GS shader variants instead of restricting them to the intended pass. |
| `PANVK_V9_HW_LAYER` | Flag; default off | Bypass software layer-write lowering. Does not implement a missing hardware layer path. |
| `PANVK_V9_INDIRECT_UNTRACKED` | Flag; default off | Restore the old untracked parked-indirect-tiler dependency behavior. |
| `PANVK_V9_LAYER0_ONLY` | Flag; default off | Render only the first selected layer instead of iterating the required layers. |
| `PANVK_V9_MS_STORAGE_2D` | Presence; default absent | Use the old 2D multisampled-storage view path and hide multisampled storage support. Even 0 activates it. |
| `PANVK_V9_NO_GS` | Flag; default off | Reject the otherwise implemented geometry-shader compilation path. |
| `PANVK_V9_NO_MINMAX` | Flag; default off | Skip shader lowering for min/max sampler filtering when emulation is used. |
| `PANVK_V9_NO_MS_STORAGE_FEATURE` | Presence; default absent | Hide multisampled storage-image support. Any value hides it. |
| `PANVK_V9_NO_MULTIVIEWPORT` | Flag; default off | Hide the implemented v9 multiViewport feature. |
| `PANVK_V9_NO_PER_SAMPLE` | Flag; default off | Skip normal per-sample fragment evaluation selection, restoring the older descriptor behavior. |
| `PANVK_V9_NO_PRIM_ID` | Flag; default off | Skip fragment primitive-ID descriptor enable/override handling. |
| `PANVK_V9_NO_RBA2` | Flag; default off | Hide robustBufferAccess2. DXVK versions requiring it can refuse device creation; not an optimization. |
| `PANVK_V9_NO_REISSUE_VA_RESET` | Flag; default off | Skip the vertex-allocation descriptor reset on command-buffer reissue. |
| `PANVK_V9_NO_SFX_FILTER` | Flag; default off | Skip v9 shader side-effect filtering and permit duplicate/unwanted writes. |
| `PANVK_V9_NO_STATS` | Flag; default off | Hide pipelineStatisticsQuery support. |
| `PANVK_V9_NO_TESS` | Flag; default off | Reject the otherwise implemented tessellation-shader compilation path. |
| `PANVK_V9_NO_TES_GS` | Flag; default off | Reject geometry shaders following tessellation. |
| `PANVK_V9_NO_TILE_RESOLVE` | Flag; default off | Force v9 draw-based resolve handling instead of the normal eligible tile resolve path. |
| `PANVK_V9_NO_VMSR` | Flag; default off | Hide variableMultisampleRate support. |
| `PANVK_V9_NO_VPS` | Flag; default off | Hide the v9 vertexPipelineStoresAndAtomics path; other explicit driconf forcing can still affect reporting. |
| `PANVK_V9_PRELOAD_CUBE` | Flag; default off | Keep cube descriptors for cube-attachment preload instead of treating the attachment as a 2D array. |
| `PANVK_V9_SPLIT_AT` | Value; default 65535 | Override the job-index threshold for splitting a batch. Small positive values exercise split paths. Leave unset for gameplay; it is not a draw-count FPS setting. |
| `PANVK_V9_STALE_BATCH` | Flag; default off | Keep the old batch pointer after operations that may split a batch, reproducing the stale-batch error. |
| `PANVK_V9_STATS_NO_DRAW` | Flag; default off | Skip draw-statistic increments so query counters remain incorrect. |
| `PANVK_V9_TCS_SUBGROUP_BARRIER` | Flag; default off | Keep subgroup-only TCS barriers for large patches instead of widening to the required workgroup barrier. |
| `PANVK_V9_TESS_INDIRECT_WG` | Value; default 16 workgroups | Number of 64-thread workgroups used for indirect tessellation setup; values below 1 clamp to 1. Diagnostic dispatch-size control. |
| `PANVK_V9_TESS_KEEP_IA` | Flag; default off | Keep API input topology instead of selecting the topology produced by tessellation for rasterization. |
| `PANVK_V9_TESS_NO_INDIRECT` | Flag; default off | Skip indirect tessellation draws. |
| `PANVK_V9_TESS_NO_WINDING` | Flag; default off | Skip TES winding/domain-origin correction in front-face selection. |
| `PANVK_V9_TEXEL_OOB_HW` | Flag; default off | Keep raw hardware out-of-range texel-buffer behavior instead of software robustness lowering. |
| `PANVK_V9_TIMESTAMP_SPLIT_BATCH` | Flag; default off | Restore unconditional batch splitting for timestamp writes instead of only the required boundaries. |
| `PANVK_V9_XFB_NO_ADJ` | Flag; default off | Skip adjacency-topology handling in the vertex transform-feedback path. |
| `PANVK_V9_XFB_SKIP` | Flag; default off | Skip vertex-path transform-feedback capture. |
| `PANVK_V9_ZSD_OLD` | Flag; default off | Restore the old depth/stencil descriptor, omitting corrected depth source, shader stencil, clip/clamp handling. |
| `PANVK_VARYING_NO_FAU_COUNT` | Flag; default off | Omit the varying shader's FAU count, reproducing the former uniform-read error. |
| `PAN_NO_SHIFT64_SPLIT` | Flag; default off | Bypass the compiler's constant 64-bit shift split correction. |
| `PAN_V9_IDVS_STORES_BOTH` | Flag; default off | Leave vertex memory stores in both IDVS variants instead of restricting them to the position shader. |

</details>

## Other Backends

These names are present in the source tree but do not tune the G57 JM path.

<details>
<summary>Show backend/build-specific controls</summary>

| Variable | Type / default | Function |
|---|---|---|
| `PANVK_CS_TRACEBUF_SIZE` | Value; CSF default 2097152 bytes | CSF command-stream trace-buffer size; must satisfy backend size/power-of-two constraints. G57 JM does not execute this path. |
| `PANVK_DESC_TRACEBUF_SIZE` | Value; CSF default 2097152 bytes | CSF descriptor trace-buffer size with tracing enabled. G57 JM does not use this ring buffer. |
| `PANVK_KBASE_HEAP_RENEW_INTERVAL` | Value; CSF default 128 | CSF heap-renewal interval; non-positive or out-of-range values select UINT32_MAX. Not a JM heap fix. |
| `PANVK_KBASE_HEAP_RENEW_WORK` | Value; CSF default 65536 | CSF heap-renewal accumulated-work threshold; non-positive selects 0. No effect on the JM path. |
| `PANVK_KBASE_KCPU_SYNC` | Value; CSF default off | Exactly 1 enables the optional CSF KCPU queue/CQS path. Other values leave it off; not used by G57 JM. |
| `PANVK_UTRACE_CLONE_MEM_SIZE` | Value; CSF default 10485760 bytes | Memory budget used by CSF u_trace cloning. Not a G57 gameplay setting. |
| `PAN_GPU_ID` | Value; DRM shim only | Select a simulated GPU ID in the noop DRM shim. It cannot turn the physical G57 into another GPU and is absent from this Android driver binary. |
| `PAN_USE_KRAID` | Value; Rust-compiler build only, default unset | Select cs/fs/vs/internal/all shader stages for the optional compiler when built in. This Android binary does not contain this option; do not set it for test9. |

</details>

## Scope

The reference covers 129 PanVK/Panfrost names found in the [test9 source stack](test9/build-order.md), plus the Mesa cache/application settings listed here. Source names alone do not prove a path runs on G57; other-backend options are separated above.

`PANVK_KBASE_ASYNC_CPU_WAITS`, `PANVK_KBASE_PROF` and `PANVK_BC_TILED` are not active options in test9. DXVK HUD/log/cache variables and wrapper BC settings belong to those components, not this driver's controls.
