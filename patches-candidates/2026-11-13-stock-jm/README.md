# Stock JM development prerequisites

This series preserves the tested stock-kernel development baseline after
Test9 and the [November correctness series](../2026-11-01-correctness/README.md).
It is an explicit candidate stack; the public Test9 build order is unchanged.

Apply the entries in `series` with `git apply`, in order. Patch 0007 retains
Matthieu Moatti's upstream attribution, review trailers and commit
`33e6787644a05af64415690e9ed466f1aab51d5a`. Other entries are plain diffs.
Do not apply the numbered files as independent, interchangeable patches.

| Group | Included work | Dependency |
|---|---|---|
| 0001, 0002, 0006, 0015 | Allocation errors, tiler batch budget, 256 MiB VA / 128 MiB planning limit | 0006 enables the previously approved 0002 policy; 0015 keeps its planning bound |
| 0003, 0004 | Draw trace guard and cached debug flags | 0004 selects the already tested 0003 default |
| 0005, 0007, 0008, 0013 | Viewport clipping, spill access flags, logic-op destination reads, null index buffer | Existing JM/v9 and correctness stack |
| 0009, 0010, 0011 | Tiler dependency reduction and descriptor/template reuse | 0011 uses 0010 state tracking; keep reset and invalidation behavior together |
| 0016, 0017 | WSI extension dependencies, process memory budget, LOW priority acceptance | Existing device/instance paths |
| 0019, 0020 | Timestamp pool implementation and precompiled-helper allocation failures | Existing query and precompiled-shader paths |

This is the exact prerequisite composition for the
[lifecycle candidate](../2026-11-13-jm-lifecycle/README.md). Full fresh builds,
targeted regression and performance controls exist for that composition.
Historical broader CTS results on earlier binaries are separate.

0012 overlap, 0014 sysval experiment, 0018 placed-map/ION experiments and
0021 idle-BO reclamation are **not** in this series. Existing overlap controls
remain off; no physical-retirement policy is introduced.

[Build order](../../docs/november-build.md) |
[Decisions and remaining gates](../../docs/november-development.md)
