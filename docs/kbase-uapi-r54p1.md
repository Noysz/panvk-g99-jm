# kbase UAPI surface — Mali-G57 / MT6789 / r54p1 (UAPI 11.46)

Extracted from `mali_kbase_mt6789_a16w_jm.ko`, pulled from this device's live
`vendor_dlkm` (build.prop `ro.tran.vendor_dlkm.version=MT6789-16.3.0.145_STB260813215254`,
Aug 13 2026) — not an old firmware dump.

Reproduce it yourself:

```bash
KO=mali_kbase_mt6789_a16w_jm.ko
nm -S $KO | grep -w kbase_ioctl        # 0x1cce0, size 0xfb0
objdump -d --start-address=0x1cce0 --stop-address=0x1dc90 -j .text $KO > kbase_ioctl.asm
```

**Gotcha when reading the disassembly.** The dispatch is a *binary-search tree*
built from `mov w9,#imm16` + `movk w9,#imm16,lsl #16` + `cmp w1, w9`. Only
comparisons followed by `b.eq`/`b.ne` are real ioctl cases; ones followed by
`b.gt`/`b.le` are search-tree **pivots**, not ioctls. Extracting naively (every
`cmp`) yields 43 entries with bogus duplicates. Correct counts here: 20 `b.eq` +
15 `b.ne` (35 equality tests, 33 unique values) and 12 pivots. There is no jump
table (`br x*` appears 0 times), so the whole dispatch is accounted for.

Names were matched against ARM's official GPL headers — not guessed:
`ExtremeXT/valhall_drivers` →
`driver/product/kernel/include/uapi/gpu/arm/midgard/{,jm/}mali_kbase_*ioctl.h`
(that mirror sits at `BASE_UK_VERSION_MAJOR 11` / `MINOR 43`).

## The 33 ioctls this device dispatches

| nr | dir | size | name | header |
|---:|:---|---:|:---|:---|
| 0 | _IOWR | 4 | KBASE_IOCTL_VERSION_CHECK | jm |
| 1 | _IOW | 4 | KBASE_IOCTL_SET_FLAGS | common |
| 2 | _IOW | 16 | KBASE_IOCTL_JOB_SUBMIT | jm |
| 3 | _IOW | 16 | KBASE_IOCTL_GET_GPUPROPS | common |
| 5 | _IOWR | 32 | KBASE_IOCTL_MEM_ALLOC | common |
| 6 | _IOWR | 16 | KBASE_IOCTL_MEM_QUERY | common |
| 7 | _IOW | 8 | KBASE_IOCTL_MEM_FREE | common |
| 12 | _IOR | 4 | KBASE_IOCTL_DISJOINT_QUERY | common |
| 13 | _IOW | 16 | KBASE_IOCTL_GET_DDK_VERSION | common |
| 14 | _IOW | 24 | KBASE_IOCTL_MEM_JIT_INIT | common |
| 15 | _IOW | 32 | KBASE_IOCTL_MEM_SYNC | common |
| 16 | _IOWR | 24 | KBASE_IOCTL_MEM_FIND_CPU_OFFSET | common |
| 17 | _IOR | 4 | KBASE_IOCTL_GET_CONTEXT_ID | common |
| 18 | _IOW | 4 | KBASE_IOCTL_TLSTREAM_ACQUIRE | common |
| 20 | _IOW | 16 | KBASE_IOCTL_MEM_COMMIT | common |
| 21 | _IOWR | 32 | KBASE_IOCTL_MEM_ALIAS | common |
| 22 | _IOWR | 24 | KBASE_IOCTL_MEM_IMPORT | common |
| 23 | _IOW | 24 | KBASE_IOCTL_MEM_FLAGS_CHANGE | common |
| 24 | _IOW | 32 | KBASE_IOCTL_STREAM_CREATE | common |
| 25 | _IOW | 4 | KBASE_IOCTL_FENCE_VALIDATE | common |
| 27 | _IOW | 16 | KBASE_IOCTL_MEM_PROFILE_ADD | common |
| 28 | _IOW | 16 | KBASE_IOCTL_SOFT_EVENT_UPDATE | jm |
| 29 | _IOW | 16 | KBASE_IOCTL_STICKY_RESOURCE_MAP | common |
| 30 | _IOW | 16 | KBASE_IOCTL_STICKY_RESOURCE_UNMAP | common |
| 31 | _IOWR | 16 | KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET | common |
| 38 | _IOW | 8 | KBASE_IOCTL_MEM_EXEC_INIT | common |
| 50 | _IOWR | 32 | KBASE_IOCTL_GET_CPU_GPU_TIMEINFO | common |
| 51 | _IOWR | 8 | KBASE_IOCTL_KINSTR_JM_FD | jm |
| 52 | _IOWR | 4 | KBASE_IOCTL_VERSION_CHECK_RESERVED | jm |
| 54 | _IOWR | 1 | KBASE_IOCTL_CONTEXT_PRIORITY_CHECK | common |
| 55 | _IOW | 1 | KBASE_IOCTL_SET_LIMITED_CORE_COUNT | common |
| 56 | _IOWR | 16 | KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO | common |
| 57 | _IOWR | 16 | KBASE_IOCTL_KINSTR_PRFCNT_SETUP | common |

