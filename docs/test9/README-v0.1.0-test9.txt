FourFectVK G57 v0.1.0-test9 -- TEST build for Winlator
===================================================

Target: Mali-G57 MC2 / Helio G99. Experimental, not Vulkan-conformant.
Other GPUs and Winlator variants need their own results.

Release files follow test1-test7:
  FourFectVK-G57-v0.1.0-test9.zip
  README-v0.1.0-test9.txt
  SHA256SUMS.txt
The ZIP also contains this document as README.txt. Keep the README with its
matching ZIP. A local preparation is not a published GitHub release.

This guide covers T06-T10 testing and the existing settings needed for a
fair baseline and useful logs. The environment section is limited to those
tests. Start with R0; the optional runs are not a list of options to enable.

WHAT CHANGED SINCE TEST8

  - Timestamp capability and counter lifetime, including multiview queries
    and command-buffer boundaries. Naruto keeps its per-game timestamp rule.
  - Vertex-input robustness: out-of-range alpha values, null descriptors,
    and propagation through graphics pipeline libraries (GPL).
  - Conditional rendering, including an inherited predicate restored after
    a secondary command buffer's local conditional block.
  - Depth-bounds emulation, opt-in. Tested stored-depth and coverage paths
    include D16/D32, early/late tests and stencil interactions.
  - Shader-cache key fixes and Android disk-cache defaults, compiler changes,
    cached dma-buf coherency and cache/queue-error reporting.
  - Deferred occlusion, incremental tiler backing and late vectorization
    remain optional. Pass overlap is an unfinished prototype: leave it off.

Vertex robustness, conditional rendering and the cache/coherency fixes are
in the driver. There is no extra enable variable for those fixes. A feature
being reported by DXVK is not proof that the game uses it.

No repeatable gameplay FPS gain is established. Do not enable every option
at once. Fourteen old tracked TES/transform-feedback CTS failures remain;
large-draw tiler exhaustion and overlap lifetime during a fault remain open.
Wrapper and BC-transcoding changes are outside this build's changes.

INSTALL AND FIRST RUN

  1. Keep test8 installed or keep its ZIP for comparison. Back up the test
     container/save before changing its driver. Do not replace Android's
     system Vulkan libraries.
  2. Check the downloaded ZIP against SHA256SUMS.txt. Import the ZIP through
     the Winlator variant's custom-driver installer. Select the imported
     test9 driver and the existing Graphics Driver: Wrapper path.
     Menu labels differ by Winlator version; report the version you use.
  3. Fully stop the game/container and start it again after a driver or
     environment change. Many options are read once per process.
  4. Check that the container selects test9. Attach the DXVK startup log and
     driver-selection screenshot if possible. A menu label alone does not
     prove which library loaded.
  5. Remove leftover diagnostic variables. Leave BC settings unchanged from
     a working baseline; do not add ASTC transcode as part of this test.

No PAN_I_WANT_A_BROKEN_VULKAN_DRIVER setting is normally needed on G57 in
this build. Export is not part of a variable's name: in Winlator enter the
name and value in its environment-variable fields.

IMPORTANT DEFAULTS

  Off: PANVK_JM_OVERLAP, PANVK_TILER_HEAP_INCREMENTAL, PAN_LATE_VECTORIZE,
       PANVK_DEFER_OQ, PANVK_V9_DEPTH_BOUNDS, PANVK_V9_EMULATE_SAMPLER.

  PANVK_UBO_PUSH is DIFFERENT from the old test8 instructions:
       unset/empty = 32 words in this candidate; 1 also means 32 words;
       0 disables it; numeric budgets are limited to 0-64 words.
       Use an explicit 0 for the comparison profile below.

  PANVK_V9_CLIP_KILL_FPK is a control, not an enable switch for K2:
       unset/0 keeps the clip-aware optimization;
       1 keeps the older conservative compiler metadata for comparison.

  PANVK_V9_NO_FPK uses presence, not Boolean parsing:
       even PANVK_V9_NO_FPK=0 disables FPK. REMOVE the variable to restore it.

  Naruto NSUNS2.exe: timestamps are selected by the per-game rule. To force
       them off, set BOTH PANVK_V9_TIMESTAMP=0 and pan_v9_timestamps=false.
       An uppercase 0 alone does not override the per-game rule.

  TombRaider.exe: clip/cull distance reporting is disabled by its existing
       per-game rule. To restore it, remove PANVK_V9_NO_CLIP_CULL and set
       pan_disable_clip_cull_distance=false. Record this change: the game
       may draw different geometry, so an FPS-only comparison is misleading.

