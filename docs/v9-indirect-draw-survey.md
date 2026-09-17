# T4.4.0 — indirect draw survey for Valhall v9 / Job Manager

**Purpose.** Decide, before writing any code, whether `vkCmdDrawIndirect` can be
implemented on Mali-G57 MC2 (v9/JM) and by what mechanism.

**Evidence rule applied here.** Every statement below is either a quotation from
a file with its line number, or a runtime measurement from this device. Nothing
is inferred from naming, from other GPU families, or from documentation not
present on this machine. Where something could not be established, it says so.

**Sources used, all local:**

| Ref | Path | What it is |
|---|---|---|
| `[XML9]` | `src/panfrost/genxml/v9.xml` | Mesa's v9 descriptor definitions |
| `[XML7]` | `src/panfrost/genxml/v7.xml` | Mesa's v7 (Bifrost) definitions, for contrast |
| `[XML10]` | `src/panfrost/genxml/v10.xml` | Mesa's v10 (CSF) definitions, for contrast |
| `[PK9]` | `build/src/panfrost/genxml/v9_pack.h` | machine-generated v9 packing code |
| `[PK7]` | `build/src/panfrost/genxml/v7_pack.h` | machine-generated v7 packing code |
| `[SH9]` | `build/src/panfrost/libpan/libpan_shaders_v9.h` | generated v9 helper-shader table |
| `[SH7]` | `build/src/panfrost/libpan/libpan_shaders_v7.h` | generated v7 helper-shader table |
| `[CL_ID]` | `src/panfrost/libpan/indirect_dispatch.cl` | indirect-dispatch helper kernel |
| `[CL_DH]` | `src/panfrost/libpan/draw_helper.cl` | draw-helper kernels |
| `[MB]` | `src/panfrost/libpan/meson.build` | how helper kernels are built per arch |
| `[DRAW]` | `src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c` | JM draw path |
| `[DISP]` | `src/panfrost/vulkan/jm/panvk_vX_cmd_dispatch.c` | JM dispatch path |
| `[PRE]` | `src/panfrost/vulkan/jm/panvk_vX_cmd_precomp.c` | JM precomp dispatcher |
| `[ARM]` | `VX504X08X-SW-99002-r54p1-12eac0.tar`, extracted | ARM Mali kbase r54p1 driver release, from the project folder |

All paths are relative to `~/panvk-g57/mesa`.

---

## 1. Finding: v9 has no indirect-draw hardware descriptor at all

`[XML9]` contains **63 `<struct>` definitions** and **zero** occurrences of the
string `indirect`, in any case. Searched terms and hit counts in `[XML9]`:

| term | hits |
|---|---|
| `Indirect` | 0 |
| `indirect` | 0 |
| `Draw Count` | 0 |
| `draw_count` | 0 |
| `Buffer Descriptor` | 0 |
| `From Buffer` | 0 |
| `Fetch` | 0 |
| `Count Buffer` | 0 |

The complete v9 struct list, for the record, contains no candidate under another
name: AFBC Plane, AFBC RGB Render Target, AFBC S Target, AFBC YUV Render Target,
AFBC ZS Target, ASTC 2D Plane, ASTC 3D Plane, Allocation, Attribute, Blend, Blend
Equation, Blend Fixed-Function, Blend Function, Blend Shader, Buffer, CRC, Cache
Flush Job Payload, Chroma 2p Plane, Compute Payload, Count, DCD Flags 0, DCD
Flags 1, DCD Pointer, Depth/stencil, Descriptor Header, Draw, Fragment Job
Payload, Framebuffer Padding, Framebuffer Parameters, Framebuffer pointer,
Generic Plane, Indices, Internal Blend, Internal Conversion, Job Header, Local
Storage, Null Descriptor, Null Plane, Plane Header, Preload, Primitive, Primitive
Size, RGB Render Target, RT Buffer, RT Clear, Render Target, Resource, S Target,
Sampler, Scissor, Shader Environment, Shader Program, Texture, Tiler Context,
Tiler Heap, Tiler Pointer, Tiler State, Tiler Weights, Vertex Array, Write Value
Job Payload.

For contrast, indirect-related hit counts across generations:

| genxml | `indirect` hits |
|---|---|
| v6 | 0 |
| v7 | 0 |
| **v9** | **0** |
| v10 | 3 |
| v12 | 6 |
| v13 | 6 |
| v14 | 6 |

