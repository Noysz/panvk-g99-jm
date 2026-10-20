# Tomb Raider benchmark and query profiling

K4 measurement complete for this workload; query attribution and the initial
ordering oracle are established. Deferred availability is not implemented.
Hardware: Infinix Note 40 / Helio G99 / Mali-G57 MC2,
kbase JM. Local artifacts are in
`~/panvk-refs/gameplay-2026-10-19/`.

## Scope and controls

- Candidate: `drvB19_cache_compiler`, SHA-256
  `d77c3c46ecd24889b34f1fe1ebd80f284622be729673c568ce1e62c244dbd114`.
- Hangover 11.16 / FEX arm64ec / DXVK 1.7.2 / the existing Winlator Vulkan
  wrapper. Render target and X display: 848 x 480. Graphics settings and
  saves are restored from one hashed snapshot before every run.
- K4 control: the same binary, `PANVK_UBO_PUSH=32` versus `0`.
  Mesa disk cache and DXVK state cache are disabled in both conditions.
  OS file caches and emulator caches are not flushed.
- The scene is the game's built-in camera benchmark, launched through its
  menu. It is a repeatable game workload, not player-controlled gameplay.
  K2 is not established by this workload: the application profile normally
  disables clip/cull-distance emulation.
- A dedicated copy of the Wine prefix protects the original registry/save
  state. The runner refuses the original prefix and never reuses an output
  directory. Individual manifests record binaries, settings, actions and
  screenshots. No driver package is replaced.

## Measuring presents through the wrapper

`PANVK_FRAME_PROF` reports `no presents seen` in this stack, even while the
game visibly renders. Its batch/pass counts remain useful; its output must
not be treated as gameplay FPS. GPU busy describes submission through
observed completion, not hardware cycle utilization. `(split)` identifies a
continued render pass, not its cause. Asynchronous groups have no measured
per-chain GPU time.

The unmodified Mesa overlay layer is built separately from the same source,
with `no_display=1`, no GPU timing or pipeline-statistics queries, and
`fps_sampling_period=0`. It logs present-call intervals and API draw/submit
counts above the wrapper. Layer SHA-256:
`18bc159f5d916fac1616c367acbb12b016aedbb7d88204290ddbd4dd53ad390b`.

A 180-frame D3D11 triangle/readback test with this layer passes all six pixel
checks (`bad=0`). The final CSV contains 178 rows accounting for 179 presents:
the swapchain is recreated after the first rendered frame, and the first CSV
row groups two presents. Subsequent rows contain individual intervals. The
analysis checks this distinction rather than equating lines with frames.

The layer writes a leading empty CSV field when the device field is disabled.
`tools/analyze_gameplay.py` handles that format, rejects missing/incomplete
runs and grouped frames within the selected window, and checks driver/state
identity. Visual review remains separate and mandatory.

The interval window is 45-80 seconds after the recorded benchmark action,
anchored to the last logged present at that point. The frozen runner records
this before moving the pointer and waiting 0.3 seconds, then holds Return for
0.4 seconds. Input recognition and the interval boundary add uncertainty;
this is not a frame-exact scene anchor. Screenshots establish that the window contains
the camera scene. These statistics are not whole-benchmark percentiles,
scanout latency or GPU execution times. The game's result dialog is retained
as a separate whole-camera-path statistic. Its OCR is checked against the
image; unreadable values fail analysis. An initial 45-95 second proposal was
shortened after the first on-run and before any off-run, because the final
15 seconds approach the result dialog. The retained window is inside the
visually observed camera scene; the choice does not use an on/off effect.

CPU cluster frequencies are sampled. Temperature and GPU frequency are not
controlled or measured. Alternating repetitions help expose drift but do
not establish long-session thermal performance or superiority over a rival.
No builds or competing GPU tests run during the measured repetitions.
Background userspace activity is not fully isolated, and the screenshot/pass
logging instrumentation remains enabled in both conditions. Small percentile
differences should not be interpreted as a new driver stutter regression.

## Setup experiments excluded from performance results

- `pilot-k4-on`: started before the prefix copy finished; stopped and the
  copy repaired. No benchmark result.
