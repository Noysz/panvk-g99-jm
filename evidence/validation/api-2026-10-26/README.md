# API and combined-build validation

Device: Infinix Note 40 4G, Helio G99, Mali-G57 MC2, kbase r54p1.
The [candidate patches](../../../patches-candidates/2026-10-26-api/README.md)
preserve the source used for these results. This directory contains selected
evidence, not a driver package.

## Build identity

| Binary | SHA-256 |
|---|---|
| Final host | `4cda12f52760b8333d3eefc52cbf91d59edc335ad52340f4556bb1a9beef4b3a` |
| Final Android | `595981cc9ec21f0b667a4127e6b31bb9a03c8d2aa4278b04a7daefff4f3dcf4e` |
| Released test8 driver | `5834e52570c587cab67b23f68c6cd828080c70d66d1433c7364e2c16a7d581d3` |

Incremental rebuilds preserve the final binary hashes. Source replay passes
against the published patch chain. Neither result establishes a reproducible
clean build. Per-feature build records identify the earlier binaries used for
their own gates; those results are not silently relabelled as final-build runs.

## API tests

`prerequisites/` retains the cache-error propagation, nonfaulting overlap
resource and completion/reissue checks for the earlier queue changes. Their
scope excludes unresolved live-fault lifetime behavior. The cache regression
also keeps its two excluded old GPU-fault cases explicit.

- Timestamp: primary/secondary recording, counter lifetime, multiview and
  boundary controls. Three 278-case CTS runs per platform each give 252 Pass,
  26 NotSupported, zero Fail. API negative controls reject the wrong period
  and wrong data expectation. See `timestamp/`.
- Vertex fetch: RGB alpha defaults, GPL and null-only paths. The corrected
  targeted 28-case set passes three times per platform; the preserved control
  fails all 28. The extended 399-case set gives 316 Pass and 83 NotSupported
  in each of six runs. See `vertex/`.
- Conditional rendering: ten full 1,030-case runs each give 415 Pass and
  615 NotSupported. Native reissue and inherited/local predicate checks cover
  672 positive submissions; the older shadow implementation fails its
  control. See `conditional/`. The indirect vertex-stage XFB gap remains.
- Depth bounds: 110 positive cases repeated three times on host and three
  times on Android, with deliberate negative controls. Selected CTS covers
  4,609 cases per platform: 3,523 Pass, 1,086 NotSupported, zero Fail.
  Stored D16/D32 depth, early/late effects, stencil and coverage are checked
  at the recorded scope. See `bounds/`. The feature remains opt-in.

The [probe sources](../../../tests/graphics/t09-api/README.md) are unchanged
copies. `publication-files.json` records source and published hashes.

## Combined regression

`cts/cases.txt` contains 5,658 unique normal CTS cases from the historical
tracked groups. The older 7,127 count includes overlapping group entries,
own tests and a negative control; it is not 7,127 distinct normal CTS cases.

| Driver | Runs | Pass | NotSupported | Fail |
|---|---:|---:|---:|---:|
| Released test8 | 1 | 3,753 | 1,890 | 15 |
| Final host | 1 | 3,754 | 1,890 | 14 |
| Final Android | 1 | 3,754 | 1,890 | 14 |

`cts/test8-comparison.json` records the sole Fail-to-Pass change and empty
lost-Pass/new-Fail lists. Host and Android per-case records are identical.
The test8 binary is unchanged; its native CTS dependencies match the candidate
runs. This is not full Vulkan CTS or a comparison of complete Winlator
packages. The 14 failures are retained in the audit files.

The combined build's own graphics fixtures also ran three times per platform
with negative controls; see `combined/own-audit.json`.

## Applications and capture diagnosis

`runtime/stack-audit.json` covers 24 executions: three triangle/readback runs
and one wrong-pixel control on each of four DXVK versions, plus tessellation
and GS Exploder AIO scenes. Versions: 1.10.3, 1.11.1-sarek, 1.12.1-sarek and
2.3.1-stripped. Launcher/probe checks cover 18 positive processes and one
negative control. These use native Wine/Termux:X11 and the reconstructed
Winlator stack, not the Winlator app itself. Wrapper code is unchanged.

The stack audit predates the visual review. Its `visual_review_pending` flag
is superseded by `aio-capture/validation.json`: separate GDI HUD/result windows
are readable in desktop images but appear black where they cover the render
window capture. All pixels outside those intersections match exactly in the
diagnostic. The paired images and window trees are included. This explains
the diagnostic's black rectangles; the other versions have no contemporaneous
full-desktop captures and are not claimed pixel-conformant.

The initial AIO attempt failed to compile its HLSL shaders. Corrected runs
load the existing native `d3dcompiler_47.dll`; the driver did not change.
Two further AIO runs advertise depth bounds, but do not prove that AIO issues
a bounds operation.

## Naruto sample

`gameplay/` records one COM vs COM Single Match: randomly selected Choji and
Tenten in a wooded arena, 30-second warmup, then 180.206 seconds of measurement.
The four images show active combat through timer 52, without a victory screen.
Configuration: 640x360, GPL on, async off, bounds/overlap/vectorization off,
memory profiling on. No GPU-fault lines were detected in this run.

There are 1,980 measured frame intervals over 180.079747 seconds: diagnostic
10.995 FPS, median 88 ms, p95 116.728 ms. This is not a controlled performance
comparison or a 30-minute stability result. The earlier attempt received
SIGTERM during menu navigation, before measurement; its sender is unknown.
It remains preserved locally and is excluded from these measurements.

## Evidence handling

Filesystem prefixes in copied evidence are normalized to `local-tests`,
`local-home`, `candidate-source` and `public-repo`. Original hashes refer to
the preserved files before normalization. References into those private
directories are identifiers, not public download links. QPA files remain
local; their hashes and full per-case result records are included here.
Selected raw QPA chunks and logs covering the changed case and remaining
failures are included under each `cts/*/selected-raw/` directory.
Historical audit fields retain their original time and scope.

T06-T08 limitations remain: no demonstrated incremental tiler backing saving,
no gameplay benefit from late vectorization, and incomplete overlap lifetime
validation during a live fault. Defaults stay off. These results support a
development candidate, not RC/1.0 qualification.
