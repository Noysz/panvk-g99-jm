# Follow-up tasks - 2026-10-19

Target: the existing phase 8-13 and release roadmaps, including correct
rendering, sustained memory use, compatibility, and performance against
Zenithblue, FristOneRR, Faux123 and the Mali blob on comparable workloads.
No task is complete based only on an advertised feature or an FPS claim.

| ID | Status | Task | Completion evidence |
|---|---|---|---|
| T01 | Done | Preserve the active source and binaries; replace the patch-chain gate | Twelve gate tests pass; final candidate stack matches 12,928 files, with six explicit support-file exclusions and one documented root-pointer mode difference. Source and binaries preserved. |
| T02 | Done for audited controls | Audit shader cache keys and isolate compiler controls | Three cache cycles separate K2 controls and preserve warm reuse; baseline demonstrates missing separation. Final Android default-cache checks also pass in three cycles. |
| T03 | Done: candidate and targeted gates | Stage public compiler/cache backports individually | Pinned 152/162/163/170/171, host/Android builds, three 2,900-case CTS runs, default-cache cycles and DXVK render/control checks pass at their documented scope. Game performance remains T04. |
| T04 | Partial: TR/K4 measured; Naruto descriptors and short battle checked | Establish comparable gameplay baselines and validate K2/K4 | Naruto renders at 640x360 with GPL=True, async=False. K2 changes 17 matched draw instances. Two comparable A/B pairs show no gain; two pairs are excluded for HUD/draw differences. The three-pair target remains unmet. A three-minute COM single battle is visually sampled. |
| T05 | Candidate evaluated; repeatable performance benefit pending | Profile occlusion splits and implement deferred availability if justified | Ordering/lifetime/allocation controls and three 1,243-case CTS repeats match the baseline. Six camera runs give median 13.4 FPS on / 13.1 off, with one unusually fast on run. Default remains off. |
| T06 | Pending | Investigate incremental tiler backing and import coherency | Kernel-matched behavior, memory and sync costs, long-session correctness |
| T07 | Pending | Investigate register pressure, dynamic matrix vectorization and compute TLS | G57 reproducer, ISA/spill evidence, bounded memory cost and workload gain |
| T08 | Pending | Evaluate safe overlap between passes | In-flight heap/TLS/WLS/arena ownership, ordering tests, measured overlap, no GPU faults |
| T09 | Pending | Continue timestamp API, robustness, conditional rendering and opt-in depth bounds | Per-feature implementation and running CTS; capability bits alone do not qualify |
| T10 | Pending | Produce a release candidate only after the roadmap gates | Reproducible stack/build, regression/control runs, game validation, documented remaining failures |

Execution starts with T01-T03. Later tasks remain explicit backlog until their
dependencies and workload evidence are available. Existing local changes and
validated driver packages are preserved. Candidate patches do not imply a
release or a measured performance improvement.

## Initial state

- Documentation repository: `5c51c56ce9286a6db2cdd67a0f8ca2f697b72c84`.
- Active Mesa HEAD: `74915219da09017ec83fb0a728294782bd82a68c`, with existing
  tracked modifications and local support files.
- Published upstream stack: `0001` through `0005`, based on
  `feeadb5f55f4ba14a5c5ddc84ba20177e419cffb`.
- No new hardware results at task creation.

## Execution evidence

Local artifacts: `~/panvk-refs/followup-2026-10-19/`.

- Preserved `active-before.patch`, source hashes, and both host/Android driver
  binaries before source changes.
- `tools/verify_patch_chain.py` replaces the touched-files-only gate. It checks
  all expected files and Git-visible additions, honors checkout line endings,
  records explicit exclusions, and returns nonzero on mismatch or apply error.
  Nine positive/negative tests passed, including a change outside the patch.
  `candidate-chain.json` reports zero differences across 12,929 compared files.
  Exclusions are the existing merge note, standalone snapshot decoder, and
  three host-tool symlinks; they are not part of the driver source stack.
