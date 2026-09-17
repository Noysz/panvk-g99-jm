# Resource table findings

**Status: IN PROGRESS.** Multi-set behaviour is still untested. The numbers below
hold for the current graphics workload only. They are *derived* values, not
architectural constants, and they change with the shader's descriptor usage.

This page separates two label classes deliberately:

* **VERIFIED-HW** — observed in runtime output from this device.
* **VERIFIED-SRC** — established from current source, genxml, or generated
  headers, but **not** measured on hardware.

The distinction is not pedantry. An earlier version of this page stated a
derivation that was source-plausible, arithmetically correct in its final value,
and **wrong about the actual runtime inputs**. Conflating the two classes is what
let it survive review. See [Correction](#correction-2026-09-17) below.

Evidence:

* runtime —
  [`run1`](../evidence/logs/restab_runtime_T4.2.1_run1_20260917.log),
  [`run2`](../evidence/logs/restab_runtime_T4.2.1_run2_20260917.log),
  [`run3`](../evidence/logs/restab_runtime_T4.2.1_run3_20260917.log),
  [`summary`](../evidence/logs/restab_runtime_T4.2.1_SUMMARY_20260917.txt)
* source dump —
  [`resource_table_audit`](../evidence/schema/PANVK_G57_resource_table_audit_20260917-013808.log)
* instrumentation —
  [`patches/0030-phase4-restab-instrumentation.patch`](../patches/0030-phase4-restab-instrumentation.patch)

## Current numbers

| Quantity | Value | Class | Source |
|---|---|---|---|
| `used_set_mask` | **`0x0`** | VERIFIED-HW | runtime, 3 runs |
| `first_unused_set` | **0** | VERIFIED-HW | runtime, 3 runs |
| `res_count` | **4** | VERIFIED-HW | runtime, 3 runs |
| `RESOURCE` descriptor size | **16 B** | VERIFIED-HW | runtime `entry_bytes=16` |
| Resource table size | **64 B per stage** | VERIFIED-HW | runtime `table_bytes=64` |
| `res_table` preparations per draw | **2** | VERIFIED-HW | runtime |
| driver-set size, stage A | **544 B** | VERIFIED-HW | runtime |
| driver-set size, stage B | **32 B** | VERIFIED-HW | runtime |
| `MALI_RESOURCE_TABLE_SIZE_ALIGNMENT` | **4** | VERIFIED-SRC | generated `v9_pack.h:6381` |

Reproduce with:

```bash
PANVK_DEBUG_RESTAB=1 PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 ./triangle_draw_test_v2
```

The instrumentation is env-gated and emits nothing when the variable is unset, so
the validated baseline is unaffected. Pixel output was 512/4096 on all three
runs, matching the pre-instrumentation baseline exactly.

## Derivation — why 4

`src/panfrost/vulkan/panvk_vX_cmd_desc_state.c`:

```c
uint32_t first_unused_set = util_last_bit(desc_info->used_set_mask);
uint32_t res_count =
   ALIGN_POT(1 + first_unused_set, MALI_RESOURCE_TABLE_SIZE_ALIGNMENT);
```

Runtime shows `used_set_mask = 0x0`, so `util_last_bit(0) = 0` and
`first_unused_set = 0`. The `1 +` is the driver set occupying entry 0. Therefore
`ALIGN_POT(1, 4)` = **4**, and the table is `4 × 16 B` = **64 B**.

**The current workload binds no application descriptor set at all.** Entry 0 is
the driver set; entries 1–3 are zero padding. Nothing in the tested path
exercises an application set.

A workload that does bind sets produces a larger table. **Do not hardcode 4
or 64.**

### Correction, 2026-09-17

**SUPERSEDED.** The previous version of this section read:

> The test workload uses descriptor set 0 only, so `used_set_mask = 0b1`,
> `first_unused_set = 1`. […] Therefore `ALIGN_POT(2, 4)` = **4**.

That is wrong on both inputs. Runtime measurement gives `used_set_mask = 0x0` and
`first_unused_set = 0`, so the operative expression is `ALIGN_POT(1, 4)`, not
`ALIGN_POT(2, 4)`.

`ALIGN_POT(1, 4)` and `ALIGN_POT(2, 4)` are **both 4**. The final number was
right, so nothing downstream broke and no test failed. The error was invisible
precisely because it was harmless.

Two things worth extracting from this:

1. The original claim was labelled VERIFIED while resting on a **source dump**,
   not a runtime measurement. The linked audit log is a `grep` of the function
   body — it never contained a value of `used_set_mask`.
2. The substantive consequence is not the arithmetic. It is that the workload was
   believed to bind one descriptor set and in fact binds **zero**. Any claim of
   the form "descriptor sets work on v9 because the triangle renders" was
   unfounded, and no such claim should be made until 4.2.2 onwards run.

## Two preparations per draw

Runtime shows `cmd_prepare_shader_res_table()` running **twice** per draw, once
per shader stage, each producing its own 64 B table. The driver-set sizes differ:

```
stage A: entry[0] DRIVER_SET size=544 contains_desc=1
stage B: entry[0] DRIVER_SET size=32  contains_desc=1
```

544 B is consistent with the stage that carries vertex attributes and vertex
buffers, 32 B with a stage carrying only the dummy sampler. **Which stage is
which is not established** — the instrumentation does not currently print the
stage, and the ordering alone is not proof. Treat the stage attribution as
INFERENCE until the print is extended.

## `RESOURCE` layout from v9.xml — VERIFIED-SRC

```xml
<struct name="Resource" size="4" align="64">
  <field name="Address"              start="0:0"  size="56" type="address"/>
  <field name="Contains descriptors" start="1:24" size="1"  type="bool" default="true"/>
  <field name="Size"                 start="2:0"  size="64" type="uint"/> <!-- bytes -->
</struct>
```

`size="4"` is **4 words = 16 bytes**, not 4 bytes. This is now also VERIFIED-HW
via `entry_bytes=16`.

`align="64"` is a 64-byte alignment requirement on the table base, a separate
thing from the 64-byte total table size — the two being equal here is a
coincidence of this workload.

## The count is smuggled in the pointer's low bits — VERIFIED-SRC

```c
shader_desc_state->res_table = ptr.gpu | res_count;
```

and read back:

```c
uint32_t count = (shader_desc_state->res_table & BITFIELD_MASK(6));
assert(count % MALI_RESOURCE_TABLE_SIZE_ALIGNMENT == 0);
```

The low **6 bits** of `res_table` carry `res_count`; the upper bits are the GPU
address. This works because `align="64"` guarantees the low 6 bits of the real
address are zero. Runtime confirms the encoding: with `ptr.gpu` ending in `300`,
`res_table` reads `...304`. Two implications:

* `res_table` must never be dereferenced without masking.
* `res_count` cannot exceed 63, and must stay a multiple of 4.

## Table population — VERIFIED-SRC, partially exercised

Entry 0 is always the **driver set** — vertex attributes, the dummy sampler,
dynamic buffers, vertex buffers. Entries `1 .. first_unused_set` are the
application's descriptor sets. Entries `first_unused_set+1 .. res_count-1` are
zero padding, written explicitly so a stale table from an earlier draw is not
reused.

With `first_unused_set = 0` on the current workload, the middle range is empty
and **only the entry-0 and padding paths have ever executed**. The
application-set branch is unexercised code.

Ordering matters: `cmd_prepare_shader_res_table()` must run **after** the driver
set is built, because it is what fills `res_table[0]`.

## The failure mode this explains — VERIFIED-SRC

`res_table` is the only thing the job reads for descriptors — see `cfg.resources`
(vertex) and `cfg.shader.resources` (fragment) in the v9 draw path. It is
populated *only* by `panvk_per_arch(cmd_prepare_shader_res_table)()`. If that
function is never called, `res_table` stays `0` and the GPU reads its resource
table from address zero. There is no fault; the descriptors are simply garbage.
The v9 path now calls it for both stages, and zeroes the fragment entry when
there is no fragment shader.

**This is source reasoning, not a measured result.** The negative control that
would make it VERIFIED-HW — deliberately skipping the call and observing
silent-garbage-without-fault — is planned as T4.2.4 and has not run.

## Related constants seen in the source audit — VERIFIED-SRC

* `cfg.table = 61` — a reserved table index used by the descriptor-copy path.
* `pan_res_handle(62, 0)` — table 62, used for the resource-table handle in
  `panvk_vX_shader.c`.

Observed, not explained. **NOT RESOLVED:** the full meaning of the reserved table
indices 61 and 62.

## Open

* Multi-set tables are **UNTESTED**. Planned as T4.2.2 (2 sets, expect
  `res_count = 4`), T4.2.3 (4 sets, expect `res_count = 8`), T4.2.5
  (non-contiguous sets 0 and 3, expect `used_set_mask = 0b1001`,
  `first_unused_set = 4`, `res_count = 8`).
* The application-set branch of the population loop is **UNEXERCISED**.
* Skipping `cmd_prepare_shader_res_table()` is predicted to yield silent garbage
  with no GPU fault — **HYPOTHESIS**, planned as T4.2.4.
* Which stage owns the 544 B versus the 32 B driver set is **INFERENCE**.
* Dynamic buffer entries are emitted but **UNTESTED** on v9.
* Whether `align="64"` is a hard hardware requirement or genxml conservatism is
  **NOT RESOLVED**.
