# Follow-up tasks - 2026-10-19

Target: the existing phase 8-13 and release roadmaps, including correct
rendering, sustained memory use, compatibility, and performance on
comparable workloads.
No task is complete based only on an advertised feature or an FPS claim.

| ID | Status | Task | Completion evidence |
|---|---|---|---|
| T01 | Done | Preserve the active source and binaries; replace the patch-chain gate | Twelve gate tests pass; final candidate stack matches 12,928 files, with six explicit support-file exclusions and one documented root-pointer mode difference. Source and binaries preserved. |
| T02 | Done for audited controls | Audit shader cache keys and isolate compiler controls | Three cache cycles separate K2 controls and preserve warm reuse; baseline demonstrates missing separation. Final Android default-cache checks also pass in three cycles. |
| T03 | Done: candidate and targeted gates | Stage public compiler/cache backports individually | Pinned 152/162/163/170/171, host/Android builds, three 2,900-case CTS runs, default-cache cycles and DXVK render/control checks pass at their documented scope. Game performance remains T04. |
| T04 | Partial: TR/K4 measured; Naruto descriptors and short battle checked | Establish comparable gameplay baselines and validate K2/K4 | Naruto renders at 640x360 with GPL=True, async=False. K2 changes 17 matched draw instances. Two comparable A/B pairs show no gain; two pairs are excluded for HUD/draw differences. The three-pair target remains unmet. A three-minute COM single battle is visually sampled. |
| T05 | Candidate evaluated; repeatable performance benefit pending | Profile occlusion splits and implement deferred availability if justified | Ordering/lifetime/allocation controls and three 1,243-case CTS repeats match the baseline. Six camera runs give median 13.4 FPS on / 13.1 off, with one unusually fast on run. Default remains off. |
| T06 | Candidate validated at targeted scope; long sessions pending | Investigate incremental tiler backing and import coherency | Six 929-case runs: 753 Pass / 174 NS / 2 existing Fail, gain 70 / lost 0. Android visibility, tiler and lifecycle checks pass. Incremental backing stays off; no measured backing reduction. Long-run timeout remains unresolved. |
| T07 | Matrix path tested; register pressure and game coverage pending | Investigate register pressure, dynamic matrix vectorization and compute TLS | Existing late vectorization passes three enabled 2,900-case repeats against baseline. Row-major submit-to-fence reduction 13.19-18.45% across three ABBA blocks; uncontrolled clocks/temperature, no FPS claim. Default stays off. |
| T08 | Prototype; live-fault lifetime unresolved | Evaluate safe overlap between passes | Preserved in the combined candidate, default off. Nonfaulting controls do not close live-fault resource lifetime; Android cleanup event remains unexplained. |
| T09 | Targeted implementation and combined validation complete; broader qualification open | Continue timestamp API, robustness, conditional rendering and opt-in depth bounds | Timestamp, vertex/GPL/null, conditional and depth-bounds controls pass at documented scope. Host/Android feature repeats complete. Bounds stays opt-in. |
| T10 | Combined candidate checked and test9 published; RC gates open | Produce a release candidate only after the roadmap gates | Published source replay passes. Final host/Android tracked regression gains one Pass against test8, loses none; 14 old failures remain. Extracted-package loader/probe/render controls pass at recorded scope. Downloaded release assets match prepared hashes. No RC qualification. |

Execution starts with T01-T03. Later tasks remain explicit backlog until their
dependencies and workload evidence are available. Existing local changes and
validated driver packages are preserved. Candidate patches do not imply a
release or a measured performance improvement.

The table includes the October 28 test9 publication. Dated sections
below retain the scope and outstanding work at their original checkpoint.

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
T05 has an evaluated opt-in deferred candidate. T06 has a tested memory
candidate with sustained validation pending. T07 has matrix-path evidence;
register-pressure work and representative workload checks remain open. T08-T10
retain their roadmap gates. No
repeatable gameplay speedup, full CTS conformance, or test9 readiness is
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
order. BC and wrapper changes remain deferred.

## Memory follow-up (2026-10-23)

T06 now has a [candidate patch](../patches-candidates/2026-10-23-memory/README.md)
and [hardware evidence](../evidence/validation/memory-2026-10-23/README.md).
Cached dma-buf properties and explicit cache flush behavior are corrected for
the tested JM v9 path. The incremental tiler option remains off by default.
Six full selected-case repeats match their expected results; Android controls
check data, pixels and resource cleanup. No tiler backing saving was observed.
The frozen build's long cached-import attempt timed out after about 1,110
seconds and has no final correctness/cleanup verdict. Its cause is unresolved.
Cache-sync error propagation, sustained graphics and game gates remain open.

