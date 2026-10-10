# tools/ — bring-up scripts

**These are not a supported build path.** Use [`../patches/`](../patches/) to
reproduce the driver state, and [`../docs/reproduction.md`](../docs/reproduction.md)
for the commands. Everything here is kept for chronology: it records what was
tried, in the order it was tried.

Most of these Python scripts **mutate driver source in place** by anchoring on
literal text. If the surrounding source has moved, they will either fail to find
their anchor or patch the wrong place. Several also encode assumptions that were
later disproved — see
[`../docs/historical-superseded.md`](../docs/historical-superseded.md).

| Script | Purpose | Caveat |
|---|---|---|
| `verify_anchor.py` | check a source anchor exists before in-place patching | the safety net the others should be run behind |
| `patch_dump_shader_bin.py` | inject `[PANVK_DEBUG_SHADERBIN]` hex dumping | produced the VS 256 B / FS 128 B captures |
| `patch_debug_submit.py` | inject submit-path debug prints | emits `[PANVK_DEBUG_SUBMIT] vtc_jc ret=` / `frag_jc ret=` |
| `patch_rtfmt_debug.py` | inject render-target format prints | produced `[PANVK_DEBUG_RTFMT] rt=0 fmt=84` |
| `patch_vs_spd_variant.py` | select a VS shader-program-descriptor variant | experiment, not part of the fix |
| `patch_coreq0_test.py` | `core_req = 0` submission experiment | paired with `test_coreq0.sh` |
| `dump_mvj_hex.py` | MALLOC_VERTEX job hex dump helper | emits `[PANVK_DEBUG_MVJ]`; produced the 384 B MVJ dump |
| `dump_tiler_heap_3points.py` | sample the tiler heap at 3 offsets | emits `[PANVK_DEBUG_HEAP]`. Written under the **superseded** assumption that an unreadable heap meant the tiler did no work |
| `test_coreq0.sh` | resubmit the *identical* `vtc_jc` chain with a different `core_req` | byte-for-byte same descriptors, so only `core_req` varies |
| `build.sh` | build shader + harness for the indirect-dispatch test | **stale on two counts:** it defaults `MESA_INCLUDE` to `$HOME/funnymdzz-mesa/include`, a path from an older tree layout, and it targets `indirect_dispatch_test`, which is **NOT IMPLEMENTED** on v9. Override `MESA_INCLUDE` if you use it. |

## Why they are kept

The debug-injection scripts are the provenance of the descriptor captures in
[`../evidence/descriptors/`](../evidence/descriptors/). Without them it would not
be clear how a stock driver came to print `[PANVK_DEBUG_MVJ]` or
`[PANVK_DEBUG_SHADERBIN]` lines at all. `dump_tiler_heap_3points.py` is kept
specifically as a record of a wrong hypothesis being actively investigated
rather than quietly dropped.

The misleading `=> halaman belum ter-commit, tiler tidak menulis apa pun`
("pages not committed, tiler wrote nothing") text is **not** from these scripts —
it is in the driver itself, at `jm/panvk_vX_gpu_queue.c:148`, and therefore
inside patches `0005` and `0012`. That conclusion is false; see
[`../docs/historical-superseded.md`](../docs/historical-superseded.md).

## iolog/ — comparing against the system Mali driver (2026-10-10)

Used for the Tomb Raider performance work in
[`../evidence/perf/tr-menu-2026-10-10/`](../evidence/perf/tr-menu-2026-10-10/).
Build with Termux clang; the binaries are not kept here.

| File | Purpose |
|---|---|
| `iolog.c` | `LD_PRELOAD` logger for `/dev/mali0` (kbase `JOB_SUBMIT` atoms, completion events) and `/proc/ged` calls. Preload it on the bionic game process only, from a path under `$HOME` (the linker refuses `/tmp`). `IOLOG_SNAP=<file>` + `IOLOG_SNAP_AT=<s>` also saves one submit's job chains and the readable GPU memory |
| `blobdeps.py` | atom kinds, rates and dependencies of a trace |
| `passtime.py` | GPU time per pass, vertex/tiler vs fragment, from the completion events |
| `snapdec.c`, `build_snapdec.sh`, `snapdec.flags` | decode a snapshot with pandecode (links the host build's static libs) |
| `shaderstats.py` | per stage: shaders, registers and instruction mix of the draws in a decoded snapshot |
| `sdbstats.py` | aggregate `BIFROST_MESA_DEBUG=shaderdb` lines from a game log |
| `gedprobe.c` | read-only MediaTek GED query (GPU clock and load), no root needed |

## winlator/run_ge.sh, winlator/noshm.c

`run_dxvk.sh` on GE-Proton 11.0-7.1 arm64ec (The412Banner's bionic build).
The script header lists the one-time prefix setup and the three things it
needs on Termux:X11 (the `noshm` preload, `winmm=n,b` for Tomb Raider, a
virtual desktop).

Hardware counters and object code:

| File | Purpose |
|---|---|
| `iolog/hwcnt.c` | GPU-wide Mali counters through kbase `kinstr_prfcnt` (the older hwcnt reader is not in the r54p1 kernel); runs as its own process, no root. Build with the Arm kbase UAPI headers (`-I<kbase>/include/uapi/gpu/arm/midgard`) |
| `iolog/hwcnt.py`, `iolog/hwcmp.py` | decode one capture / compare two (per second and per frame), names from Arm's Mali-G77 layout (`MALI_LAYOUT_XML`) |
| `iolog/isapairs.py` | per-shader tables of two decoded snapshots and structurally matched shader pairs |