RUNS WANTED

Use the same game build, save, scene, resolution, graphics settings, FPS cap,
Winlator/Wine/DXVK version, wrapper, BC settings and phone power mode for A/B.
Keep VSync/post-processing fixed and frame generation off when comparing
rendered FPS. Record the limiter setting.
Keep charging state and temperature comparable. Note throttling/background
activity. Stop after a crash or GPU fault; keep the log before trying again.
Missing objects or effects invalidate a performance comparison even if FPS
increases. Keep a screenshot with the HUD and both characters/scene visible.

R0 -- launch and rendering check
  Run test9 with its defaults first. Reach gameplay, check menus, models,
  shadows, effects and transitions. Record which settings were actually used.
  This is the shipped-default check; it is not the controlled profile below.

R1/R2 -- test8 versus test9 with explicit shared controls
  For the test9 comparison run use:
    PANVK_UBO_PUSH=0
    PANVK_V9_CLIP_KILL_FPK=1
    PANVK_JM_OVERLAP=0
    PANVK_TILER_HEAP_INCREMENTAL=0
    PAN_LATE_VECTORIZE=0
    PANVK_DEFER_OQ=0
    PANVK_V9_DEPTH_BOUNDS=0
    PANVK_V9_EMULATE_SAMPLER=0
  Use PANVK_UBO_PUSH=0 on test8 too; record its complete environment. Some
  test9 controls do not exist in test8 and must not be claimed as active there.
  Keep shader caching in the same state. Test9 enables its disk cache when
  a usable cache path exists; test8's Android cache behavior differs. For a
  cache-independent comparison set MESA_SHADER_CACHE_DISABLE=true on BOTH.
  Measure cache behavior separately in R5. These controls deliberately differ
  from test9's shipped defaults; keep R0 and R1/R2 reports separate.

  Warm up for 30 seconds, then play for 2-3 minutes. For a performance claim,
  use three matched A/B pairs and alternate the order. Keep all results,
  including slow runs; record exclusions with a reason. One pair is useful
  for reporting a reproducible rendering failure, not a general speed claim.

  Naruto Storm 2:
    Free Battle -> COM vs COM -> Single Match, not Team Match.
    Pick random characters and arena once, then reuse those choices for A/B.
    Stop at 2-3 minutes; do not wait for a KO. Record characters and arena.
    With the tested DXVK GPL-async fork, use these dxvk.conf settings:
      dxvk.enableGraphicsPipelineLibrary = True
      dxvk.enableAsync = False
    These are DXVK configuration keys, NOT environment-variable names.
    Support depends on the DXVK version. Do not force unsupported keys.
    The earlier short battle used timestamps and memory profiling with the
    shader cache disabled. Its roughly 11 FPS is one diagnostic run with
    different settings, not a target FPS or a controlled comparison.

R3 -- T07 late-vectorization comparison
  Start from R2, change only PAN_LATE_VECTORIZE=1, then fully restart.
  Keep cache policy, scene and other variables fixed. Compare pictures and
  frametimes against PAN_LATE_VECTORIZE=0. Restore 0 after the comparison.
  This existing option was evaluated in T07; it is not a new test9 feature.
  A matrix workload improved, but gameplay benefit is not established.

  Keep PANVK_JM_OVERLAP=0: T08's fault/lifetime work is incomplete.
  Keep PANVK_TILER_HEAP_INCREMENTAL=0 unless a maintainer requests a T06
  comparison. The tested workloads showed no tiler backing saving.

