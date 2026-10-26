# JM v9 API candidate

Apply after the upstream `0001`-`0005` stack, the compiler/cache candidate,
the deferred-query candidate, and the 2026-10-23 memory candidate, in that
order. `series` lists this directory's patches. These reproduce the tested
development build; they do not publish test9 or qualify a release candidate.

| Patch | Change |
|---|---|
| 0001 | Cache-sync and queue-error propagation, opt-in pass overlap, and timestamp capability prerequisites |
| 0002 | Keep the timestamp counter active during timestamp jobs; handle multiview, render end and job-index boundaries |
| 0003 | Correct out-of-bounds vertex alpha, carry vertex-input robustness through GPL, and handle null descriptors independently |
| 0004 | Conditional rendering, including restoring an inherited predicate after a secondary command buffer's local conditional block |
| 0005 | Opt-in depth bounds, stored-depth precision and fragment coverage handling |

`0001` preserves the intermediate prerequisites used by the later builds.
The overlap prototype remains incomplete: live-fault resource lifetime and
the unexpected Android cleanup event are unresolved. It is included so the
source stack matches the tested binaries. Do not enable it for test9.
The shared cache-sync and pipeline-state changes also touch code used outside
JM v9; runtime results here cover the G57 device only.

## Defaults and limits

- `PANVK_JM_OVERLAP`, `PANVK_TILER_HEAP_INCREMENTAL`, `PAN_LATE_VECTORIZE`
  and `PANVK_DEFER_OQ` remain off.
- Depth bounds is exposed only with `PANVK_V9_DEPTH_BOUNDS=1`. Leave it off
  unless testing the emulation. AIO advertising it does not prove use of it.
- Timestamp capability uses the existing `PANVK_V9_TIMESTAMP`/driconf
  selection and a successful counter query. Naruto's existing per-app
  selection is retained.
- The 14 tracked TES/transform-feedback failures remain. Large-draw tiler
  exhaustion and broader game/device coverage are still open.

## Validation

The full published chain reconstructs 12,929 compared files with zero
differences. Two local support files are excluded; the existing root-document
pointer mode exception is recorded. See
[source replay](../../evidence/validation/api-2026-10-26/source-replay.json).
This verifies the source stack, not a clean-from-scratch reproducible build.

Host and Android builds completed. Their final 5,658-case tracked regressions
match exactly: 3,754 Pass, 1,890 NotSupported, 14 Fail. The unchanged test8
driver gives 3,753 Pass, 1,890 NotSupported, 15 Fail on the same cases and
native dependencies. One null-vertex case improves; no Pass is lost.

Per-feature controls, repeats, application checks and the short Naruto
battle are recorded in the [evidence](../../evidence/validation/api-2026-10-26/README.md).
No gameplay speedup is established.
