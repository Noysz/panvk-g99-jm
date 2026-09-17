# Resource table findings

**Status: IN PROGRESS.** The numbers below are verified for the current
single-descriptor-set workload. They are *derived* values, not architectural
constants, and they change with the shader's descriptor usage.

Audit output:
[`evidence/schema/PANVK_G57_resource_table_audit_20260917-013808.log`](../evidence/schema/PANVK_G57_resource_table_audit_20260917-013808.log)

## Current numbers — VERIFIED

| Quantity | Value | Where it comes from |
|---|---|---|
| `res_count` | **4** | computed, see derivation below |
| `RESOURCE` descriptor size | **16 B** | `v9.xml`, `size="4"` (4 x 32-bit words) |
| Resource table size | **64 B per stage** | `res_count` x 16 B |
| `MALI_RESOURCE_TABLE_SIZE_ALIGNMENT` | **4** | generated `v9_pack.h:6381` |

## Derivation — why 4, not "because v9"

`src/panfrost/vulkan/panvk_vX_cmd_desc_state.c`:

```c
uint32_t first_unused_set = util_last_bit(desc_info->used_set_mask);
uint32_t res_count =
   ALIGN_POT(1 + first_unused_set, MALI_RESOURCE_TABLE_SIZE_ALIGNMENT);
```

The test workload uses descriptor set 0 only, so `used_set_mask = 0b1`,
`first_unused_set = 1`. The `1 +` is the driver set occupying entry 0. Therefore
`ALIGN_POT(2, 4)` = **4**, and the table is `4 x 16 B` = **64 B**.

A workload using more sets produces a larger table. **Do not hardcode 4 or 64.**

## `RESOURCE` layout from v9.xml

```xml
<struct name="Resource" size="4" align="64">
  <field name="Address"              start="0:0"  size="56" type="address"/>
  <field name="Contains descriptors" start="1:24" size="1"  type="bool" default="true"/>
  <field name="Size"                 start="2:0"  size="64" type="uint"/> <!-- bytes -->
</struct>
```

`size="4"` is **4 words = 16 bytes**, not 4 bytes. `align="64"` is a 64-byte
alignment requirement on the table base, which is a separate thing from the
64-byte total table size — the two being equal here is a coincidence of this
workload.

## The count is smuggled in the pointer's low bits

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
address are zero. Two implications:

* `res_table` must never be dereferenced without masking.
* `res_count` cannot exceed 63, and must stay a multiple of 4.

## Table population

Entry 0 is always the **driver set** — vertex attributes, the dummy sampler,
dynamic buffers, vertex buffers. Entries `1 .. first_unused_set` are the
application's descriptor sets. Entries `first_unused_set+1 .. res_count-1` are
zero padding, written explicitly so a stale table from an earlier draw is not
reused.

Ordering matters: `cmd_prepare_shader_res_table()` must run **after** the driver
set is built, because it is what fills `res_table[0]`.

## The failure mode this explains

`res_table` is the only thing the job reads for descriptors — see
`cfg.resources` (vertex) and `cfg.shader.resources` (fragment) in the v9 draw
path. It is populated *only* by
`panvk_per_arch(cmd_prepare_shader_res_table)()`. If that function is never
called, `res_table` stays `0` and the GPU reads its resource table from address
zero. There is no fault; the descriptors are simply garbage. The v9 path now
calls it for both stages, and zeroes the fragment entry when there is no
fragment shader.

## Related constants seen in the audit

* `cfg.table = 61` — a reserved table index used by the descriptor-copy path.
* `pan_res_handle(62, 0)` — table 62, used for the resource-table handle in
  `panvk_vX_shader.c`.

These are noted as observed, not explained. **NOT RESOLVED:** the full meaning of
the reserved table indices 61 and 62.

## Open

* Multi-set and multi-stage tables are **UNTESTED**.
* Dynamic buffer entries are emitted but **UNTESTED** on v9.
* Whether `align="64"` is a hard hardware requirement or genxml conservatism is
  **NOT RESOLVED**.