R4 -- optional depth-bounds compatibility check
  Compare PANVK_V9_DEPTH_BOUNDS=0 and 1 with the same app/DXVK/scene. Record
  startup, image differences and crashes. If the application does not request
  or issue a depth-bounds operation, report "usage not confirmed". A feature
  list or successful AIO launch alone is not a depth-bounds correctness test.
  Maintainers use the stored-depth probes/CTS for that proof. Restore 0 after
  the check. It is an emulation and can cost GPU time.

R5 -- optional shader-cache check
  Set MESA_SHADER_CACHE_DISABLE=false and MESA_SHADER_CACHE_SHOW_STATS=true.
  Use a NEW writable MESA_SHADER_CACHE_DIR for this driver/profile; leave old
  cache files intact. Run the same scene once cold, exit normally, then repeat
  with the same directory warm. Record hits/misses if the log provides them,
  first-run stutter and warm-run frametimes. No cache files/no writable path
  means the cache test was not completed. DXVK's own cache is separate; record
  its settings. Do not compare a cold A run with a warm B run as a speed gain.

R6 -- longer stability, after the short checks pass
  An optional 30-minute session can check memory growth and transitions in
  an affected game. Keep it separate from the 2-3 minute comparison runs.
  Record the scene changes, elapsed time and any crash/kill. A short run does
  not prove that a memory leak or Android process termination is fixed.

LOGGING

Use distinct writable paths for each run. The native driver needs an Android
path, not a Windows C:\ path. The paths below are examples; Winlator storage
permissions and mounts vary. If no file appears, check the container's logs.
The memory/frame file loggers append and fall back to stderr if opening the
path fails. PANVK_FAULT_REPORT writes diagnostics to the process log.

  PANVK_MEM_PROF=/storage/emulated/0/Download/test9-R2-mem.log
  PANVK_FRAME_PROF=/storage/emulated/0/Download/test9-R2-frame.log
  PANVK_FAULT_REPORT=1

Memory/profile logs have overhead: use the same logging setup for both sides
or collect them in a separate diagnostic run. The frame log reports roughly
two-second windows of GPU busy time, process CPU use, submit/wait time and
work counts. Process CPU 100% means one core. It does not replace an FPS or
frametime capture from the same measurement window.

PANVK_KBASE_SYNC_SUBMIT=1 serializes work for a separate timing diagnostic.
It is slow and changes scheduling. Never use that run as normal performance.
Remove leftover heap, shader and submission tracing before routine FPS
comparisons; tracing can change timing or submission behavior.

REPORT TO GITHUB

https://github.com/Noysz/panvk-g99-jm/issues/new/choose
Use the test9 report form when available, or paste this into an issue:

  Title: [test9] <game/app> - <what fails or changes>
  Device / SoC / GPU:
  Android version / kernel (if known):
  ZIP filename + SHA256; selected driver / loaded-driver log:
  Winlator variant + version; Wine/Proton + architecture:
  DXVK exact version; wrapper / BC options:
  Game/app version; D3D level if known:
  Resolution / graphics / FPS cap / power mode / charging:
  Scene/save; Naruto characters, arena and Single Match if applicable:
  Test ID (R0-R6); all environment variables; dxvk.conf:
  Reproduction steps:
  Expected image/behavior:
  Actual image/behavior:
  test8 result / test9 result:
  Warmup + measured duration; cache cold/warm/disabled:
  Per-run FPS / median and p95 frametime (only if actually measured):
  Temperature/throttling/background activity, if observed:
  Crash/hang time, exact error lines, frequency (e.g. 2 of 3 runs):
  Screenshots/video and logs, labelled by run:

Write "not measured" or "unknown" for unavailable data. Include all runs.
For a hang report whether the image froze, the process exited, or Android
closed it; an absent GPU-fault log does not identify the cause. Attach text
logs as files, not just screenshots of the final line. Do not attach saves,
game executables, tokens or personal account data.

