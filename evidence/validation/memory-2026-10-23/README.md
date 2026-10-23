# T06 memory validation

Device: Mali-G57 MC2, kbase JM r54p1, UAPI 11.46. The candidate is
[`2026-10-23-memory`](../../../patches-candidates/2026-10-23-memory/README.md),
applied after the previously published candidate stack. Build hashes and the
five changed source hashes are in `build.json`.

## Recorded results

VERIFIED-HW: six 929-case CTS runs, three with incremental tiler backing off
and three on, each produce 753 Pass, 174 NotSupported and two Fail. Compared
with the preserved pre-T06 baseline, 70 cases gain Pass and none lose it.
`cts-results.json`, `cts/` and `cts-baseline-results.tsv` retain every status.
Raw QPA status and execution order match each TSV; `qpa-pins.json` identifies
the larger QPA files retained locally.

Android visibility covers eleven allocation/import combinations in three
repetitions: 27 data passes and six correctly rejected unsupported requests.
The wrong-reference control reports 32 copy mismatches, with fill and tail
checks intact. The probe checks CPU-to-GPU copy, GPU-to-CPU fill and partial
invalidation that preserves a dirty CPU tail. Raw logs are in `visibility/`.

The 27 ordinary Android tiler processes cover small draws, growth and repeated
submission on the baseline and both candidate modes. All checked non-edge
pixels pass. The wrong-angle control reports 67 bad pixels. Small workloads
finish with 2 MiB tiler backing; growth and repeated-submit workloads use
10 MiB in every mode. The edge tolerance and raw results are retained in
`tiler/`; these measurements establish no backing reduction.

Eighteen device create/destroy cycles restore the initial four file descriptors
and preserve data/pixel checks (`lifecycle/`). This is bounded lifecycle
coverage, not proof that arbitrary applications cannot leak memory.

Both Vulkan targets built. Real second Ninja invocations leave the frozen
library hashes unchanged; finalization logs and build options are in `build/`.
The patch replay is checked against a separate snapshot of the tested T06
source, excluding later queue work.

## Remaining limits

`interrupted-soak/` preserves a 30-minute cached-import attempt that ends after
about 1,110 seconds with `VK_TIMEOUT`. It has no successful final buffer oracle
or cleanup result. The reported Termux closure does not establish the cause.
A later long run on a different queue candidate is not credited to this build.

The generic deferred cache-sync interface still cannot propagate all backend
failures. Incremental tiler backing remains off by default. Sustained graphics,
long game sessions, full CTS conformance and release readiness remain open.

Probe sources and reproduction instructions are in
[`tests/graphics/t06-memory`](../../../tests/graphics/t06-memory/README.md).
