# `KBASE_IOCTL_GET_GPUPROPS` works without a context — and that's what enumeration needs

**TL;DR:** on kbase you can read the full GPU property table from `/dev/mali0`
*before* creating a context (before `SET_FLAGS`, even before `VERSION_CHECK`).
That is precisely the state `vkEnumeratePhysicalDevices` runs in, so a
`kbase_kmod.c` backend for `pan_kmod` can fill `pan_kmod_dev_props` there with
no context, no allocation, and no job submission.

Test program: [`tests/test_kbase3.c`](../tests/test_kbase3.c).
Full run output: [`results/gpuprops-g57-r54p1.txt`](../results/gpuprops-g57-r54p1.txt).
Device: Mali-G57 MC2, MT6789U, kbase r54p1 / UAPI 11.46. Exit code 0, all checks passed.

## The ioctl

```c
struct kbase_ioctl_get_gpuprops {
    uint64_t buffer; /* offset  0 */
    uint32_t size;   /* offset  8 */
    uint32_t flags;  /* offset 12 */
};
#define KBASE_IOCTL_GET_GPUPROPS _IOW(0x80, 3, struct kbase_ioctl_get_gpuprops)
/* == 0x40108003 */
```

Those field offsets were confirmed against **this device's own binary**, not just
the 11.43 mirror header. From `kbase_ioctl+0x154..0x1a0`:

```
1ce34: mov  w9, #0x8003
1ce38: movk w9, #0x4010, lsl #16   ; 0x40108003 = _IOW(0x80, 3, 16B)
1ce3c: cmp  w1, w9
1ce40: b.ne 1cfa0                  ; falls through to the kctx gate
1ce44: add  x0, sp, #0x28
1ce4c: mov  w2, #0x10              ; sizeof(struct) == 16
1ce54: bl   _inline_copy_from_user
1ce5c: ldr  w8, [sp, #52]          ; offset 12 -> .flags
1ce64: cbnz w8, 1d78c              ; flags != 0 -> dev_err -> -EINVAL
1ce68: ldr  w8, [sp, #48]          ; offset  8 -> .size
1ce6c: ldr  w19, [x21, #6664]      ; kbdev->gpu_props.prop_buffer_size
1ce70: cbz  w8, 1d5f0              ; size == 0 -> return required size
1ce74: cmp  w8, w19
1ce78: b.cs 1d31c                  ; size >= required -> copy_to_user
1ce7c: mov  w19, #0xffffffea       ; else -EINVAL (-22)
```

## Why no context is needed

The handler lives in the **first** switch of `kbase_kfile_ioctl()` — the group
commented *"Only these ioctls are available until setup is complete"*
(`mali_kbase_core_linux.c:1855-1894`), which sits **above** the
`kctx = kbase_file_get_kctx_if_setup_complete(kfile)` gate at line 1896. On this
device that gate is at `1cfa0` (`ldr w9,[x20,#32]` = `setup_state`; `cmp w9,#4`
= `KBASE_FILE_COMPLETE`).

`test_kbase3.c` doesn't take that on faith — it probes twice before any context
exists (before `VERSION_CHECK`, and after it but before `SET_FLAGS`). Both
returned the same 749 bytes as the post-context call.

## Calling convention

Two-pass, and all three rules were confirmed by negative tests *and* by the
disassembly above:

1. `size = 0` → returns the required byte count (749 here, 83 properties).
2. Allocate, call again with `size >= required` → kernel fills the buffer.
3. `flags` must be 0, `size < required` → `-EINVAL`.

Buffer encoding, from `kbase_gpuprops_populate_user_buffer()` in
`mali_kbase_gpuprops.c:727`:

```
key = (prop_id << 2) | size_code    /* size_code: 0=u8, 1=u16, 2=u32, 3=u64 */
```

Key is u32 little-endian, immediately followed by the value. **Tightly packed —
no padding, no terminator.** Total = sum of `(4 + size)`. Because there's no
padding, values are not guaranteed aligned: read them with `memcpy`, not a cast.

## Values on Mali-G57 MC2 (G99)