The v10 hits are `[XML10]:550` `RUN_COMPUTE_INDIRECT value="44"`, `[XML10]:802`
`<struct name="CS RUN_COMPUTE_INDIRECT" size="2">`, and `[XML10]:809` the opcode
field. **Those are compute-shader command-stream opcodes, not graphics draws**,
and they belong to the CSF frontend which v9 does not have.

**Conclusion 1 (VERIFIED-SRC).** No Job Manager generation in this tree —
v4, v5, v6, v7, v9 — exposes an indirect-draw descriptor. Indirect draw on JM
cannot be a matter of filling in a hardware struct, because no such struct
exists.

---

## 2. Finding: Bifrost implements indirect draw with compute helper shaders

Since v6/v7 also have zero indirect descriptors, yet `[DRAW]` implements
`CmdDrawIndirect` for `PAN_ARCH < 9`, the Bifrost mechanism must be software. It
is. From `[DRAW]:1537` onward, `panvk_cmd_draw_indirect()` does the following:

1. `[DRAW]:1581` obtains a precomp context:
   `struct panvk_precomp_ctx precomp_ctx = panvk_per_arch(precomp_cs)(cmdbuf);`
2. `[DRAW]:1610` packs a `WRITE_VALUE_JOB` that seeds a min/max scratch
   result with `type = MALI_WRITE_VALUE_TYPE_IMMEDIATE_64`.
3. `[DRAW]:1617` submits it as `MALI_JOB_TYPE_WRITE_VALUE`.
4. `[DRAW]:1627` dispatches `panlib_draw_index_minmax_search_helper_struct`,
   a compute kernel that scans the index buffer for its minimum and maximum index.
5. `[DRAW]:1700` dispatches `panlib_draw_indexed_indirect_helper_struct`, a
   compute kernel that reads the application's indirect buffer and **patches the
   already-built job descriptors in place**.

**Conclusion 2 (VERIFIED-SRC).** On Job Manager hardware, indirect draw is
emulated: the GPU runs helper compute shaders that rewrite draw descriptors from
the indirect buffer. There is no hardware indirection. Any v9 implementation must
follow the same route.

---

## 3. Finding: the v9 precomp dispatcher is an empty stub, and why

`[PRE]` is the function that submits a precomp helper as a compute job. It is
split by arch. The v9 half, quoted in full from `[PRE]:100-112`:

```c
#else /* PAN_ARCH >= 9 -- lihat catatan PATCH di awal file */

void
panvk_per_arch(dispatch_precomp)(struct panvk_precomp_ctx *ctx,
                                 struct panlib_precomp_grid grid,
                                 enum panlib_barrier barrier,
                                 enum libpan_shaders_program idx, void *data,
                                 size_t data_size)
{
   /* TODO v9: belum diimplementasikan. */
}

#endif /* PAN_ARCH >= 9 */
```

The reason is recorded at `[PRE]:16-20`:

> `file ini masih pakai COMPUTE_JOB gaya Bifrost (INVOCATION/PARAMETERS/DRAW
> terpisah), belum diadaptasi ke "Compute Payload" native v9.`

That claim is checkable, and it holds. The Bifrost body uses three payload
sections — `[PRE]:66` `pan_section_ptr(job.cpu, COMPUTE_JOB, INVOCATION)`,
`[PRE]:70` `pan_section_pack(job.cpu, COMPUTE_JOB, PARAMETERS, cfg)`, and
`[PRE]:83` `pan_section_pack(job.cpu, COMPUTE_JOB, DRAW, cfg)` — and those
sections do not exist at v9.

### 3.1 Byte-level proof that the sections differ

From the machine-generated headers, not from reading prose.

**v7 COMPUTE_JOB** — `[PK7]`:

| macro | value | line |
|---|---|---|
| `MALI_COMPUTE_JOB_LENGTH` | **192** | `[PK7]:6527` |
| `MALI_COMPUTE_JOB_ALIGN` | 64 | `[PK7]:6528` |
| `MALI_COMPUTE_JOB_SECTION_HEADER_OFFSET` | 0 | `[PK7]:6535` |
| `MALI_COMPUTE_JOB_SECTION_INVOCATION_OFFSET` | 32 | `[PK7]:6542` |
| `MALI_COMPUTE_JOB_SECTION_PARAMETERS_OFFSET` | 40 | `[PK7]:6549` |
| `MALI_COMPUTE_JOB_SECTION_DRAW_OFFSET` | 64 | `[PK7]:6556` |