WHAT HAS BEEN CHECKED

Source candidate: public commit 7f28e47db6e50215fbe2935cecaf048f23d6dbfb.
Android driver SHA256:
  595981cc9ec21f0b667a4127e6b31bb9a03c8d2aa4278b04a7daefff4f3dcf4e
Host/Android feature controls and repeats are recorded in the repository.
Final tracked CTS: 5,658 unique normal cases per platform, each with
3,754 Pass / 1,890 NotSupported / 14 Fail. Unchanged test8 on the same cases
and native dependencies: 3,753 / 1,890 / 15. One case improves, no Pass lost.
The older 7,127 total included overlapping groups, own tests and a control.
These results are not full Vulkan CTS or RC qualification.

Four DXVK versions have triangle/readback checks and AIO tessellation/GS
scene runs. The reconstructed native Wine/Termux:X11 stack was tested;
that is not the same as testing every actual Winlator app/container.
Black areas in one AIO render-window capture were reproduced as occlusion
by separate HUD/result windows; paired desktop captures show those windows.
Depth-bounds advertisement in AIO does not prove an operation was exercised.

Evidence and package status:
  https://github.com/Noysz/panvk-g99-jm/blob/main/docs/test9-readiness.md
  https://github.com/Noysz/panvk-g99-jm/tree/main/evidence/validation/api-2026-10-26

ENVIRONMENT SETTINGS FOR T06-T10

Set these in Winlator's environment fields using the exact name and value.
Restart the game/container after every change. Use 1/0 for flags and
true/false for Boolean options. Remove path-valued settings to restore their
usual behavior; there is no universal "0 disables everything" rule.

T06 - MEMORY AND IMPORT COHERENCY

  PANVK_TILER_HEAP_INCREMENTAL
    Default false. true requests incremental backing for supported tiler
    heaps, starting at 2 MiB. Keep false for normal tests. No saving was
    demonstrated in the tested workloads.

  PANVK_KBASE_USER_CACHE_SYNC
    Diagnostic only. Exactly 0 requests kernel-mediated cache synchronization
    instead of userspace synchronization where supported. Remove to restore
    default selection. The text false does not mean the same as 0 here.
    Use only for a requested cache-coherency comparison.

  The corrected cached dma-buf properties and cache-sync error handling
  require no enable variable. Normal rendering and longer memory runs test
  their behavior; do not attribute them to the incremental-tiler switch.

T07 - COMPILER WORKLOAD COMPARISON

  PAN_LATE_VECTORIZE
    Default false. true runs the existing second UBO vectorization pass after
    late shader optimization. R3 compares it against false. The matrix result
    does not establish lower register pressure or faster gameplay.

T08 - INCOMPLETE OVERLAP PROTOTYPE

  PANVK_JM_OVERLAP
    Default false. true permits eligible pass pairs to overlap using separate
    scratch/heap banks and chained submission. Live-fault resource lifetime
    remains unresolved. Leave false; it is not part of the tester A/B runs.

T09 - API AND CORRECTNESS

  PANVK_V9_TIMESTAMP
    Default off except the per-game rule below. 1 selects timestamp handling;
    0 clears this selection. Advertised capability also needs a successful
    GPU counter/frequency query. It is not an FPS boost switch.

  pan_v9_timestamps
    Lower-case Boolean option, default false; true for Naruto NSUNS2.exe.
    Either this option or PANVK_V9_TIMESTAMP can select timestamps. To force
    them off, both must be off. Leave Naruto's rule intact for normal tests;
    disabling it can change or stall the game's query path.

  PANVK_V9_DEPTH_BOUNDS
    Default off. 1 exposes the emulation for an app that enables/uses it.
    Compare 0/1 only in R4, then restore 0. The emulation can cost GPU time.
    A listed capability alone does not prove that the app exercised it.

  Vertex-input robustness/GPL/null-descriptor fixes and conditional rendering
  need no extra environment variable. Maintainer fault-injection controls
  are not gameplay options and are omitted from this guide.