```
RAW_GPU_ID          0x90930010   -> product 0x9093, arch v9, r0p1
RAW_SHADER_PRESENT  0x5          -> 2 cores, bits 0 and 2 (MC2)
RAW_TILER_PRESENT   0x1          RAW_L2_PRESENT 0x1   L2_NUM_L2_SLICES 1
RAW_AS_PRESENT      0xff         -> 8 address spaces
RAW_JS_PRESENT      0x7          -> 3 job slots (JM)
RAW_MMU_FEATURES    0x2830       -> VA 48-bit, PA 40-bit
RAW_L2_FEATURES     0x8120206    -> 64B line, 256KB, 1 slice
RAW_TILER_FEATURES  0x809        -> 512B bins, 8 active levels
RAW_THREAD_FEATURES 0x48000      -> regs 32768, task_queue 4, tgs 0
MAX_THREADS 1024, MAX_WORKGROUP_SIZE 512, MAX_BARRIER_SIZE 512
TLS_ALLOC 1024, RAW_THREAD_TLS_ALLOC 0
RAW_COHERENCY_MODE 31 (COHERENCY_NONE)
```

⚠️ **`SHADER_PRESENT` is `0x5`, not `0x3`** — the two cores are at bits 0 and 2,
not contiguous. Use `popcount`, never `mask + 1`, to derive the core count.

## Mapping to `pan_kmod_dev_props`

`panfrost_kmod.c` fills these via `DRM_PANFROST_PARAM_*`. The kbase equivalents:

| `pan_kmod_dev_props` field | `KBASE_GPUPROP_*` | id |
|:---|:---|---:|
| `gpu_id` | `RAW_GPU_ID` | 55 |
| `shader_present` | `RAW_SHADER_PRESENT` | 25 |
| `tiler_features` | `RAW_TILER_FEATURES` | 51 |
| `mem_features` | `RAW_MEM_FEATURES` | 31 |
| `mmu_features` | `RAW_MMU_FEATURES` | 32 |
| `l2_features` | `RAW_L2_FEATURES` | 29 |
| `texture_features[0..2]` | `RAW_TEXTURE_FEATURES_0..2` | 52, 53, 54 |
| `texture_features[3]` | `RAW_TEXTURE_FEATURES_3` | 81 |
| `max_threads_per_core` | `RAW_THREAD_MAX_THREADS` | 56 |
| `max_threads_per_wg` | `RAW_THREAD_MAX_WORKGROUP_SIZE` | 57 |
| `max_tasks_per_core`, `num_registers_per_core` | derived from `RAW_THREAD_FEATURES` | 59 |
| `max_tls_instance_per_core` | `RAW_THREAD_TLS_ALLOC` | 83 |
| `is_io_coherent` | `RAW_COHERENCY_MODE` | 60 |
| `timestamp_frequency` | *not in gpuprops* — use `KBASE_IOCTL_GET_CPU_GPU_TIMEINFO` (nr 50) | — |
| `afbc_features` | *no kbase equivalent found* — **open question** | — |

## Traps in the property table (all traced to ARM source — don't re-derive)

- **`GPU_FREQ_KHZ_MAX` is fake.** It reports 5000, which is a hardcoded
  `DEFAULT_GPU_FREQ_KHZ_MAX` (`mali_kbase_gpuprops.c:843`), not a measured or
  probed value. Useless.
- **`COHERENCY_COHERENCY` is not a coherency enum.** It is literally assigned
  `regdump->mem_features` (`mali_kbase_gpuprops.c:666`). The real answer is
  `RAW_COHERENCY_MODE = 31 = COHERENCY_NONE` → `is_io_coherent = false`.
- **`NUM_EXEC_ENGINES` / `RAW_CORE_FEATURES` are 0 and meaningless here.**
  `CORE_FEATURES` bits[3:0] only encode exec-engine count on tGOx
  (`mali_kbase_gpuprops.c:655-662`).
- **`THREAD_FEATURES` field positions are gated on `MALI_USE_CSF`, not on
  Bifrost-vs-Valhall.** JM layout: regs `[15:0]`, task_queue `[23:16]`, tgs
  `[29:24]` (`:691-693`). CSF layout: regs `[21:0]`, task_queue `[31:24]`
  (`:687-688`).

### Possible Mesa bug this exposes (reported here, not fixed)

`src/panfrost/lib/kmod/panfrost_kmod.c:140` computes:

```c
props->max_tasks_per_core = MAX2(thread_features >> 24, 1);
```

`>> 24` is the **CSF** field position, but `panfrost_kmod.c` is the JM-only
backend. With the JM layout the field is at `[23:16]`. On G57
(`THREAD_FEATURES = 0x48000`) that yields `1` instead of `4`, which shrinks
`pan_calc_wls_instances()` (`src/panfrost/lib/pan_desc.h:309`) by 4×.

This is a **performance** issue, not a correctness one, and it is **unverified
against real hardware** — it is a source-reading result only. Someone with a
working PanVK on a JM part should measure before anyone patches it.