**v9 COMPUTE_JOB** — `[PK9]`:

| macro | value | line |
|---|---|---|
| `MALI_COMPUTE_JOB_LENGTH` | **128** | `[PK9]:6573` |
| `MALI_COMPUTE_JOB_ALIGN` | 128 | `[PK9]:6574` |
| `MALI_COMPUTE_JOB_SECTION_HEADER_OFFSET` | 0 | `[PK9]:6581` |
| `MALI_COMPUTE_JOB_SECTION_PAYLOAD_OFFSET` | 32 | `[PK9]:6588` |
| `MALI_COMPUTE_PAYLOAD_LENGTH` | 96 | `[PK9]:6464` |

Four sections at v7 versus two at v9; 192 bytes versus 128; alignment 64 versus
128. And `[XML7]:602` defines `<struct name="Invocation">` while `[XML9]` defines
it **zero** times. The stub's stated reason is confirmed at byte level.

### 3.2 The exact v9 COMPUTE_JOB layout, byte by byte

The container is an aggregate, not a struct — `[XML9]:1525`:

```xml
<!-- Compute job also covers vertex and geometry operations -->
<aggregate name="Compute Job" align="128">
  <section name="Header" offset="0" type="Job Header"/>
  <section name="Payload" offset="32" type="Compute Payload"/>
</aggregate>
```

The comment on `[XML9]:1524` is worth recording: on this generation the compute
job type also carries vertex and geometry work. Field bits are from
`[XML9]:1508-1522`, packing confirmed against `[PK9]:6473-6500`. Byte offsets
below are absolute within the 128-byte job; payload words start at byte 32.

```
offset  size  field                                   source
------  ----  --------------------------------------  ----------------
0       32    section Header : Job Header             [XML9]:1526
32      96    section Payload: Compute Payload        [XML9]:1527
```

`Compute Payload`, `size="24"` words = 96 bytes (`[XML9]:1508`):

```
word  byte    bits    field                        modifier      source
----  ------  ------  ---------------------------  ------------  --------------
 0    32..35   0..9   Workgroup size X             minus(1)      [XML9]:1509
 0    32..35  10..19  Workgroup size Y             minus(1)      [XML9]:1510
 0    32..35  20..29  Workgroup size Z             minus(1)      [XML9]:1511
 0    32..35     31   Allow merging workgroups     bool          [XML9]:1512
 1    36..39   0..13  Task increment               default 1     [XML9]:1513
 1    36..39  14..15  Task axis                    Task Axis     [XML9]:1514
 2    40..43   0..31  Workgroup count X                          [XML9]:1515
 3    44..47   0..31  Workgroup count Y                          [XML9]:1516
 4    48..51   0..31  Workgroup count Z                          [XML9]:1517
 5    52..55   0..31  Offset X                                   [XML9]:1518
 6    56..59   0..31  Offset Y                                   [XML9]:1519
 7    60..63   0..31  Offset Z                                   [XML9]:1520
 8    64..127  512    Compute : Shader Environment               [XML9]:1521
```

The `Shader Environment` sub-fields, as actually emitted by
`[PK9]:6485-6497`:

```
word  byte      bits    field                        source
----  --------  ------  ---------------------------  -----------
 8    64..67     0..31  compute.attribute_offset     [PK9]:6485
 9    68..71     0..7   compute.fau_count            [PK9]:6486
10-15 72..95        -   zero                         [PK9]:6487-6492
16-17 96..103   0..63   compute.resources            [PK9]:6493-6494
18-19 104..111  0..63   compute.shader               [PK9]:6495-6496
20-21 112..119  0..63   compute.thread_storage       [PK9]:6497-6498
22-23 120..127  0..63   compute.fau                  [PK9]:6499-6500
```

Arithmetic check: 32 header + 96 payload = **128** = `MALI_COMPUTE_JOB_LENGTH`
`[PK9]:6573`. The layout is internally consistent.

Note `compute.fau_count` at word 9, byte 68, bits 0..7. This is the compute-side
counterpart of the graphics Shader Environment FAU count whose absence was the
Phase 4 root cause. It is programmed on the working v9 dispatch path.

