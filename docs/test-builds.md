# FourFectVK G57 v0.1.0 test builds (test1-test9)

Test builds for Winlator (Ludashi / CMOD, *Graphics Driver: Wrapper*) on the
**Mali-G57 MC2 (Helio G99)**. Experimental and not Vulkan-conformant.
Downloads (driver ZIP, separate testing README and checksums):
**[release `fourfectvk-g57-v0.1.0-tests`](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-tests)**.

## Test9 (2026-10-28)

Latest: [driver ZIP](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/FourFectVK-G57-v0.1.0-test9.zip),
[testing README](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/README-v0.1.0-test9.txt),
[SHA256SUMS.txt](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/SHA256SUMS.txt).
The ZIP contains the same guide as `README.txt`. The dedicated
[test9 release](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)
has the full changelog. Downloaded assets match the prepared hashes.

ZIP SHA-256:
`492816fb6872c1ce6916017d9c2793c8bfcc67d71cb23926b2813ad40dd81755`

The [source stack](test9/build-order.md) includes timestamp lifetime,
vertex-input robustness/GPL, conditional rendering, opt-in depth bounds,
and cache/coherency fixes. On 5,658 tracked CTS cases per host/Android build,
the candidate has 3,754 Pass, 1,890 NotSupported and 14 Fail: one gained Pass
and no lost Pass against test8. Package checks use the reconstructed native
Wine/Termux:X11 stack, not an actual Winlator app installation. See
[package checks and publication](../evidence/validation/test9-package-2026-10-28/README.md)
and [combined validation](../evidence/validation/api-2026-10-26/README.md).
No repeatable gameplay FPS gain or RC qualification is established.

Start with R0 in the [testing guide](test9/README-v0.1.0-test9.txt).
UBO push now defaults to 32 words. Depth bounds is opt-in; late vectorization,
incremental tiler backing and deferred occlusion stay off by default. Leave
pass overlap off: its fault/lifetime work is unfinished. Use the guide for
test9's defaults and comparison settings, rather than the historical table.

