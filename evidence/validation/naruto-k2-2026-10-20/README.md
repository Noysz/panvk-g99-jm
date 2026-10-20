# Naruto title, gameplay and K2 evidence

Measurements recorded on 2026-10-20, Mali-G57 MC2 / Helio G99, using drvB19.
The [report](../../../docs/naruto-gpl-k2-2026-10-20.md) describes controls,
results and exclusions. No new hardware run was made for publication.

`A1` through `B4` retain all eight runs. Primary comparison uses only A2/B2
and A3/B3. Pair 1 has a HUD mismatch; pair 4 changes draw counts during
timing. See `pair1-exclusion.json` and `pair4-exclusion.json`. The intended
three comparable pairs were not obtained.

`draw-A` and `draw-B` contain descriptor audits and images; their capture
instrumentation was separate from the timing runs. `k2-descriptor-pair.json`
records the matched signatures and the 17 changed draw instances.

`title/` contains the GPL/async controls, including the off-ui runner failure
and its manual snapshot verification. The valid decoder uses GPU argument
`90930000`. Raw snapshots, full IOLOG and shader dumps remain local; the
selected audit files do not replace them for a fresh decoder audit.

`freebattle-A/` contains mode/selection images, combat samples and the separate
20-second quiet-window statistics. This was COM vs COM Single Match for about
three minutes, not a completed match or a paired K2 benchmark.
It has no loaded-maps record; runtime identity is recorded in its manifest,
binary pins and process/D3D logs.

## Offline timing check

From this directory:

```sh
python3 analyze.py A1 B1 A2 B2 A3 B3 A4 B4
```

This validates timing, settings, image hashes and preservation records, then
recomputes per-run statistics. It does not make the visual comparison or
decide pair exclusions. Use `performance-summary.json` only with the two
exclusion records. Primary FPS is complete frame count divided by the sum of
the selected frame intervals; `analysis.json` records the CSV hashes.

## Publication

CSV files, images and the offline analyzer are copied byte-for-byte.
JSON is reformatted, and device-home/save-profile directory names in metadata
and logs are replaced by placeholders. Numeric values, control flags, hashes
and failure states are retained. `publication.json` maps each selected file
to its local source identifier and records both source and published hashes.
`SHA256SUMS.json` covers the published files except itself.

Game/driver/wrapper binaries, game-save and registry contents, credentials and
raw GPU memory are not included. Paths inside manifests describe the original
runtime, with the documented normalization; they are not portable launch
commands. Images show the tested 640x360 swapchain inside an 848x480 desktop.
