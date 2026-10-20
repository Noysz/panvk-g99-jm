# Occlusion-query follow-up

Status: opt-in candidate implemented; correctness and performance gates are
tracked below. This is not a release recommendation.
Source references below are relative to the active Mesa checkout.

## Confirmed source behavior

The preserved `src/panfrost/vulkan/jm/panvk_vX_cmd_query.c` closes the active
batch in `CmdEndQueryIndexedEXT`, then emits an immediate availability write
in a subsequent batch's vertex/tiler/compute chain. This puts the availability
write after the fragment chain that produces the occlusion result.

The kbase queue's `panvk_kbase_submit_locked` puts DATA dependencies between
successive atoms in one submission and ORDER dependencies on the preceding
submission. The existing sequencing is relevant to correctness, not merely
an avoidable CPU stall.

There is an important prerequisite in the preserved `CmdCopyQueryPoolResults`: its WAIT
branch calls `close_batch(cmd, true)`. That helper closes a batch only when
its second argument is false, so this call is a no-op. Existing EndQuery
splitting supplies the ordering today. Removing that split without another
ordering mechanism could place a waiting copy before the fragment writes
or deferred availability update it needs.

`cmd_close_batch` has an early return for content-less batches as well as its
normal fragment path. Any deferred-query design must handle both; simply
appending a flush at the bottom misses empty queries.

## Hardware observations and measurement limits

The first successful Tomb Raider camera pilot visibly renders geometry and
reports continued 848x480 batches with attachment loads/stores. This establishes
that render-pass splitting occurs. It does not attribute every split to a
query or quantify the removable GPU cost. The pilot overlapped a tool build
and is excluded from performance comparisons.

The Winlator wrapper bypasses the driver's present hook. Use the separately
validated Mesa overlay layer for present intervals, and retain driver pass
logs for batch structure. The driver's asynchronous profiler does not provide
per-chain GPU timing. Its GPU-busy interval extends through CPU observation
of completion; it is not a cycle counter.

The completed camera trace now establishes six query ends inside render passes
for each of five fully captured submission intervals: frame labels 1201, 1501,
1502, 1801 and 1802. Each query contains one indexed draw. All sampled query
begins use PRECISE_BIT, which is valid for occlusion queries. The game pool's
creation occurs outside the capture ranges, so its queryType was not directly
observed. The selected submitted buffers contain no secondary execution and
no in-render-pass pipeline barriers. Other causes and other frames remain
outside this attribution.

An independent synchronous-submit diagnostic shows the repeated continued
848x480 group with one draw and one compute job per pass. In thirteen visually
bracketed windows, its median chain wall time is 1.373 ms/pass (range
1.063-2.270); its rounded share of all measured chain wall time is 4-10%.
Those windows were observed at 132-158 seconds after launch. Compiler activity
around 169-176 seconds is excluded. These are serialized submission-to-observed-
completion times, including CPU overhead and actual drawing/attachment work,
not hardware cycle times or fully removable overhead. The diagnostic runner
later terminated with signal 15 before the result dialog, for an unknown
reason. No whole-benchmark result or normal-mode FPS is inferred from it.

This supports a bounded deferred-availability experiment, not a predicted
speedup or permission to discard query ordering. Raw API/pass logs, screenshots,
window selection, source hashes and exclusions are retained in
`evidence/validation/gameplay-2026-10-19/`.

## Initial independent oracle (before the candidate)

`tests/graphics/occlusion_ordering_oracle.c` checks analytically fixed sample
counts 512/0/0/128, exact framebuffer pixels, host and 32/64-bit WAIT copies,
availability, padded strides, empty queries and reset/reuse. Primary and
secondary recordings each run three sequential submissions. Both the preserved
baseline and current candidate pass with zero bad pixels. Each early-copy
control rejects all twelve incomplete query snapshots with exit 2.

The same positive/control matrix passes its expected outcomes on Lavapipe with
the Khronos validation layer inserted and synchronization validation requested;
no validation errors are logged. The native API-dump check also passes: five
recorded query ends become fifteen across three complete submissions, and the
pool is explicitly logged as OCCLUSION. Five trace-analysis tests and nine
gameplay-analysis tests pass.

The initial early-copy control detects unavailable results; it does not inject
an incorrect early availability write into a driver. At that stage,
simultaneous use, inherited active queries, command-buffer reset/destruction
under a deferred design, allocation failures and concurrent statistics queries
were separate gates. The candidate results below record the later coverage and
remaining limits.
The historical `occlusion_query_test.c` is not the acceptance oracle: it omits
the precise feature enable, includes an invalid unreset-query case, and waits
on a reset query. See `tests/graphics/occlusion_ordering_oracle.md`.

## Candidate design and gates

The candidate implements `PANVK_DEFER_OQ=1` on v9. The default is off. An
occlusion query with no active pipeline-statistics query appends an availability
write to a batch owned by its producing batch. Closing the producer appends the
write batch after its fragment work. The empty-batch path also publishes the
write batch. Both batches retain their descriptors for resubmission.

Reset/copy and a subsequent statistics query flush pending availability first.
Event set/reset operations also close it before adding the event operation.
Secondary command buffers use the common API-command replay path, so hardware
batches and their pending state belong to the primary. Reset/free now release
an unclosed current batch as well as any attached availability batch. Query
descriptor allocation and availability-batch allocation set recording errors
on failure. This does not repair every pre-existing allocation-error path in
the driver.

