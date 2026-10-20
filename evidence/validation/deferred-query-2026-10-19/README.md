# Deferred occlusion availability, 2026-10-19

Opt-in v9/JM candidate on Mali-G57 MC2, Helio G99, kbase r54p1/UAPI 11.46.
`PANVK_DEFER_OQ` remains off by default. This evidence does not clear a release.

## Correctness

- Native and Android builds complete. `build-manifest.json` identifies the
  preserved and candidate binaries and their source hashes.
- Patch replay matches 12,928 files, with six explicit support-file exclusions.
  One root-document pointer has identical bytes but a regular-file/symlink
  mode difference. Source modes remain strict. See `patch-chain.json`.
- `oracle/final/runs.json`: 82 processes meet expected outcomes across
  baseline/candidate, native/Android, primary/secondary, sequential/pending
  reuse, abandoned recordings, polling and allocation failure.
- Twelve premature-availability controls reject all three first-availability
  snapshots per process, even though final query counts and pixels are correct.
  Twelve allocation-failure processes recover and finish with no outstanding
  command-pool callback allocations. This is not a general memory-leak test.
- `oracle/empty/`: five standalone empty-query runs pass, including Lavapipe
  with the validation layer. The separate baseline lifetime check leaves one
  callback allocation; the candidate leaves zero.
- Fresh baseline, candidate off and three candidate-on CTS runs each have
  938 Pass, 2 Fail, 303 NotSupported across 1,243 unique cases. Every status
  matches. Both `query_pool.concurrent_queries` failures remain. Unsupported
  timestamp cases do not exercise timestamps.

The final matrix uses the frozen C file in `oracle/final/`. The later
`--empty-outside` supplement uses the file and build manifest in `oracle/empty/`.
See the [oracle instructions](../../../tests/graphics/occlusion_ordering_oracle.md)
for build/run commands and limitations.

`publication-cleanup.json` records the final replay command after local root
documentation metadata was removed from the checkpoint patch. Its three
driver-source sections are unchanged. Twelve patch-gate tests pass, including
rejection of pointer-content changes and mode changes outside root documents.

## Gameplay

Same packaged driver, frozen runner, registry/save hashes, DXVK 1.7.2,
Hangover 11.16/FEX and Winlator wrapper, 848x480. Both caches disabled,
UBO push 32, clip-kill control 0, normal asynchronous submission. Only
`PANVK_DEFER_OQ` changes. The order was declared before the first off result.

| Pair | Off FPS | On FPS | Change |
|---|---:|---:|---:|
| 1 | 13.1 | 13.4 | +2.29% |
| 2 | 13.5 | 13.3 | -1.48% |
| 3 | 12.8 | 16.8 | +31.25% |

The primary metric is the complete built-in camera benchmark. Median game
average is 13.1 FPS off / 13.4 on. The 16.8 FPS result is retained and its
cause remains unresolved. A repeatable benefit is not established.

`normal-analysis.json` validates all six runs and holds present-window
statistics. `normal-summary.json` holds paired changes and medians. The
45-80 second present window uses an approximate menu-action anchor; loading
changes the sampled camera phase. It is not frame-exact or a GPU timing.

The pass logs show the continued one-draw group in off runs and merged groups
in on runs. `pass-group-summary.json` counts printed rows only. Top-group
truncation prevents inferring a global split count or that no splits remain.

Selected scenes and result dialogs show coherent character/scenery geometry.
They are not pixel-equivalence checks. `visual-review.json` records the review.
The earlier interrupted attempt under `excluded/` has no result and does not
enter the six-run analysis. Its suspicious loading transition did not establish
a driver regression.

## Reproduction and preservation

The scripts retain exact invocations and local paths. `measurement_runner.py`
is the frozen runner used for every accepted game run. `normal-gameplay-runs.json`
records successful runner exits. The enclosing exec session later reported
143; all six per-run manifests and results were already complete. This does
not replace the per-run completion checks.

`preservation-final.json` checks 69 source files, nine build artifacts, seven
original registry/save files and three validated driver packages: 88 matches,
zero mismatches. Originals remain outside this directory. No game saves,
registry contents, executable driver/test binaries or credentials are included.
`SHA256SUMS.json` covers every retained evidence file except itself.

The [implementation notes](../../../docs/occlusion-followup-2026-10-19.md)
describe ownership, consumer/event ordering and remaining gates. Full release
regression, broader game coverage and comparison with rivals/blob remain open.
