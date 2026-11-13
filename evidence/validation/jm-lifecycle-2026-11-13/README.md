# JM lifecycle candidate validation

Recorded 13 November 2026 on stock, non-root Infinix X6853, Android 16,
Mali-G57 MC2. Baseline = 41 ordered patches; candidate = baseline plus the
four CPU-lifetime/fault patches. Overlap and physical-retirement experiments
were disabled. These are development binaries, not Test9 or a Test10 release.

## Source and full builds

[Source manifest](source.json) pins every patch SHA-256 and both reconstructed
trees. Candidate replay matches all 12,928 frozen input hashes after resolving
symlinks and 33 prescribed CSV line-ending conversions. The baseline input
inventory also contained two owner files that were not linked until candidate
0001. They are explicitly recorded, not treated as baseline runtime code.

[Build records](builds.json): fresh directories, no object replay or ccache;
Clang/LLD 21.1.8, Meson 1.12.0, Ninja 1.13.2. Host shader tools were built
before Android from the same source. Host/Android completed 1109/1119
baseline and 1110/1120 candidate commands. Generated v9 shaders match.
Only the inherited per-application configuration warning remains.

| Binary | SHA-256 |
|---|---|
| Baseline Host | `1470b68f67584c5dad5f86b09375ccad8b08d73d74ac0809d7cc6d365c17cacb` |
| Candidate Host | `ae73ca4ebfbec16d25ff0fc1e3446cadf7499b3020a7a5857f4cbd2b9b2f6fc9` |
| Baseline Android | `d4d2bf1153cb8d6acdec877a7376bd792fa1040e5fb210970498e2ebe691dbf2` |
| Candidate Android | `ab208ca58cffa7f2103ecfdd9a14109cb3422635ed51c7a74d37c97ce03611fe` |

Host means native Termux/Bionic, debugoptimized. Android is release AArch64
API 29 with platform stubs, `__ANDROID__=1` and inherited portable
`DETECT_OS_ANDROID=0`; kbase and JM v6/v7/v9 source participation was checked.
This is not AOSP packaging or an actual Winlator installation qualification.
The rebuild procedure is reproducible; byte-identical rebuilds are not claimed.

## Correctness and lifetime

Each of the four binaries: **120 Pass / 20 NotSupported / 2 Fail**, with
identical case outcomes. Both failures are inherited:

- `dEQP-VK.query_pool.concurrent_queries.primary_command_buffer`
- `dEQP-VK.query_pool.concurrent_queries.secondary_command_buffer`

[Case outcomes](cts-cases.json), [selection](targeted-cases.txt) and
[correctness/memory observations](correctness.json) retain the bounded scope.
The later query-counter correction is excluded from this composition and
needs separate combined qualification.

[CPU tests](cpu-tests.json), on Host UBSan and native Android:
133 owner/callback/reader concurrency scenarios, 500 sync payload cycles,
163 queue-adapter scenarios, 64 device/profiler teardown cycles and
512 queue initialization/destruction cycles. Partial submission, read EFAULT
at 0-32 events, late events, original IDs and initialization failures are
covered. Before-source controls fail as expected. The fault reporter survives
metadata becoming inaccessible. Lower kernel/resource layers are mocked.
ASan and GPU fault recovery are not claimed.

Normal hardware lifecycle: six processes, 48 devices, 4,608 allocations and
9,216 checked transfers. FD returns to 7 after each device and 4 after the
instance; candidate uses one additional live wake FD. VA restores per device,
and post-device smaps RSS stays flat within each process. This short run does
not establish long-run stability or safe physical retirement after lost events.

## Performance

All 80 runs valid; no new GPU/teardown errors in the recorded runs.
DriverLab, 1920x1080, headless single frame in flight, shader cache disabled,
same pinned libraries, experiments off. No builds or source hashing ran
during measurement. [Method](performance-method.json) retains settings.

| Scene / series | Baseline FPS | Candidate FPS | Delta | Interpretation |
|---|---:|---:|---:|---|
| Draws, initial | 32.688 | 34.305 | +4.95% | Candidate outliers; no improvement claim |
| Compute, initial | 19.401 | 19.096 | -1.57% | Required confirmation |
| Compute, confirmation | 19.270 | 19.238 | -0.17% | Initial decline did not repeat materially |
| Passes, initial | 49.327 | 50.374 | +2.12% | Noisy |
| Passes, confirmation | 50.823 | 50.837 | +0.03% | No material shift resolved |

Initial: AAAA plus three ABBA cycles per scene, 8 s / 2 s warmup / 3 s
cooldown. Confirmation: Compute and Passes, AAAA plus three BAAB cycles,
20 s / 5 s / 10 s. [Initial results](performance-initial.json) and
[confirmation](performance-confirmation.json) include every FPS observation
and cycle delta.

A/A CV: Draws 1.97%; Compute 0.30% initially, 3.12% in confirmation;
Passes 12.69%, then 8.42%. Battery temperature access was denied; clocks were
dynamic. Coarse Compute GPU frequency was 1100/1066.58 MHz initially and
1095.56/1093.85 MHz in confirmation. This does not prove thermal equivalence
or DVFS causality. Small effects remain poorly resolved, especially Passes.
No gameplay speedup or zero-overhead claim is made.

## Decision

KEEP the four patches for candidate integration review on this exact stack.
No new targeted CTS failure or repeatable material performance decline was
found. Full tracked regression on this combined binary, longer games and
release packaging remain pending. P0 stays BLOCKED; 0012 stays REWORK/default
OFF; Test10 stays PRIVATE/NO-GO. CPU quiescence is not GPU terminal recovery.

Original logs, binaries and source archives remain preserved privately.
[Record digests](record-integrity.json) identify the selected evidence inputs.
The exported fixtures were recompiled and rerun on Host UBSan and native
Android during repository preparation; [export checks](export-validation.json)
record positive and expected-negative outcomes.