- `pilot2-k4-on`: command-line benchmark produced narrow rendering targets
  and vertical strips. No comparable result; not used as a performance run.
- `pilot3-menu`: the earlier experiment had altered the copied save profile;
  it reached the game's first-run agreement. The original profile was then
  restored from a new immutable snapshot.
- `pilot4-state`: successfully rendered the menu and full benchmark camera
  path. A background layer build and manual input make it unsuitable for
  performance claims. Its result image supplies the completion detector.
  The detector rejects all pre-result pilot screenshots and recognizes the
  two final result screenshots. Only header/button pixels are matched;
  result numbers are parsed and retained separately.
- `k4-on-1`: automation failed to resolve a Wine window name before the
  benchmark started. Excluded. Input now uses the established pointer/input
  path with a held key, as in the successful pilot.

## K4 results

Execution order: on1, off1, on2, off2, on3, off3, with a 30-second pause
before each subsequent process. Every run reaches the benchmark result dialog.
Its values were checked visually against the retained screenshots. The scene
images show the same camera-path phase with geometry/textures present in both
conditions; they are not pixel-identical captures or an external rendering oracle.

| Run | UBO words | Game average FPS | Window FPS | Window p99 ms |
|---|---:|---:|---:|---:|
| on1 | 32 | 12.9 | 12.762 | 156.066 |
| off1 | 0 | 13.3 | 13.260 | 128.208 |
| on2 | 32 | 13.3 | 13.437 | 131.941 |
| off2 | 0 | 13.1 | 12.548 | 194.981 |
| on3 | 32 | 13.0 | 13.015 | 142.771 |
| off3 | 0 | 13.3 | 12.965 | 130.567 |

Median game average: on **13.0**, off **13.3** FPS. Median window rate:
on **13.015**, off **12.965** FPS. These two measures differ in scope and
point in different directions. The paired whole-benchmark effect also changes
sign. This does **not establish a repeatable K4 performance improvement** in
this workload, nor a robust global preference for disabling K4. The earlier
approximately 29% menu result must not be presented as a gameplay gain.

The control changes real submitted work: continued 848x480 pass groups report
one compute job each with K4 on and zero with it off. This matches the source's
per-batch UBO gathering path. It motivates investigating the cost of gathering
in fragmented passes, but does not measure the removable query cost by itself.

`ab-analysis.json` contains the complete numerical results and identity checks.
Each run directory includes a manifest, compressed present/pass/process logs,
a selected scene, selection provenance, the result image and its OCR. The
comparison sheets provide a quick visual review. Original screenshots remain
in the local artifact directory.

Nine analysis-gate tests pass (`test_analyze_gameplay.py`), including malformed
CSV, unfinished benchmarks, reset logs, grouped frames, truncated windows and
wrong-resolution rejection, explicit exclusions/manual steering, and rejection
of API-dump/synchronous diagnostics as normal-mode measurements.
The 68 source checkpoint hashes still match, with
no new tracked Mesa modifications. The preceding compiler/cache CTS results
remain separate; no driver source/default is changed by this measurement batch.

K2 still needs a workload using the affected clip/GPL path. Query optimization,
long-session thermal behavior and equivalent-rendering rival/blob comparisons
remain open tasks.

## Query attribution

`query-api-scene3` completes the camera benchmark with both the unmodified
overlay and `VK_LAYER_LUNARG_api_dump` enabled. API capture ranges are
0-2,1200-2,1500-3,1800-3, using start-count syntax. This is a diagnostic run;
its FPS is excluded from normal-mode comparisons.

The parser follows observed command-buffer Begin/End pairs across adjacent
captured frames, clears coverage across gaps, rejects unknown secondary
execution and checks submitted buffer counts. Five frame labels have every
submitted command buffer completely captured:

| API frame | Complete buffers submitted | Indexed draws | Query ends inside render passes |
|---|---:|---:|---:|
| 1201 | 6 | 348 | 6 |
| 1501 | 8 | 836 | 6 |
| 1502 | 11 | 828 | 6 |
| 1801 | 8 | 393 | 6 |
| 1802 | 7 | 396 | 6 |