---

## 4. Finding: the v9 indirect *dispatch* helper kernel already exists

`[CL_ID]` is the indirect-dispatch helper. Its guard at `[CL_ID]:11` is
`#if (PAN_ARCH >= 6 && PAN_ARCH <= 9)`, so **v9 is included**. Inside, at
`[CL_ID]:38-50`, there is a dedicated v9 branch:

```c
#if PAN_ARCH == 9
   global struct mali_compute_payload_packed *payload =
      (global struct mali_compute_payload_packed *)(indirect_job +
                                                    pan_section_offset(
                                                       COMPUTE_JOB, PAYLOAD));

   pan_unpack(payload, COMPUTE_PAYLOAD, unpacked_job_payloads);
   pan_pack(payload, COMPUTE_PAYLOAD, cfg) {
      memcpy(&cfg, &unpacked_job_payloads, sizeof(cfg));
      cfg.workgroup_count_x = num_x;
      cfg.workgroup_count_y = num_y;
      cfg.workgroup_count_z = num_z;
   }
#else
```

It also patches the job header at `[CL_ID]:30`, setting
`cfg.type = is_no_op ? MALI_JOB_TYPE_NULL : MALI_JOB_TYPE_COMPUTE`, where
`is_no_op` is `num_x * num_y * num_z == 0` (`[CL_ID]:26`).

The kernel is registered for v9: `[SH9]:163` lists
`PANLIB_INDIRECT_DISPATCH = 6`.

Its argument block layout, from the generated static assertions
`[SH9]:118-126`:

```
offset  size  field              source
------  ----  -----------------  -----------
 0       8    cmd                [SH9]:118
 8       4    size_x             [SH9]:119
12       4    size_y             [SH9]:120
16       4    size_z             [SH9]:121
20       4    _pad4              (implicit)
24       8    indirect_job       [SH9]:122
32       8    num_wg_sysval_x    [SH9]:123
40       8    num_wg_sysval_y    [SH9]:124
48       8    num_wg_sysval_z    [SH9]:125
------  ----
total   56                       [SH9]:126
```

And the v9 caller is written, not stubbed: `[DISP]:244-252` calls
`panlib_indirect_dispatch(...)`, then `[DISP]:255-257` submits the compute job as
`indirect ? MALI_JOB_TYPE_NOT_STARTED : MALI_JOB_TYPE_COMPUTE`.

**Conclusion 4 (VERIFIED-SRC).** For indirect *compute dispatch*, v9 has the
kernel, the argument layout, and the caller. The only missing link is
`dispatch_precomp` at `[PRE]:102`. Because the job is deliberately submitted as
`NOT_STARTED` and only the never-dispatched helper would promote it to `COMPUTE`,
`vkCmdDispatchIndirect` on v9 should silently do nothing. **That specific
prediction was not tested in this pass** and remains untested.

---

## 5. Finding: the v9 *graphics* indirect helper kernels are not built at all

This is the decisive result for `vkCmdDrawIndirect`.

`[SH7]` registers **22** helper programs; `[SH9]` registers **14**. The eight
present at v7 and absent at v9 are exactly the graphics indirect helpers:

```
PANLIB_DRAW_INDIRECT_HELPER
PANLIB_DRAW_INDEXED_INDIRECT_HELPER
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_0
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_1
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_2
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_3
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_4
PANLIB_DRAW_INDEX_MINMAX_SEARCH_HELPER_5
```

No program is present at v9 and absent at v7. The difference is one-directional.

The cause is in `[CL_DH]`, 644 lines, which has exactly two arch guards:

| lines | guard | kernels inside |
|---|---|---|
| `[CL_DH]:14`–`:72` | `#if PAN_ARCH >= 10` | `panlib_update_prims_generated_query_restart` `[CL_DH]:20`, `panlib_update_prims_generated_query_indirect` `[CL_DH]:49` |
| `[CL_DH]:74`–`:644` | `#if (PAN_ARCH == 6 \|\| PAN_ARCH == 7)` | `panlib_patch_draw_vertex_dcd` `:123`, `panlib_patch_draw_tiler_dcd` `:164`, `panlib_patch_varying_bufs` `:258`, `panlib_patch_attrib_buf` `:296`, `panlib_patch_attrib` `:370`, `panlib_patch_job_type_header` `:388`, `panlib_patch_draw` `:404`, **`panlib_draw_indirect_helper` `:472`**, **`panlib_draw_indexed_indirect_helper` `:527`**, **`panlib_draw_index_minmax_search_helper` `:590`** |