The copy kernel in `libpan/query_pool.cl` reads availability once; it does not
spin on WAIT_BIT. Correctness therefore depends on ordering the copy after
the report and availability writes. The candidate removes the ineffective
`close_batch(cmd, true)` call and explicitly closes pending availability before
all copies, including non-waiting copies.

`PANVK_DEFER_OQ_EARLY_CONTROL=1`, used only with deferral enabled, deliberately
places availability in the producer's VTC chain. The expanded oracle polls
query 0 while work is in flight on a 2048x2048 image. This control exposes an
available value of zero instead of 524288; after completion, the final count
and framebuffer are correct. It demonstrates why checking only final results
does not catch premature availability.

The expanded oracle also tests primary/secondary sequential reuse, repeated
primary submissions with SIMULTANEOUS_USE and explicit memory dependencies,
discarded recordings via reset/pool reset/free/pool destruction, and a failed
allocation during EndQuery through the command pool's allocation callbacks.
Allocation accounting requires zero outstanding callback allocations when the
pool is destroyed. The failure case must return OUT_OF_HOST_MEMORY, then pass
three ordinary submissions after reset. This covers the newly added host
allocation, not exhaustive device-memory allocation failures.

Inherited active queries are not advertised (`inheritedQueries=false`) and
are not enabled by this candidate. The existing concurrent-query failures
remain part of the baseline.

Acceptance gates:

1. Query result/availability visibility, including queries with no covered
   samples or no draws, and pools with multiple queries.
2. Explicit flushes before a WAIT copy, reset/reuse, and any operation that
   consumes results; no waiting copy may block its own prerequisite writes.
3. Secondary command buffers, simultaneous use/resubmission, command-buffer
   reset/destruction, allocation failures and the batch-close early returns.
4. Pipeline-statistics queries sharing the hardware occlusion counter. The
   existing concurrent-query CTS failures remain real failures, not passes.
5. An independent oracle using analytically known covered/occluded samples,
   host availability, GPU copies with WAIT and 32/64-bit result layouts, plus
   a deliberate incorrect-ordering control that the oracle must reject.
6. Baseline/candidate repeated CTS and rendering checks, then normal-mode
   A/B with fewer measured fragment splits and an actual workload benefit.

## Candidate correctness results

Native and Android builds pass. The exact candidate patch stack reproduces
12,928 files with zero differences, excluding six local support files. One
root-document pointer mode differs with identical bytes; source modes remain
strict. Eighty-two oracle processes match
their expected outcomes. These include validation-layer runs, primary and
secondary paths, lifetime tests, pending resubmission, polling, twelve injected
allocation failures followed by successful reuse, and twelve premature-
availability controls rejecting all three early observations per process.

Standalone empty queries outside rendering also pass on the baseline, native
candidate, Android package off/on, and Lavapipe with validation. They exercise
the content-less producer close path directly. A separate callback-accounted
lifetime run leaves one allocation on the preserved baseline; the candidate
leaves zero. This observation is scoped to allocations using that callback.

Fresh CTS baseline: 1,243 unique cases from the query, secondary-command-buffer,
event, basic synchronization, indirect-draw, layered/multiview, simple-draw and
API smoke groups. Candidate off and three candidate-on repetitions all match:
938 pass, 2 fail, 303 not supported; lost=0, gain=0. The two concurrent-query
failures are unchanged, and unsupported cases are not counted as coverage.

## Normal-mode gameplay results

Three alternating off/on pairs use the same packaged Android driver, frozen
runner, initial registry/save state, DXVK 1.7.2 and Hangover 11.16/FEX stack at
848x480. Both shader caches are disabled. UBO push stays at 32 words and
`PANVK_V9_CLIP_KILL_FPK=0`; only `PANVK_DEFER_OQ` changes. Submission remains
asynchronous.

| Pair | Off average FPS | On average FPS | On relative to off |
|---|---:|---:|---:|
| 1 | 13.1 | 13.4 | +2.29% |
| 2 | 13.5 | 13.3 | -1.48% |
| 3 | 12.8 | 16.8 | +31.25% |

These are complete built-in camera benchmark results. Medians are 13.1 FPS
off and 13.4 on (+2.29%). The 16.8 FPS run passes the same completion/state/
stack checks and remains in the results. Its difference from the other two on
runs is unresolved; no repeatable speedup is established. Sampled big-core
frequency has the same 2.2 GHz median in all six processes, but that does not
control GPU clocks, temperature or every background workload.

The secondary present-window medians are 12.964 FPS off / 13.255 on, with
p99 interval medians 143.682 ms / 134.748 ms. This 45-80 second window is
anchored to the menu action, not an exact camera frame. Variable loading
shifts the sampled camera phase; these numbers cannot establish an isolated
frame-time improvement.

All three off logs print the continued 848x480 group with one draw and one
compute job. No `(split)` group is printed in the three on logs, which instead
include merged draw groups. The profiler truncates groups, so this is not
evidence that all splits disappeared, a global pass count or a GPU timing.
Representative scenes and result dialogs were inspected. They show coherent
character and scenery geometry; they do not prove pixel equivalence or cover
every frame.

An earlier on attempt was stopped after a suspicious loading transition and
has no benchmark result. Later images showed a coherent menu, and the off
comparison also showed the same PROFILE transition. A driver regression was
not established. That attempt is preserved separately and excluded from FPS
analysis.

The candidate stays opt-in. The scoped correctness gates pass, but the
repeatable-performance and broader release gates remain open. Evidence,
commands, binary/source hashes, CTS results and exclusions are in
[`evidence/validation/deferred-query-2026-10-19/`](../evidence/validation/deferred-query-2026-10-19/).
