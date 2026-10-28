FourFectVK G57 v0.1.0-test9 -- TEST build for Winlator
===================================================
Mali-G57 MC2 / Helio G99. Experimental, not Vulkan-conformant.

Changes:
  - Timestamp lifetime, vertex-input/GPL and conditional-rendering fixes.
  - Shader-cache and memory-coherency fixes; UBO push defaults to 32 words.
  - Clip-aware FPK, optional deferred occlusion queries and depth bounds.

Install the ZIP through Winlator's custom-driver installer, select test9
as the Wrapper driver, then restart the container.

Testing:
  Choose any game. Use the same scene, resolution, settings and DXVK for
  each run. Warm up for 30 seconds, then play for 2-3 minutes. Check models,
  shadows/effects, stutter and crashes as well as FPS.
  Enter the variables below in Winlator's environment settings.
  Clear other driver overrides before starting. Restart after each change.

R0 - Default behavior
  PANVK_V9_CLIP_KILL_FPK=0
  PANVK_DEFER_OQ=0
  PANVK_V9_DEPTH_BOUNDS=0
  PANVK_TILER_HEAP_INCREMENTAL=0
  PANVK_JM_OVERLAP=0
  This is the reference for R1-R3.

R1 - Clip/FPK comparison
  Start from R0; change only PANVK_V9_CLIP_KILL_FPK=1.
  Uses conservative clip/FPK metadata instead of the clip-aware optimization.
  Compare rendering and FPS with R0; 1 is the comparison setting, not a boost.

R2 - Deferred occlusion queries
  Start from R0; change only PANVK_DEFER_OQ=1.
  Defers eligible occlusion-query availability instead of closing the batch
  at EndQuery. Compare FPS/stutter, missing objects and hangs with R0.

R3 - Depth bounds
  Start from R0; change only PANVK_V9_DEPTH_BOUNDS=1.
  Exposes depth-bounds emulation. Check startup, effects and FPS against R0.
  It can cost GPU time; games that do not use it may show no difference.

Restore R0 before each new session; do not stack R1-R3 settings.
Keep incremental tiler backing and pass overlap at 0: validation is unfinished.
The other fixes need no enable variable. Known CTS failures remain; a gameplay
speedup is not established. Full changelog and results are on the release page.

Report: https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml
  Device/GPU, Android, Winlator and DXVK versions.
  Game, scene, resolution/settings; session R0/R1/R2/R3 and variables used.
  FPS/stutter, image differences or crash; attach screenshots and DXVK logs.
  Include R0 and the changed session, even if the result is the same.
