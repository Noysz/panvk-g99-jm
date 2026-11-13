# JM CPU lifetime and fault accounting

Four additive patches on the pinned
[stock JM baseline](../2026-11-13-stock-jm/README.md). The complete
[45-patch series](../../docs/november-lifecycle.series) reconstructs the source
used by the fresh Host/Android builds and regression.

| Patch | Effect | Dependency |
|---|---|---|
| 0001 CPU lifetime | Close admission, wake the event reader, quiesce CPU callbacks, retain sync payload owners and interrupt external waits | Pinned queue/sync implementation |
| 0002 profiler teardown | Finish queue CPU teardown before freeing profiler state | 0001 callback quiescence |
| 0003 failed-event accounting | A latched device loss cannot advance success sequence numbers or release original atom IDs | Pinned event accounting; no ledger dependency |
| 0004 fault metadata snapshot | Store scalar metadata at submission instead of dereferencing dead command buffers/batches in callbacks | Pinned fault reporter |

**Decision: KEEP as a qualified local candidate at the recorded scope.**
These patches do not enable ledger, capture, physical leases, quarantine
or overlap. They protect CPU metadata and error reporting, not physical
GPU backing on every failure path.

Fault reporting deliberately loses callback-time live job-status scans.
Event code, atom/submission identity, chain address, command-buffer identity
and submission-time batch size remain. The command-buffer identity is an
opaque value, never a callback dereference. This trade-off avoids a diagnostic
use-after-free; it is not a replacement for GPU terminal proof.

The four diffs also apply textually to the earlier correctness stack.
That different composition has **not** received this combined runtime
qualification. Do not transfer these results to a Main-only replay.

[Validation](../../evidence/validation/jm-lifecycle-2026-11-13/README.md) |
[Reconstruction/build](../../docs/november-build.md) |
[CPU regression fixtures](../../tests/jm-lifecycle/README.md)