The dedicated test9 tag identifies the candidate stack at
[`7f28e47`](https://github.com/Noysz/panvk-g99-jm/commit/7f28e47db6e50215fbe2935cecaf048f23d6dbfb).

## Earlier builds: test1-test8

| build | base | what is new | result on the phone |
|---|---|---|---|
| test1 | old (`patches/`) `0070` | Memory: device-local memory gets RAM on first GPU use (lazy), reported heap = RAM / 3 (2.5 GB on 8 GB), `PANVK_MEM_PROF` memory log, FourFectVK name in the device name | AIO test RAM 600 -> 240-340 MB, same FPS |
| test2 | `0071`-`0073` | robustBufferAccess2 on by default (DXVK 2.4+ / 3.x / gplasync start), re-submitted command buffers fixed (Vulkan cube in AIO), command-buffer memory cache capped at 48 MB | DXVK 3.1.1 and gplasync reach FL 11_1 |
| test3 | `0074` | One scratch buffer per command buffer instead of one per pass | Driver test: 200 passes 300 MB -> 1.6 MB of scratch. LN2: one run reached the title screen, then the GPU fault |
| test4 | `0075`, `0076` | Empty combined texture slot written to the texture half (CTS robustness: 25 more passes), `PANVK_FAULT_REPORT`, `PANVK_DBG_FREE_DELAY_MS` | LN2: the GPU fault (1x `0x4`, then `0x42`) stays; it is the fragment job of a 166-226 draw pass |
| test5 | `0077` | Pointer audit of failed GPU jobs (`PANVK_FAULT_REPORT`), 1/s failure summary | Audit: 0 suspect addresses in LN2 and Tomb Raider; Tomb Raider: about 1000 failed chains per second, flat pale characters |
| test6 | **new**: upstream Mesa + hafiz's rebase (`patches-upstream/0001`) + FourFectVK `0060`-`0077` (`0002`) | One kbase submit per `vkQueueSubmit`, asynchronous submission with kernel fences, forward pixel kill, multisampled storage images (from hafiz); tess/GS/XFB and the 0.1.0 fixes kept | **0 failed GPU jobs** in Tomb Raider (5 runs) and Little Nightmares II; pictures correct |
| test7 | same as test6 | `PANVK_FRAME_PROF`: GPU busy %, process CPU %, submit / wait time per 2 s; with `PANVK_KBASE_SYNC_SUBMIT=1` the vertex vs fragment split | measuring build |
| test8 | `0003`-`0005` | TR clip/cull off by default (`pan_disable_clip_cull_distance`), kbase `EXEC_VA` 256 MiB (`0004`), v9 timestamps (`0005`, on for `NSUNS2.exe`), `PANVK_UBO_PUSH` | TR menu 8.1-9.7 fps (9.5-12 with UBO push), DXVK 2.x 0 alloc fails, Naruto Storm 2 intro 30 fps; gameplay not measured |

sha256 (first 16 hex digits; full list in `SHA256SUMS.txt` in the release):

```
test1  4d57f2acfaa8f6e3   test5  c342fef950412bd6
test2  ec4313ca14f9f33a   test6  551e164f91f51e7a
test3  b1c56c68b1c79638   test7  c93f0b3a98d24edd
test4  0363c4e21dc06d28   test8  bf1a131e23434c6c
```

For test1-test8: CTS regression of 7127 cases with 0 lost against the build
before, and a Winlator-stack device check (DXVK probe, D3D11 test program on
four DXVK versions, AIO tess/gs scenes). Evidence: `evidence/cts/phase13b`
(test1-test2), `phase13c` (test3-test5), `phase14` (test6-test8).

## Historical switches (test1-test8)

This table records the earlier builds. Test9 defaults, value parsing and
app-rule overrides are documented in its testing guide.

| switch | from | effect |
|---|---|---|
| `PANVK_MEM_PROF=<file>` | test1 | memory log every 2 s |
| `PANVK_FRAME_PROF=<file>` | test7 | GPU busy / CPU / submit / wait log every 2 s |
| `PANVK_UBO_PUSH=<words>` | test8 | push constant UBO words to FAU via GPU gather (`1` = 32 words) |
| `PANVK_KBASE_EXEC_VA_MB=<mb>` | test8 | shader code VA zone in MiB (default 256) |
| `PANVK_V9_TIMESTAMP=1` | test8 | write GPU timestamps in `vkCmdWriteTimestamp2` |
| `PANVK_FAULT_REPORT=1` | test4 | what a failed GPU job carried, plus address audit (test5) |
| `PANVK_KBASE_DEVMEM_EAGER=1` | test1 | back all memory at allocation (old) |
| `PANVK_KBASE_HEAP_PERCENT=N` | test1 | reported heap = N % of RAM |
| `PANVK_V9_NO_RBA2=1` | test2 | hide robustBufferAccess2 |
| `PANVK_POOL_CACHE_MAX_MB=0` | test2 | no cap on cached command-buffer memory |
| `PANVK_TLS_PER_BATCH=1` | test3 | scratch per pass again |
| `PANVK_NULL_TEX_SAMPLER_SLOT=1` | test4 | old empty-texture write |
| `PANVK_DBG_FREE_DELAY_MS=<ms>` | test4 | freed GPU memory stays mapped that long |
| `PANVK_V9_NO_FPK=1` | test6 | forward pixel kill off |
| `PANVK_KBASE_WAIT_SUBMIT=1` | test6 | submit waits for the GPU (no async) |
| `PANVK_KBASE_SYNC_SUBMIT=1` | test1/test6 | wait after every pass (slow) |
| `PANVK_NO_FOURFECT_NAME=1` | test1 | plain device name |

Removed in test6 with the old asynchronous submit (`0068`):
`PANVK_KBASE_ASYNC_CPU_WAITS`, `PANVK_KBASE_PROF`.