`PAN_ARCH == 9` satisfies neither guard. `[MB]:8` compiles `draw_helper.cl` for
every arch in the loop and `[MB]:22` passes `-DPAN_ARCH=@0@`, so the file *is*
compiled for v9 — it simply emits no kernels.

**Conclusion 5 (VERIFIED-SRC).** Graphics indirect draw on v9 is missing three
things, not one: the helper kernels are not written for v9, the precomp
dispatcher is stubbed, and the entry points are empty.

---

## 6. Finding: the v9 draw-indirect entry points are empty

Quoted in full from `[DRAW]:3025-3040`:

```c
VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndirect)(VkCommandBuffer commandBuffer, VkBuffer _buffer,
                                VkDeviceSize offset, uint32_t drawCount,
                                uint32_t stride)
{
   /* TODO v9: belum diimplementasikan. */
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdDrawIndexedIndirect)(VkCommandBuffer commandBuffer,
                                       VkBuffer _buffer, VkDeviceSize offset,
                                       uint32_t drawCount, uint32_t stride)
{
   /* TODO v9: belum diimplementasikan. */
}
```

No parameter is read. No job is emitted. No error is returned.

---

## 7. Runtime confirmation — VERIFIED-HW

The survey predicts that `vkCmdDrawIndirect` accepts the call and draws nothing,
without error and without a GPU fault. Tested on the device, 3 runs per case,
fingerprints identical:

| case | API path | non-black | centre | jobs observed | verdict |
|---|---|---|---|---|---|
| `direct` | `vkCmdDraw(3,1,0,0)` | 512 / 4096 | 255,0,0,255 | 3 `DONE`, 1 `NOT_STARTED` | control OK |
| `draw_indirect` | `vkCmdDrawIndirect(count=1, stride=16)` | **0 / 4096** | 0,0,0,255 | **1 `DONE` only** | prediction held |

The indirect case emitted a valid `VkDrawIndirectCommand`
(`vertexCount=3, instanceCount=1, firstVertex=0, firstInstance=0`) into a
`VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT` buffer, and `vkCmdDrawIndirect` was
resolved and non-NULL. `vkQueueSubmit` and `vkQueueWaitIdle` both returned
`VK_SUCCESS`.

The single observed job in the indirect case is the fragment/clear job. No
`MALLOC_VERTEX` job was created, matching an entry point that emits nothing.
Zero GPU faults; every `exception_status` was `0x01 (DONE)` or
`0x00 (NOT_STARTED)`.

Evidence: `T4.4.0_direct*.log`, `T4.4.0_draw_indirect*.log`.

---

## 8. Independent corroboration from ARM's own kernel source

The first version of this survey listed "whether ARM documentation describes an
indirect mechanism absent from Mesa's genxml" as an open gap. ARM's official
kbase release is present in the project folder, so the gap is now closed as far
as that source can close it.

**Source `[ARM]`:** `VX504X08X-SW-99002-r54p1-12eac0.tar`, ARM's Mali kbase
r54p1 driver release — the same driver version running on this device. Extracted
locally: **513 files, 7.6 MB**.

### 8.1 The word "indirect" does not appear anywhere in it

A case-insensitive search of all 513 files under
`driver/product/kernel/drivers/gpu/arm/midgard` returned **0 matching files**.
The three UAPI headers individually:

| header | lines | `indirect` hits |
|---|---|---|
| `include/uapi/gpu/arm/midgard/jm/mali_base_jm_kernel.h` | 936 | **0** |
| `include/uapi/gpu/arm/midgard/csf/mali_base_csf_kernel.h` | 639 | **0** |
| `include/uapi/gpu/arm/midgard/mali_base_kernel.h` | 641 | **0** |

### 8.2 The complete ARM job requirement list has no indirect class

From `[ARM] include/uapi/gpu/arm/midgard/jm/mali_base_jm_kernel.h`, all 31
`BASE_JD_REQ_*` definitions. The five that name GPU work classes:

