# Valhall-JM (v9) graphics bring-up — working log

Running log of the graphics (draw) side of the v9 port: what was built, what
broke, which hypotheses were tested and **eliminated**, and what is still open.

Compute is done and validated separately — see
[`v9-compute-dispatch-validated.md`](v9-compute-dispatch-validated.md). This
file covers the draw path only, which is still **not working**.

> Everything below is either a command that was run and its raw output, or a
> file/line reference. Claims that were later disproven are kept, marked as
> disproven, because knowing which explanations are already ruled out is the
> most useful part of this document.

---

## Current status: draw executes on the GPU and renders nothing

A minimal offscreen triangle test ([`../tests/triangle_draw_test_v2.c`](../tests/graphics/triangle_draw_test_v2.c))
reaches the GPU end to end:

```
vkCmdDraw returned (no crash)
vkQueueSubmit ok
vkQueueWaitIdle ok
--- ringkasan pixel image 64x64 (rowPitch=256) ---
total piksel non-hitam: 0 dari 4096
CLEAR-ONLY: image ke-clear (hitam) tapi triangle nggak ke-render
```

The image is cleared (the 0xAA sentinel written before submit is gone), so the
fragment job runs. The triangle never appears. No crash, no hang, no fault.

Raw job status read back **after** the GPU ran each job
([`../results/triangle-draw-jobstatus-2026-09-13.log`](../results/triangle-draw-jobstatus-2026-09-13.log)):

```
[PANVK_DEBUG_SUBMIT]    vtc_jc ret=0
[PANVK_DEBUG_JOBSTATUS] vtc_jc  job[0] type=11 exception_status=0x00000001 fault_pointer=0x0
[PANVK_DEBUG_JOBSTATUS] vtc_jc  job[1] type=9  exception_status=0x00000000 fault_pointer=0x0
[PANVK_DEBUG_SUBMIT]    frag_jc ret=0
[PANVK_DEBUG_JOBSTATUS] frag_jc job[0] type=11 exception_status=0x00000001 fault_pointer=0x0
[PANVK_DEBUG_JOBSTATUS] frag_jc job[1] type=9  exception_status=0x00000001 fault_pointer=0x0
```

`type=11` is `MALI_JOB_TYPE_MALLOC_VERTEX`, `type=9` is `FRAGMENT`.
`exception_status=0x1` is **DONE**, not an error — see the exception table
section below. `fault_pointer=0x0` everywhere.

And the tiler heap, read back after both submits:

```
[PANVK_DEBUG_HEAP] SESUDAH vtc_jc  bo->flags=0x2 dev=0x7470400000 size=134217728
                   mincore=absent -- UNREADABLE (I/O error)
[PANVK_DEBUG_HEAP] SESUDAH frag_jc bo->flags=0x2 dev=0x7470400000 size=134217728
                   mincore=absent -- UNREADABLE (I/O error)
```

The heap is allocated `ALLOC_ON_FAULT` (`commit_pages=0`), so pages only exist
once the GPU faults them in. **Zero pages were ever committed.** The GPU
executed the job, reported DONE, and never touched the tiler heap.

On v9 the vertex packet buffer is allocated *by the tiler, from this heap* —
that is what "Malloc Vertex" means. An untouched heap therefore means no vertex
packets were allocated either: the job did nothing at all, successfully.

---

## Hypotheses tested and DISPROVEN

Listed so nobody re-runs these.

### ❌ `core_req` wrong in the kbase atom

Suspected because a wrong `core_req` produces exactly this shape (atom accepted,
completes instantly, GPU idle) — a known failure mode. Disproven: the job
reports `type=11` correctly and `exception_status=DONE`, meaning the GPU
genuinely executed it. Current values: `0x16` for `vtc_jc`, `0x01` for
`frag_jc`.

### ❌ Atom `stride` wrong

Verified against the shipping kernel module
(`mali_kbase_mt6789_a16w_jm.ko`, `r54p1-12eac0`, UAPI 11.46) by disassembly:

```asm
kbase_jd_submit+0x44:
  and  w8, w3, #0xfffffff7   ; w3 = stride, clear bit 3
  cmp  w8, #0x40             ; == 64 ?
  b.ne <error>
```