All 48 sampled query begins use PRECISE_BIT, indicating occlusion queries under
valid Vulkan usage. The game pool's creation is outside the capture ranges;
its queryType is not directly logged. Each query encloses one indexed draw.
No secondary execution or in-render-pass pipeline barrier occurs in the
selected complete submitted buffers. This establishes a concrete EndQuery
split trigger in the active driver, not that every pass split has this cause.

Screenshot/present-count brackets place labels 1200/1201 between the 105/120 s
captures, labels 1500-1502 between 135/150 s, and labels 1800-1802 between
150/165 s. These are scene brackets, not exact screenshot-to-frame matches.
The retained images show the 3D camera path; the final dialog confirms completion.
`query-attribution.json`, `api-scene3-analysis.json`, `query-draw-counts.json`
and the compressed API trace preserve the underlying evidence.

Excluded profiling attempts are retained as small manifests with explicit
reasons: the initial ranges captured loading/UI; one run selected the wrong
menu item; another failed OCR before activation. No absence-of-query conclusion
is drawn from those attempts. The final automation checks the selected menu's
START BENCHMARK title before Return. The six K4 measurement runs used the
separately preserved `measurement_runner.py` and all reached a result dialog.

## Serialized pass diagnostic

`query-sync-profile` enables `PANVK_KBASE_SYNC_SUBMIT=1`. The group printed as
`848x480 8rt+zs, load 3, store 3, afbc 1 (split)` contains one draw and one
compute job per pass. The `8rt` label counts layout slots; it does not establish
eight active color targets.

Thirteen windows first observed at 132-158 seconds after launch give a median
of **1.373 ms/pass** for the sum of VTC and fragment chain wall time, range
1.063-2.270 ms. The group's rounded share of all measured chain wall time is
4-10%. Screenshots at 135/150 seconds bracket the camera scene. A one-second
observer records when profiler headers become visible; these are approximate
wall-clock brackets. Compiler activity around 169-176 seconds is excluded.
The calculation uses printed, rounded rates; see `query-sync-profile/split-cost.json`.

These timings include submission/CPU observation overhead and useful rendering,
gathering and attachment work. They are not hardware execution timestamps and
are not wholly removable cost. API and timing runs are separate, so the group
is not matched to individual query calls. Normal-mode A/B remains required.

The diagnostic runner and observer later terminated with signal 15 before a
result dialog, for a cause not established. No game processes remained on
inspection. The original partial manifest is preserved; the final manifest
records the interruption. This is a partial diagnostic, not a completed
benchmark or a stability pass. Only the earlier observed windows are analyzed.

## Query oracle and tooling checks

`tests/graphics/occlusion_ordering_oracle.c` establishes analytical query counts
512/0/0/128 and verifies every pixel. It checks host reads, 32/64-bit GPU copies
with WAIT, availability, padding, empty queries, reset/reuse and three sequential
submissions of primary or secondary recordings.

Both the preserved baseline and candidate pass both recording modes. Four
hardware early-copy controls each reject twelve incomplete snapshots with
exit 2. Four matching Lavapipe runs produce the expected outcomes with the
Khronos validation layer loaded and synchronization validation requested;
no validation errors are logged. Simultaneous use and inherited active queries
are outside this test. The negative control does not inject premature
availability into a driver.

The native API-layer oracle run passes and records an OCCLUSION pool. Its five
recorded query ends become fifteen across three complete submissions, checking
the trace parser against known resubmission behavior. API-layer D3D11 readback
and wrong-pixel controls also have their expected outcomes. Five trace-parser
tests and nine gameplay-analysis tests pass.

The oracle sources, shader build commands, binary/source hashes, process logs
and controls are retained. See `occlusion-oracle/` and
`tests/graphics/occlusion_ordering_oracle.md`. The older test's feature-enable,
unreset-query and WAIT-after-reset problems are documented there; it is not
the acceptance gate for deferred availability.

`preservation-final.json` confirms all seven original registry/save hashes and
all 68 source checkpoint hashes still match. `SHA256SUMS.json` covers compact
evidence files. No build products, game saves or registry contents are included.