Present in the 11.43 header but **not** dispatched on this device — a MediaTek
kernel build-config difference, not a UAPI version difference: `POST_TERM` (4),
`HWCNT_READER_SETUP` (8, superseded by KINSTR_PRFCNT), `TLSTREAM_FLUSH` (19),
`HWCNT_SET` (32), `CINSTR_GWT_START/STOP/DUMP` (33/34/35 — need
`CONFIG_MALI_CINSTR_GWT`).

## Main takeaway

33/33 match ARM's GPL header by **name and direction** (`_IOW`/`_IOR`/`_IOWR`).
Zero MediaTek-custom ioctls. Zero unknown numbers. So a `kbase_kmod.c` backend
for `pan_kmod` can be written against ARM's public headers as-is — no struct
layout reverse-engineering needed.

## Version negotiation (from the nr 0 handler disassembly)

At `kbase_ioctl+0x1a4`:
```
ldrh w8, [sp,#40]      ; user major
cmp  w8, #0xb          ; must be == 11, otherwise -> error path 0x1d1ec
ldrh w8, [sp,#42]      ; user minor
mov  w9, #0x2e         ; 46 = the driver's own minor
cmp  w8, #0x2e
csel w8, w8, w9, cc    ; minor = min(user_minor, 46)
strh w8, [sp,#42]      ; written back to userspace
```
So: major must be **exactly** 11 (not `>=`); minor is clamped down to 46 and
returned to userspace. Pass a deliberately high `minor` and read it back to
discover what the driver supports.

## Notes

- The module is `not stripped` but has neither `.BTF` nor `.debug_info` — only
  `.symtab` (3118 symbols). So there is no struct layout available from DWARF;
  the ioctl names above come from matching ARM's headers, not from debug info.
- Embedded ARM/MTK source path:
  `vendor/mediatek/kernel_modules/gpu/gpu_mali/mali_avalon/a16w_jm/drivers/gpu/arm/midgard/`
  (70 unique `.c` files).
- Only 9 `kbase_api_*` functions are not inlined (`mem_alloc`, `mem_alias`,
  `mem_import`, `mem_jit_init`, `mem_profile_add`, `sticky_resource_map`,
  `sticky_resource_unmap`, `get_ddk_version`, `get_cpu_gpu_timeinfo`) — all nine
  of their nr values appear in the table above (consistent cross-check).
- The ExtremeXT mirror is at 11.43, this device is 11.46 — 3 minors apart. kbase
  UAPI changes are additive, but this has **not** been verified per-struct
  against 11.46. If some ioctl fails with `-EINVAL` in use, suspect a struct
  that grew in 11.44–11.46.
