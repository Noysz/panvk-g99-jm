# STATUS — PANVK on Mali-G57 MC2 (v9 / JM)

Snapshot date: **2026-09-17**. Every line below is labelled. Nothing here is
inference unless it says so.

Label meanings:

* **VERIFIED** — reproduced on this hardware, with an artifact in this repo.
* **IN PROGRESS** — partially working, known incomplete.
* **NOT IMPLEMENTED** — no code path exists yet.
* **HISTORICAL-SUPERSEDED** — was believed, later disproved. Kept on purpose.

---

## Headline

| Area | State |
|---|---|
| Kernel / kbase surface | **VERIFIED** working |
| Native v9/JM compute dispatch + readback | **VERIFIED** |
| Native v9/JM MALLOC_VERTEX + FRAGMENT execution | **VERIFIED** |
| Offscreen partial-triangle rasterization + readback | **VERIFIED** |
| FAU count root cause for that workload | **VERIFIED** |
| Raw-JM WRITE_VALUE atom submission | **VERIFIED** |
| Descriptor / resource-table audit | **IN PROGRESS** |
| WSI / present / swapchain | **NOT IMPLEMENTED** |
| General application or game support | **NOT IMPLEMENTED** |
| Vulkan conformance | **NOT IMPLEMENTED** |

## Hardware / runtime — VERIFIED

Mali-G57 MC2, GPU ID `0x90930010` (product `0x9093`), Valhall v9, JM frontend.
kbase `r54p1`, UAPI **11.46**. MediaTek MT6789 / Helio G99, Infinix X6853,
Android 16. Details and raw property dump: [hardware-runtime.md](hardware-runtime.md).

## Compute — VERIFIED

SPIR-V → native Mali ISA → kbase EXEC_VA allocation → job submit → GPU executes
→ result read back on the CPU, through real PanVK (not a raw-ioctl harness).
Reproduced 3x byte-identical. Details: [compute-progress.md](compute-progress.md)
and the original writeup [v9-compute-dispatch-validated.md](v9-compute-dispatch-validated.md).

`vkCmdDispatchIndirect` on v9 is **NOT IMPLEMENTED** (direct dispatch only).

## Graphics — VERIFIED for one offscreen workload

Both v9 job types execute and report success:

* MALLOC_VERTEX job (type 11) — `exception_status=0x01 (DONE)`
* FRAGMENT job (type 9) — `exception_status=0x01 (DONE)`

A deliberately partial 64x64 triangle rasterizes correctly:

```
total non-black pixels: 512 / 4096
corner (0,0)  = 0,0,0,255      <- still clear
center (32,32)= 255,0,0,255    <- triangle
```

512 of 4096 is **12.5%** coverage — a partial shape, not a full-surface fill.
That distinction is the whole point: a fullscreen result cannot be told apart
from a mis-scaled clear or a blit. Details: [graphics-progress.md](graphics-progress.md).

**This proves offscreen rasterization and readback. It does NOT prove WSI,
present, swapchain, or general application support.** See
[known-limitations.md](known-limitations.md).

## FAU root cause — VERIFIED

The missing v9 Shader Environment **FAU count** is causal for the validated
graphics workload. Counts must come from compiled shader metadata
(`fau.total_count`) — this workload reports VS `count=4` / FS `count=3`, so
hardcoding 4 and 3 would be wrong for any other shader. Full A/B evidence and
descriptor-level stage attribution: [fau-root-cause.md](fau-root-cause.md).

## Captured descriptors — VERIFIED

Live byte-level captures of MALLOC_VERTEX job (384 B), Framebuffer Descriptor
(192 B), Fragment Job (64 B), VS/FS shader binaries, both FAU blocks, tiler
context (192 B), tiler heap (32 B), blend (16 B), depth/stencil (32 B).
See [graphics-progress.md](graphics-progress.md) and
[`evidence/descriptors/`](../evidence/descriptors/).

## Resource table — IN PROGRESS

`res_count = 4`, `RESOURCE` = 16 B, so the table is **64 B per stage** for the
current single-descriptor-set workload. `res_count` is derived, not fixed.
Details and the derivation: [resource-table-findings.md](resource-table-findings.md).

## Raw JM — VERIFIED

`WRITE_VALUE` atom submits and executes against `/dev/mali0`:

```
WRITE_VALUE core_req=0x2 submitted, atom_size=56 stride=64
event=0x4, target=0x2a2a2a2a
```

`sizeof(base_jd_atom_v2) == 56` but `JOB_SUBMIT.stride` must be **64**.
`event_code 0x4` is `BASE_JD_EVENT_TERMINATED`, **not** `DONE` (`DONE` = `0x01`).
Artifact: [`evidence/logs/raw_jm_write_value_20260917-004008.log`](../evidence/logs/raw_jm_write_value_20260917-004008.log).
Details: [raw-jm-progress.md](raw-jm-progress.md).

## Corrected earlier conclusions

The "tiler heap is UNREADABLE therefore the tiler did no work" conclusion is
**false** and is retained with a correction, not deleted. The same UNREADABLE
message appears in runs that render correctly. Full list:
[historical-superseded.md](historical-superseded.md).

## Where to go next

Per-test results: [test-matrix.md](test-matrix.md).
How to rebuild and re-run: [reproduction.md](reproduction.md).