T10 - TESTING AND PACKAGING

  T10 adds validation and packaging, not a new performance switch. Use R0-R6
  and the reporting fields above. The settings below support comparisons and
  logging; they are existing controls, not features introduced by T06-T10.

EXISTING BASELINE SETTINGS

  PANVK_UBO_PUSH
    Unset/empty or 1 selects 32 words; 0 disables; numeric budgets clamp to
    0-64 words. Use 0 for R1/R2 so the old and new driver share the explicit
    baseline. R0 keeps the shipped default.

  PANVK_V9_CLIP_KILL_FPK
    Unset/0 allows the clip-aware FPK/early-Z analysis; 1 keeps the older
    conservative metadata. R1/R2 use 1. This is the earlier K2 control.

  PANVK_DEFER_OQ
    Default off. 1 defers eligible occlusion-query availability until the
    producing fragment batch. Keep 0 for these comparisons. This comes from
    earlier query work; repeatable gameplay benefit remains unproven.

  PANVK_V9_EMULATE_SAMPLER
    Default false. true exposes cubic/min-max sampler emulation on v9.
    Keep false for the shared baseline; it is outside this test matrix.

  PANVK_V9_NO_FPK
    REMOVE this old diagnostic variable. Any value, even 0, disables global
    FPK. It is separate from the clip-aware K2 comparison control.

  PANVK_V9_NO_CLIP_CULL / pan_disable_clip_cull_distance
    Either can hide clip/cull distance and skip its emulation. The upper-case
    flag defaults off; the lower-case Boolean defaults false but is true for
    TombRaider.exe. Preserve the game's setting during A/B. Changing it can
    change the image, so it cannot be treated as a speed-only adjustment.

LOGS AND CACHE

  PANVK_MEM_PROF / PANVK_FRAME_PROF
    Default off. 1 logs to stderr; a value containing / appends to that file,
    falling back to stderr on open failure. Memory/work counts and time
    windows help diagnosis; neither replaces an FPS/frametime capture.

  PANVK_FAULT_REPORT
    Default off. Use 1 for fault diagnostics, or remove it to disable.
    Avoid textual false/off: low-level tracking parses those differently.

  PANVK_KBASE_SYNC_SUBMIT
    Default off. 1 serializes submission for a requested timing diagnostic.
    Keep unset/0 in normal performance runs because it changes scheduling.

  MESA_SHADER_CACHE_DISABLE
    Default false in test9 when a usable cache path exists. true disables
    Mesa's disk cache. Use true on both drivers for cache-independent R1/R2.

  MESA_SHADER_CACHE_DIR
    Writable Android cache directory for R5. Use a new directory for each
    driver/profile, then reuse it for that profile's warm run. If unset,
    Mesa searches XDG_CACHE_HOME, HOME/.cache and the platform fallback.
    Leave existing caches intact.

  MESA_SHADER_CACHE_SHOW_STATS
    Default false. true prints cache statistics on normal cache teardown.
    An abrupt kill can prevent the counters from appearing.

  DXVK_LOG_LEVEL / DXVK_LOG_PATH
    Use info and a writable directory when collecting reports. Verify that
    the log appears there; launcher/version defaults differ. Debug logging
    can add overhead and should be kept separate from performance runs.

  DXVK_HUD
    Optional display of supported items such as fps,frametimes,compiler.
    Availability depends on DXVK. A HUD screenshot is not a frametime series.

  DXVK_STATE_CACHE
    On supporting versions, 0 disables DXVK's state cache. It does not disable
    Mesa's shader cache. Record the setting and exact DXVK fork/version.

  dxvk.enableGraphicsPipelineLibrary and dxvk.enableAsync are configuration
  keys in dxvk.conf, not environment variables. The Naruto instructions above
  apply only to DXVK versions that support those keys.
