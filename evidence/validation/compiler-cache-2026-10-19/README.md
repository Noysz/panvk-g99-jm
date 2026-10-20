# Compiler/cache candidate validation

Device: Mali-G57 MC2, CTS device ID `0x90930010` (`device-session.txt`).
Source: published upstream stack plus `patches-candidates/2026-10-19/series`.
This is targeted candidate validation, not a conformance or release result.

The original replay below includes local root-document metadata that has since
been removed from the public checkpoint patch. Its driver-source sections and
the tested binaries are unchanged. The final replay, including the deferred
query candidate, is in
[`../deferred-query-2026-10-19/patch-chain.json`](../deferred-query-2026-10-19/patch-chain.json):
12,928 compared files, six support-file exclusions and one explicit root-pointer
mode exception. Twelve gate tests pass. Historical patch hashes below refer to
the preserved original checkpoint.

## Source and builds

- Nine patch-chain gate tests pass (`gate-tests.log`).
- Reconstructed candidate matches 12,929 files, with zero differences and
  five explicitly excluded local support files (`candidate-chain.json`).
- Host and Android Vulkan targets both build successfully. Build options,
  source fingerprints, binary SHA-256 values and ELF Build IDs are recorded
  alongside the build logs.
- Existing binaries were preserved before modification. The runtime baseline
  here is the preserved host binary identified in `build-manifest.json`;
  it was not rebuilt during this work.

## Shader cache separation

`tools/verify_shader_cache.py` runs one existing CTS draw/readback case in
independent processes. Each repetition starts with a new cache directory.
The transition changes only `PANVK_V9_CLIP_KILL_FPK`, within the same binary.

| Step | Preserved binary: hits/misses | Cache-key fix: hits/misses |
|---|---|---|
| Default, cold | 0/6 | 0/6 |
| Default, warm | 6/0 | 6/0 |
| Control after default | 6/0 | 3/3 |
| Control, warm | 6/0 | 6/0 |
| Return to default using `false` | 6/0 | 6/0 |
| Explicit cache disable | No cache stats or directory | No cache stats or directory |

All steps render successfully in three repetitions. The preserved binary is
an intentional negative control for separation: its complete reuse after the
switch changes demonstrates the missing key distinction. The corrected binary
separates the affected entries while retaining shared entries and warm reuse.
This test does not prove that a draw exercises K2's GPL metadata path, nor
does it measure FPK or game performance.

`cache-key-validation.json` and `cache-baseline-control.json` contain the
per-process verdicts and binary hashes. Small raw logs are retained; the full
QPA files remain in `~/panvk-refs/followup-2026-10-19/`.

## Compiler regression

The list combines the existing clipping, UBO and scratch/TLS lists, removing
80 duplicate names: 2,900 unique cases (`compiler-cases.txt`). CTS explicitly
disables the shader cache, so these runs exercise compilation.

| Run | Pass | Fail | NotSupported | Status changes |
|---|---:|---:|---:|---|
| Preserved baseline | 2343 | 6 | 551 | Reference |
| Candidate 1 | 2343 | 6 | 551 | None |
| Candidate 2 | 2343 | 6 | 551 | None |
| Candidate 3 | 2343 | 6 | 551 | None |

The six failures are the existing transform-feedback `depth_clip_control_tese`
and `depth_clip_control_tese_ptsz` cases in monolithic, fast-GPL and optimized-GPL
variants. Their continued failure is not counted as a pass. Compressed TSVs
preserve the complete per-case results without copying large QPA logs.

## Android and DXVK

The final Android build passes all 18 process checks (three complete cycles)
with `MESA_SHADER_CACHE_DISABLE` unset. Counts match the cache-key fix column
above, and the explicit disable override still prevents cache creation.
See `android-default-cache.json` and its raw logs.

The packaged driver passes three Winlator-stack DXVK 2.3.1 render/readback
runs (`frames=180 checked=6 bad=0`). The deliberate wrong-pixel negative
control fails with `bad=6`, as expected. Driver identity is also checked by the
existing harness. `dxvk-validation/summary.txt` reports `as-expected=4` and
`unexpected=0`: three positive runs plus one control, all using one DXVK version.

## Limits

- No gameplay FPS, frame-time, stutter or compiler-corpus speedup is established.
  Process durations include harness overhead and, for some runs, concurrent
  CPU build work; they are not performance comparisons.
- The Android property-cache patch assumes system properties remain unchanged
  during a process lifetime. Environment-variable overrides are still read.
- Experimental controls remain experimental. The intentional
  `PANVK_V9_EMU_NO_HASH` negative control is retained.
- No commit, push, install over a validated package, or release was performed.

## Reproduce the cache test

Use a new output directory for each invocation:

```sh
python3 tools/verify_shader_cache.py \
  --cts /data/data/com.termux/files/home/cts \
  --driver /data/data/com.termux/files/home/panvk-refs/followup-2026-10-19/host-cache-key.so \
  --output /data/data/com.termux/files/home/panvk-refs/cache-recheck \
  --repeats 3
```

Use `--expect shared` with the preserved baseline for the negative control.
Use `--default-cache` with the final Android candidate to omit the explicit
cache-enable override. `run_candidate_cts.py` and `run_android_checks.py` are
archived execution scripts; they expect the binaries in the local artifact
directory, not in this evidence directory.