| macro | value | meaning per ARM's comment | line |
|---|---|---|---|
| `BASE_JD_REQ_DEP` | `0` | dependency-only atom | :173 |
| `BASE_JD_REQ_FS` | `1 << 0` | "Requires fragment shaders" | :177 |
| `BASE_JD_REQ_CS` | `1 << 1` | "Requires compute shaders" | :190 |
| `BASE_JD_REQ_T` | `1 << 2` | "Requires tiling" | :193 |
| `BASE_JD_REQ_CF` | `1 << 3` | "Requires cache flushes" | :196 |
| `BASE_JD_REQ_V` | `1 << 4` | "Requires value writeback" | :199 |

The remaining 25 are flags and soft-job selectors: `FS_AFBC` (`1 << 13`),
`EVENT_COALESCE` (`1 << 5`), `COHERENT_GROUP` (`1 << 6`), `PERMON` (`1 << 7`),
`EXTERNAL_RESOURCES` (`1 << 8`), `SOFT_JOB` (`1 << 9`) plus its twelve
sub-selectors, `ONLY_COMPUTE` (`1 << 10`), `SPECIFIC_COHERENT_GROUP` (`1 << 11`),
`EVENT_ONLY_ON_FAILURE` (`1 << 12`), `SKIP_CACHE_START` (`1 << 15`),
`SKIP_CACHE_END` (`1 << 16`), `JOB_SLOT` (`1 << 17`),
`LIMITED_CORE_MASK` (`1 << 20`), and the two composite masks `ATOM_TYPE` and
`SOFT_JOB_TYPE`.

**There is no job class that means "read your parameters from a buffer."** ARM's
Job Manager interface says only *run vertex*, *run compute*, *run tiler*, *run
fragment*, *flush caches*, *write a value*. Parameters always come from a
descriptor that something else has already written.

### 8.3 ARM's own wording corroborates the Mesa genxml comment

`[ARM] mali_base_jm_kernel.h:180-189`, verbatim:

```
/* Requires compute shaders
 *
 * This covers any of the following GPU job types:
 * - Vertex Shader Job
 * - Geometry Shader Job
 * - An actual Compute Shader Job
 *
 * Compare this with BASE_JD_REQ_ONLY_COMPUTE, which specifies that the
 * job is specifically just the "Compute Shader" job type, and not the "Vertex
 * Shader" nor the "Geometry Shader" job type.
 */
```

