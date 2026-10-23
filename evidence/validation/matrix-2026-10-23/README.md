# T07 dynamic matrix validation

These runs evaluate the existing, default-off `PAN_LATE_VECTORIZE` option on
Mali-G57 MC2. They add no compiler patch. The driver source is the frozen T06
memory candidate, with host and Android hashes recorded in the run manifests.

VERIFIED-HW: one baseline and three enabled CTS runs each contain 2,900 cases:
2,343 Pass, 551 NotSupported and six existing Fail. Every case status and
execution order match the baseline. `cts/` contains the results,
`execution-order-audit.json` records the comparison, and `qpa-pins.json`
identifies the larger raw QPA files retained locally.

The small dynamic-matrix oracle passes 18 ordinary processes. Two deliberate
wrong-reference processes fail as expected (`correctness/`). The separate
Android timing workload passes 24 ordinary processes and one wrong-reference
control, checking the output, unchanged input and guards (`timing/`).

## Timing scope

Each layout uses three blocks ordered off, on, on, off. Layout order alternates
between blocks. The table compares the mean of the two process medians per
mode within each block. It measures QueueSubmit-to-fence time, including host
submission and wait overhead.

| Block | Row-major reduction | Column-major control reduction |
|---|---|---|
| 1 | 13.19% | 1.95% |
| 2 | 15.88% | -0.19% |
| 3 | 18.45% | 2.69% |

Clock frequency and temperature were not controlled. These are workload
measurements, not GPU-only timing or gameplay/FPS results.

The row-major shader changes from 896 to 768 code bytes and from 15 to 17 work
registers. Both versions have zero stack, spills and fills. The column-major
shader stays at 640 bytes and 29 registers. Compiler output and raw timings
are retained alongside `timing/compile-stats.json` and `timing/measurements.json`.
The row-major result therefore does not establish reduced register pressure.

## Remaining work

`PAN_LATE_VECTORIZE` remains off by default pending representative render/game
validation. A separate attempt to reserve more registers by lowering the SSA
spill target from 64 to 56 was rejected; it is not part of the source stack.
Register-pressure improvements and T07 as a whole remain open.

The [probe sources](../../../tests/graphics/t07-matrix/README.md) include both
workloads. The portable timing runner changes path selection from the original
local runner; the C and shader sources retain their tested bytes.
