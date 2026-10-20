# Deferred occlusion candidate

Apply `0001-defer-occlusion-availability.patch` after the compiler/cache stack
in `../2026-10-19/series`, itself applied after `patches-upstream/0001` through
`0005`. This is a local implementation for Valhall v9 JM, not a source backport
of an unavailable rival implementation.

`PANVK_DEFER_OQ=1` retains EndQuery availability writes in a following batch
ordered after the producing fragment work. It is off by default. Statistics
queries retain the prior split behavior. The patch also releases abandoned
current batches during command-buffer reset/destruction and handles allocation
failure in the new availability-batch path.

`PANVK_DEFER_OQ_EARLY_CONTROL=1` is an intentionally incorrect ordering control;
never use it for gameplay or performance results. The polling oracle must
reject it even when all final query results and framebuffer pixels are correct.

Builds, ordering/lifetime/allocation controls and three targeted CTS repeats
meet the documented gates; the two baseline concurrent-query failures remain.
Six TR camera runs give median 13.4 FPS on / 13.1 off, including one unusually
fast 16.8 FPS on run. A repeatable benefit is not established. Keep the default
off pending further workload evidence and the full release gates.

See the [query follow-up](../../docs/occlusion-followup-2026-10-19.md),
[task ledger](../../docs/followup-tasks-2026-10-19.md), and
[evidence](../../evidence/validation/deferred-query-2026-10-19/README.md).
No release or commit is implied.