## Matrix follow-up (2026-10-23)

T07 evaluates the existing `PAN_LATE_VECTORIZE` option; it adds no driver
patch. One baseline and three enabled 2,900-case CTS runs each report
2,343 Pass, 551 NotSupported and six existing Fail, with no per-case changes.
The Android matrix workload passes its data/guard checks in 24 ordinary
processes, and its wrong-reference control fails as expected.

Three ABBA blocks measure row-major submit-to-fence reductions of 13.19%,
15.88% and 18.45%. The column-major control changes by 1.95%, -0.19% and
2.69%. Clock frequency and temperature are uncontrolled. The row-major shader
uses fewer code bytes but more work registers; this does not establish reduced
register pressure or improved gameplay. A separate SSA spill-target experiment
was rejected and is absent from the patch stack. The option remains off.

See the [matrix evidence](../evidence/validation/matrix-2026-10-23/README.md)
and [reproducers](../tests/graphics/t07-matrix/README.md). T08 still needs safe
resource ownership and measured overlap. T09 covers the remaining API features;
T10 is the release-candidate gate, including full regression and game checks.

## API and combined build (2026-10-26)

The [API candidate](../patches-candidates/2026-10-26-api/README.md) preserves
the final tested stack, including cache-sync/queue-error prerequisites and
the disabled overlap prototype. It does not promote incomplete T06-T08 work.

Timestamp API/lifetime, vertex-input robustness and GPL, conditional predicate
inheritance, and opt-in depth bounds now have targeted hardware tests with
repeats and negative controls. Final source replay and both builds complete.
The combined host/Android 5,658-case results match exactly: 3,754 Pass,
1,890 NotSupported, 14 Fail. The released test8 driver on the same cases and
native dependencies has one additional failure; no Pass is lost.

Four DXVK versions pass the recorded triangle/readback and AIO checks. AIO's
black rectangles are reproduced as capture occlusion by separate GDI windows
in a paired desktop/render-window diagnostic. One 180-second Naruto COM vs COM
Single Match shows combat throughout the sampled images and no detected
GPU-fault lines. These are native Wine/Termux:X11 results, not a controlled
FPS comparison or a 30-minute game matrix.

The [evidence](../evidence/validation/api-2026-10-26/README.md) distinguishes
earlier feature builds from final combined runs. The
[test9 checklist](test9-readiness.md) has two remaining package steps.
The RC/1.0 roadmap remains unmet; T10 is not complete.

## Test9 package and instructions (2026-10-28)

The frozen Android library and tested launcher dependencies are packaged in
a new local test9 ZIP. It follows the earlier release layout with the same
README inside and beside the ZIP, plus checksums. Ten extracted members
match their inputs. Six loader/wrapper/profile checks and one enumeration
control give expected results; DXVK readback passes six checks and the
wrong-reference control detects all six mismatches. All 540 historical
preservation pins remain unchanged.

The [testing README](test9/README-v0.1.0-test9.txt) documents R0-R6, T06-T10
environment settings, the existing baseline/logging controls and issue-report
fields. Unrelated backend options and fault-injection switches are omitted.
The [build/package order](test9/build-order.md) preserves the patch order,
binary/dependency identities and acceptance checks. The environment audit
corrects the UBO push default and presence-based FPK control instructions.
The current ZIP changes only the README; all nine other members match the
smoke-tested package. Its archive checks and hashes are recorded separately.

See [package evidence](../evidence/validation/test9-package-2026-10-28/README.md).
These are package smoke checks on the reconstructed stack. No new game
benchmark, actual Winlator app install, source change or release publication
is claimed. RC qualification and the broader T06-T08 work remain open.

## Test9 publication (2026-10-28)

The prepared test9 ZIP, scoped README and versioned checksum file are now
attached to the [test-build prerelease](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-tests).
All three assets were downloaded again and match their prepared hashes.
The 17 historical assets and shared tag remain unchanged. Publication is
recorded separately in
[publication.json](../evidence/validation/test9-package-2026-10-28/publication.json);
earlier preparation manifests keep their original status. Source, library
bytes, runtime evidence and the open RC gates are unchanged.