- `tools/verify_shader_cache.py` launches independent Vulkan processes against
  a new cache per repetition. The existing biner reuses all six entries after
  changing `PANVK_V9_CLIP_KILL_FPK`; the corrected biner has three misses on
  that transition, then six hits on the warm repetition. Both behaviors were
  reproduced three times. Rendering passed in every tested process.
- The cache test also checks `false` and `0` equivalence and the explicit cache
  disable override. It establishes key separation, not an FPK/FPS improvement.
- The new state key includes compiler flags and effective shader-control
  booleans; UBO budget and clip/cull state are always hashed. The deliberate
  `PANVK_V9_EMU_NO_HASH` negative control remains available.
- Both Vulkan targets build. The final Android binary passes 18 process checks
  with no explicit cache-enable override: three repetitions of cold/warm,
  control transition, default return, and explicit cache disable.
- Compiler regression: 2,900 unique cases in each of three candidate runs;
  2,343 pass, 6 fail, 551 not supported. Every status matches the freshly run
  preserved baseline: `lost=0`, `gain=0`, no missing or extra cases.
  The six old failures concern transform-feedback depth clip from TES.
- Candidate package: `~/wltest/drvB19_cache_compiler/`. The linked and packaged
  Android binaries share Build ID `0ba47c48e7eaed58d0ceff16127c0e954612ba02`.
  Packaged driver SHA-256:
  `d77c3c46ecd24889b34f1fe1ebd80f284622be729673c568ce1e62c244dbd114`.
- Packaged Winlator-stack DXVK 2.3.1: three render/readback runs pass with
  `checked=6 bad=0`; the deliberate wrong-pixel control fails with `bad=6`.
  `as-expected=4` refers to those four runs of one DXVK version.

Reviewable patches: `patches-candidates/2026-10-19/`.
Compact evidence: `evidence/validation/compiler-cache-2026-10-19/`.

T01-T03 are complete at the scope above. T04 has a measured TR/K4 baseline and
the limited Naruto results described below;
T05 has an evaluated opt-in deferred candidate, and T06-T10 remain pending. No
repeatable gameplay speedup, victory over a rival/blob, full CTS conformance, or test9 readiness is
claimed. See `evidence/validation/gameplay-2026-10-19/README.md` and
`docs/occlusion-followup-2026-10-19.md` for the new evidence and remaining gates.

## Gameplay/query execution

- Six alternating camera benchmarks establish no repeatable K4 improvement:
  median game average 13.0 FPS on / 13.3 off; median interval-window rate
  13.015 on / 12.965 off. The default remains unchanged.
- Five fully captured API submission intervals contain six precise query
  ends inside render passes, with one indexed draw per query. A separate
  synchronous diagnostic measures the small continued-pass group, but ends
  prematurely with signal 15; only the earlier selected windows are retained.
- The new analytical query oracle passes primary and secondary recordings on
  both preserved baseline and candidate. Three sequential submissions per
  process cover exact pixels/counts, host reads, 32/64-bit copies, availability
  and reset/reuse. Four hardware negative-control processes each reject twelve
  early snapshots. Lavapipe/validation-layer checks agree.
- Fourteen analysis tests pass. Automation now verifies the selected benchmark
  title; diagnostic/manual/excluded runs cannot enter normal-mode analysis.
  The six original numerical results are unchanged. Their frozen runner is
  preserved, and its action anchor is documented as approximate, not frame-exact.
- Seven original registry/save hashes and 68 source hashes remain unchanged.