Only `(stride & ~8) == 64` is accepted, i.e. 64 or 72. We pass 64
(`base_jd_atom_v2`); 72 is the v3 variant with `seq_nr` (UAPI 11.22). Valid.

### ❌ Job header malformed

The MVJ dump was originally taken *inside* the job-encoding function, i.e.
before `pan_jc_add_job()` writes the header, so bytes +000..+031 were always
zero and it looked like `Type=0` (Not started). Moving the dump after
`pan_jc_add_job()` shows:

```
header: type=11 (MALLOC_VERTEX) index=1 dep1=0 dep2=0
jc state: first_job=0x7711155500 job_index=1 tiler_dep=1
```

Correct. `first_job` matches the job's own GPU address.

### ❌ `Tiler Context.Polygon List` not set

Suspected because the field exists in `v9.xml` and we never write it.
Disproven: `grep polygon_list src/gallium/drivers/panfrost/pan_jm.c` returns
**nothing** — Gallium never sets it on v9 either. The only writer is
`pan_emit_midgard_tiler()`, gated `#if PAN_ARCH <= 5`. On Valhall the tiler
allocates the polygon list itself out of `Heap`.

### ❌ Hierarchy mask computed as 0

Device reports `RAW_TILER_FEATURES = 0x809` →
`max_levels = (0x809 >> 8) & 0xF = 8`. Since `max_levels >= 8`,
`pan_select_tiler_hierarchy_mask()` takes the `BITFIELD_MASK(max_levels)`
branch, not the `default_mask[]` table, giving **0xFF**. With a 64×64 FB and a
128 MB budget the shrink loop never runs. Mask is valid.

### ❌ `poly_heap` should be used instead of `tiler_heap`

`dev->poly_heap` is allocated and initialised in `panvk_vX_device.c`, but every
consumer is under `csf/`. CSF uses a driver-written `{base, bottom, size}`
struct the GPU reads directly — a different mechanism from the JM `TILER_HEAP`
descriptor. Our JM path points `TILER_CONTEXT.heap` at a `TILER_HEAP`
descriptor describing `dev->tiler_heap`, which is exactly what
`jm_emit_tiler_desc()` does for v9 in Gallium.

### ⚠️ Note on the exception table

`exception_status=0x1` was initially misread as an error. String table in the
kernel module resolves it:

```
NOT_STARTED/IDLE/OK   0x00
DONE                  0x01
...
JOB_CONFIG_FAULT, JOB_READ_FAULT, JOB_WRITE_FAULT, JOB_BUS_FAULT,
DATA_INVALID_FAULT, TILE_RANGE_FAULT, ADDR_RANGE_FAULT, INSTR_INVALID_PC,
OUT_OF_MEMORY, JOB_POWER_FAULT, JOB_AFFINITY_FAULT
```

All real faults have distinct names at higher codes. `0x1` is success.

---

## What is verified correct

Decoded by hand from the 384-byte `MALLOC_VERTEX_JOB` dump against the
`v9.xml` aggregate layout (offsets in bytes):

| Offset | Section | Value | Verdict |
|---|---|---|---|
| +000 | Job Header | `type=11`, `index=1` | ✅ |
| +032 | Primitive | `draw_mode=0x08`, `index_type=0`, `index_count=3`, `allow_rotating=1`, `low/high_depth_cull=1`, `secondary_shader=0` | ✅ |
| +048 | Instance Count | `1` | ✅ |
| +052 | Allocation | `vertex_packet_stride=16`, `vertex_attribute_stride=0` | ✅ (no-varyings path) |
| +056 | Tiler | non-null pointer to `TILER_CONTEXT` | ✅ |
| +104 | Scissor | `min 0,0  max 63,63` | ✅ (64×64 FB) |
| +112 | Primitive Size | `1.0f` | ✅ |
| +120 | Indices | `0` | ✅ (non-indexed) |
| +128 | Draw.Flags 0 | `0x00010000` → `front_face_ccw=1`, both cull bits **0** | ✅ nothing is culled |
| +132 | Draw.Flags 1 | `0x0001ffff` → `sample_mask=0xffff`, `render_target_mask=0x1` | ✅ |
| +136 | Draw.Vertex array | `packet=1` | ✅ |
| +152 | Draw.Minimum Z | `0.0f` | ✅ |
| +156 | Draw.Maximum Z | `1.0f` | ✅ |
| +168 | Draw.Depth/stencil | non-null | ✅ |
| +176 | Draw.Blend | `blend_count=1`, non-null address | ✅ |
| +192 | Draw.Shader (FS env) | resources / shader / thread_storage / fau all non-null | ✅ |
| +256 | Position (VS env) | resources / shader / thread_storage / fau all non-null | ✅ |
| +320 | Varying | all zero | consistent with `secondary_shader=0` |