That matches the Mesa comment quoted in §3.2 (`[XML9]:1524`, *"Compute job also
covers vertex and geometry operations"*) — two independently authored sources,
one from ARM and one from Mesa, describing the same hardware behaviour.

### 8.4 Honest limit of this corroboration

kbase is the **kernel** driver. It governs atom submission, memory and
scheduling; it never parses job descriptors. A hardware indirect mechanism living
entirely inside a descriptor would therefore not necessarily appear in kbase, so
§8 is corroboration and not proof on its own. Its weight comes from agreeing with
§1: Mesa's descriptor definitions and ARM's submission interface both show
nothing, from opposite ends of the stack.

---

## 9. Cross-check against other builds and notes in the project folder

### 9.1 A third-party v9 build shows no v9 indirect helpers

`PanVK-G57-Custom/libvulkan_panfrost.so` in the project folder is a 131 MB
binary-only distribution — `meta.json` declares
`"driverVersion": "Mesa 26.3.0-devel Vulkan 1.3.354"`, build ID
`11a969d24dc3fbe7c699d6bd02641c5ea16d1bf6`. No source is included.

Probing it for helper names is inconclusive by itself, because one ICD links
every arch variant, so v6/v7 strings appear regardless. Compared against our own
build as the control:

| string | third-party build | our build |
|---|---|---|
| `panlib_draw_indirect_helper` | 4 | 4 |
| `panlib_draw_indexed_indirect_helper` | 4 | 4 |
| `panlib_draw_index_minmax_search_helper` | 19 | 19 |
| `panlib_indirect_dispatch` | 5 | 5 |
| `dispatch_precomp` | 21 | 14 |

The counts match everywhere except `dispatch_precomp`. The seven extra strings
are function signatures, one per arch — `void panvk_v6_dispatch_precomp(...)`
through `void panvk_v14_dispatch_precomp(...)` — i.e. embedded
`__PRETTY_FUNCTION__`-style text from a different build configuration, not extra
code paths. `panvk_v9_dispatch_precomp` appears as a plain symbol name in **both**
builds, which is expected: the v9 stub is still a real function.

**No evidence that any other build implemented v9 graphics indirect draw.**

### 9.2 Third-party notes reached the same conclusion independently

**EXTERNAL-UNVERIFIED.** Working notes from another party in the project folder,
searched for the relevant terms (66 `draw_helper` hits, 51 `precomp`, 0
`dispatch_precomp`). Quoted:

> `CmdDrawIndirect/CmdDrawIndexedIndirect. Traced the Bifrost mechanism to
> libpan/draw_helper.cl — GPU-side precompiled kernels
> (panlib_draw_indirect_helper, …)`

> `jm/panvk_vX_cmd_draw.c dan panvk_vX_cmd_precomp.c (grafis, belum diporting)
> di-STUB jadi no-op untuk PAN_ARCH>=9`

> `PAN_ARCH=9 yang gagal compile total di cmd_draw.c/cmd_dispatch.c/cmd_precomp.c
> (RSD, INVOCATION, PARAMETERS, DRAW section semua undeclared untuk v9)`

The last line matters most. It states the same cause this survey established at
byte level in §3.1 — that `INVOCATION`, `PARAMETERS` and `DRAW` are undeclared at
v9 — and it was reached by a different route, from compiler errors rather than
from the generated pack headers.

These notes are recorded as corroboration only. Nothing here was promoted to
VERIFIED on their basis; §3.1 stands on `[PK7]` and `[PK9]` alone.

---

## 10. Verdict

**`vkCmdDrawIndirect` on v9/JM: NOT IMPLEMENTED, and not implementable by
descriptor programming alone.**

The gate question for sub-phase 4.4 was whether a hardware indirect path exists.
It does not — on any Job Manager generation. Three sources, from opposite ends of
the stack and authored independently, agree:

| source | what it shows | section |
|---|---|---|
| Mesa `v9.xml` + generated pack headers | 63 structs, zero indirect descriptors | §1 |
| ARM kbase r54p1 UAPI | 31 job requirements, zero indirect job class | §8 |
| runtime on this device | `vkCmdDrawIndirect` draws nothing, no fault, no vertex job | §7 |

Implementing it means writing a software emulation, and the work is larger than
the empty function bodies suggest:

| # | Work item | Evidence it is missing |
|---|---|---|
| 1 | Port `dispatch_precomp` to v9's single `PAYLOAD` section | `[PRE]:102-110` empty; sections differ per §3.1 |
| 2 | Extend `draw_helper.cl` guards to admit `PAN_ARCH == 9` and port the seven `panlib_patch_*` helpers to v9 descriptor layouts | `[CL_DH]:74` excludes v9; 8 programs absent from `[SH9]` |
| 3 | Implement the two entry points | `[DRAW]:3030`, `[DRAW]:3038` |

Item 2 is the substantial one. The Bifrost patch helpers rewrite Bifrost DCDs,
vertex attribute buffers and varying buffers; v9's equivalents are different
descriptors, so this is a port and not a guard change.

### Recommended disposition

Close sub-phase 4.4 as **NOT IMPLEMENTED** with this survey as the evidence, and
do not write code now. Reasons:

* Item 1 is a prerequisite shared with `vkCmdDispatchIndirect`, and it is the
  cheapest of the three. If indirect work is wanted, that is where to start,
  because the v9 kernel and caller already exist (§4) and only the dispatcher is
  missing.
* Item 2 touches the working draw path's descriptor construction. Phase 4.1,
  4.2 and 4.3 results were obtained against the current baseline; a helper port
  risks them for a feature nothing downstream needs yet.
* Texture sampling (Phase 5) is worth more than indirect draw for anything that
  would actually run on this driver.

### What this survey does not establish

* Whether an ARM **architecture specification** describes an indirect mechanism
  absent from both Mesa's genxml and ARM's kbase. No such specification is
  present on this machine. ARM's kernel driver source *was* consulted and shows
  nothing (§8), but as §8.4 states, kbase never parses job descriptors, so it
  cannot rule out a descriptor-only mechanism by itself.
* Whether `vkCmdDispatchIndirect` silently does nothing at runtime. Predicted in
  §4, untested.
* Whether the six `MINMAX_SEARCH_HELPER` variants correspond to index sizes or
  to something else. Not investigated.