T05 candidate artifacts: `~/panvk-refs/deferred-query-2026-10-19/` and
`patches-candidates/2026-10-19-deferred-query/`. It retains availability writes
after the producing fragment batch, with explicit consumer/event handling and
cleanup for abandoned recordings. `PANVK_DEFER_OQ` is off by default. Eighty-two
oracle processes meet their expected outcomes, including twelve deliberate
premature-availability rejections and twelve injected allocation failures that
recover after reset. Eleven gameplay-analysis tests pass. Patch replay compares
12,928 files with zero differences after removing local root-document metadata
from the checkpoint. Six support files are excluded, and one root-document
pointer has identical bytes but a different checkout mode. Twelve gate tests
cover this explicit exception; source contents and modes remain strict.
Fresh baseline, candidate off, and three
candidate-on CTS runs each have 938 pass, 2 fail and 303 not supported across
1,243 unique cases. Every status matches; the two concurrent-query failures
remain failures. Standalone empty-query checks pass on baseline, candidate,
packaged Android and Lavapipe/validation. The baseline leaves one allocation
in the callback-accounted abandoned-recording test; the candidate leaves zero.
The six normal-mode camera runs report off 13.1/13.5/12.8 FPS and on
13.4/13.3/16.8 FPS. All use the same package, runner, initial state and shared
controls. The isolated 16.8 FPS result is retained; its cause is unresolved.
The three pairs do not establish a repeatable benefit, so the default remains
off. Selected screenshots show coherent geometry; they are not matched frames
or proof of correctness throughout the camera path. The present-window anchor
also shifts with loading, so the complete game benchmark is the primary metric.
Final preservation checks match all 69 source hashes, nine build artifacts,
seven original registry/save files and three previously validated packages.
Compact evidence: `evidence/validation/deferred-query-2026-10-19/`.

## Naruto setup (2026-10-19 pilot)

A dedicated Proton 10 arm64ec prefix and DXVK 2.3.1-gplasync reach the initial
village after the intro and tutorials. A recorded movement input changes the
character's orientation. The original 30 registry/user-state/game files keep
their hashes; the new village save is private and isolated. This pilot overlaps
source verification and is not performance evidence. DXVK recreates the
640x360 startup swapchain at 640x480, which must be stabilized before A/B.
Combat, affected K2 draw-path evidence and repeated K2 measurements were still
pending at this checkpoint. See
`evidence/validation/naruto-gameplay-setup-2026-10-19/`.

## Naruto follow-up (2026-10-20)

Forcing GPL in the installed DXVK 2.3.1-gplasync package omitted title draws.
Setting `dxvk.enableAsync = False` restores the missing background/panels while
GPL and descriptor varyings remain active. The earlier black title does not
establish a PanVK varying bug. The packed-varying probes were already complete
and were not repeated for the gameplay measurements.

The village captures contain 143 draws each. Fifteen matched shader/state
groups, representing 17 draw instances, change only `Allow forward pixel to
kill` under K2. This confirms the descriptor path; it does not establish a
whole-frame performance improvement.

Eight 90-second runs use the same frozen initial state at 640x360, GPL=True,
async=False, UBO push=0 and deferred occlusion=0. A1/B1 are excluded because
A1's HUD differs. The single replacement pair A4/B4 is also excluded because
B4 changes draw counts during timing despite complete endpoint images.
A2/B2 and A3/B3 remain comparable: K2-active FPS is lower by 1.074% and 7.776%.
The two-run medians are 10.068 FPS active and 10.530 control. The intended
three comparable pairs were not obtained. Ordering, animation and clock
variation prevent treating this as a general regression result.

Free Battle uses COM vs COM, Single Match and in-game random selection:
Kakashi versus Shino, with a random arena. About three minutes of images show
characters, scenery, HUD and several combat effects before stopping without
a KO. A separate 20-second interval without screenshots records 14.082 FPS;
CPU/GPU bottlenecks have not been isolated.

Details and all exclusions: [Naruto report](naruto-gpl-k2-2026-10-20.md).
Selected evidence: `evidence/validation/naruto-k2-2026-10-20/`.
Next, explain the transient HUD/draw changes and profile the remaining frame
cost with complete rendering. T04 remains partial; T06-T10 keep their existing
order. BC and wrapper changes remain deferred. The
[FristOneRR 3.0 audit](fristonerr-3.0-audit.md) is supporting research.