Resource tables carry a table count of 4 in their low bits, as
`cmd_prepare_shader_res_table()` encodes.

---

## Still open

1. **Is the position shader actually producing positions?**
   The `POSITION` shader environment points at `vs->spds.pos_triangles`, but
   the SPD contents have never been dumped or verified to contain real code.
   If position comes out degenerate, every primitive is discarded before
   tiling — which matches the symptom exactly, with no fault.
   Note `spd` is a union alias for `spds.pos_points` (first member), so
   `vs->spd` silently means *points* — easy to get wrong.

2. **`Vertex Array` semantics on v9 IDVS.**
   ```xml
   <struct name="Vertex Array" size="3">
     <field name="Packet" size="1" start="0:0" type="bool"/>
     <!-- Written by hardware in MallocVertexShader job mode -->
     <field name="Pointer" size="58" start="0:6" type="address" modifier="shr(6)"/>
     <field name="Vertex packet stride" size="16" start="2:0" type="uint"/>
     <field name="Vertex attribute stride" size="16" start="2:16" type="uint"/>
   </struct>
   ```
   We set `packet=1` and leave `Pointer` / the strides at zero, on the reading
   that the hardware fills them ("Written by hardware in MallocVertexShader job
   mode"). The same strides also appear in the separate `ALLOCATION` section,
   which we *do* fill. Whether both are required, and whether `Pointer` must be
   pre-seeded, is unconfirmed.

3. **Does anything need to initialise the tiler heap before first use?**
   `pan_jc_initialize_tiler()` emits a `WRITE_VALUE` job for that purpose but
   early-returns on `PAN_ARCH >= 6`. Whether v9 + `ALLOC_ON_FAULT` needs an
   equivalent warm-up is untested.

4. **`first_provoking_vertex` was missing** and is now set in
   `cmd_prepare_tiler_context()` (Gallium sets it under `#if PAN_ARCH >= 9`;
   PanVK's JM path did not). This alone should only affect winding, not make
   geometry vanish, and the run above already includes the fix — the result
   did not change.

---

## Not yet implemented in the v9 draw path

Not bugs; simply unwritten:

- `CmdDrawIndirect` / `CmdDrawIndexedIndirect` — still empty stubs
- Multi-layer / multiview — only layer 0 is encoded
- Transform feedback, tessellation

## Debug instrumentation

[`../patches/0005-panvk-v9-kbase-jm-submit-and-debug.patch`](../patches/0005-panvk-v9-kbase-jm-submit-and-debug.patch)
contains the kbase JM submission path plus the debug hooks used above:

- `PANVK_HEAP_LOG=<path>` — send `[PANVK_DEBUG_HEAP]` / `[PANVK_DEBUG_SUBMIT]` /
  `[PANVK_DEBUG_JOBSTATUS]` to a file. Without it they go to stderr, which is
  lost inside a container.
- Tiler-heap dump reads through `pread()` on `/proc/self/mem` rather than
  dereferencing the mapping, so an uncommitted `ALLOC_ON_FAULT` page reports
  `EIO` instead of raising `SIGSEGV` — the SIGSEGV case is precisely the one
  being diagnosed.
- Job headers are read back after the wait, with the descriptor pool
  invalidated first, so `Exception Status` / `Fault Pointer` reflect what the
  GPU wrote.

Two temporary hacks in that patch must be reverted before it is anything but a
debug build: `NO_MMAP` is dropped from the tiler heap allocation so the CPU can
read it, and the debug prints are unconditional rather than gated behind a
`PANVK_DEBUG` flag.

## Build note

`ninja` with no target fails in this tree on the Gallium DRI target
(`_mesa_glapi_tls_Context` version-script error — a known bionic TLS/glapi
dead end, unrelated to Panfrost). Build the Vulkan target explicitly:

```
ninja -C build src/panfrost/vulkan/libvulkan_panfrost.so
```
