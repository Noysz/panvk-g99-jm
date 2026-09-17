# Compute progress — v9 / JM

**Status: VERIFIED.** Native v9/JM compute dispatch with correct readback,
through real PanVK.

The detailed original writeup is
[v9-compute-dispatch-validated.md](v9-compute-dispatch-validated.md). This page
is the current summary and the delta since then.

## What works

The full chain, end to end:

```
SPIR-V  ->  compiled to native Mali-G57 ISA  ->  executable memory via kbase
        ->  COMPUTE_JOB submitted  ->  GPU executes  ->  result read back on CPU
```

Test: [`tests/compute/panvk_compute_test_g57.c`](../tests/compute/panvk_compute_test_g57.c) —
`vkCreateInstance` → `vkCreateDevice` → storage buffer → shader module
(`data[0] = 777u;`) → compute pipeline → command buffer → `vkQueueSubmit` →
`vkQueueWaitIdle` → read back.

```
buffer initialized with 0xAA, first u32=0xaaaaaaaa
...
vkCreateComputePipelines ok
vkQueueSubmit ok
vkQueueWaitIdle ok
buffer after dispatch, first u32=0x00000309 (expected 0x00000309 = 777)
```

Run 3 times, byte-for-byte identical output including the whole sequence of
intermediate `kbase MEM_ALLOC` requests — not just the final value. That rules
out a race or a one-off.

This runs through PanVK, not a raw-ioctl harness. The GPU is genuinely compiling
and executing SPIR-V-derived code.

## Why v9 compute is not Bifrost compute

v9 has a native `Compute Job` with a **flat `Compute Payload`** struct. Bifrost
splits the same work across separate `Invocation` / `Parameters` / `Draw`
structs. The v9 form is simpler, and it reuses the SPD-style `Shader Environment`
sub-struct that the fragment side also uses — which is why the FAU-count lesson
from graphics ([fau-root-cause.md](fau-root-cause.md)) is relevant here too: the
same descriptor shape carries the same count field.

Implementation: `jm/panvk_vX_cmd_dispatch.c` under `PAN_ARCH >= 9`.
Patch: [`0011-panvk-v9-jm-compute-dispatch.patch`](../patches/0011-panvk-v9-jm-compute-dispatch.patch).

## Enablement fixes that compute needed

These are prerequisites for *anything* v9, found while getting compute up:

1. **`meson.build` arch matrix** — `9` added, so `libpanvk_v9.a` compiles.
   [`0001`](../patches/0001-panvk-add-v9-to-build-matrix.patch)
2. **`panvk_physical_device.c`** — `PER_ARCH_FUNCS(9)` was missing from the
   manually-invoked macro list, so v9 entry points had no prototypes.
   [`0002`](../patches/0002-panvk-physical-device-proto-v9.patch)
3. **`panvk_macros.h`** — `case 9` added to `panvk_arch_dispatch` /
   `panvk_arch_dispatch_ret`. This is the top-level switch routing generic Vulkan
   entry points (including `vkCreateDevice`) to per-arch functions. Without it,
   v9 fell into `default: UNREACHABLE(...)`, which under `NDEBUG` compiles to
   `__builtin_unreachable()` — undefined behaviour, not a clean error. It
   manifested as execution jumping into unrelated CSF-only code.
4. **`#if PAN_ARCH < 9` / `>= 10` boundary conditions** that left v9 itself
   unhandled, e.g. `panvk_cmd_graphics_state.tsd`, `panvk_rendering_state::fb`.
   v9 is the only arch that is both Valhall *and* JM, so it falls through
   conditions written when those two things implied each other.
5. **EXEC_VA zone sizing** — see
   [hardware-runtime.md](hardware-runtime.md#exec_va-zone-sizing--verified-constraint).
   [`0003`](../patches/0003-kbase-exec-va-pages.patch)

## Descriptor readback

[`tests/compute/panvk_spd_readback_test.c`](../tests/compute/panvk_spd_readback_test.c)
and its `_fixed` variant read back the Shader Program Descriptor to confirm the
driver is writing what it thinks it is. Output:
[`evidence/descriptors/shader_dump.txt`](../evidence/descriptors/shader_dump.txt),
[`shader_dump2.txt`](../evidence/descriptors/shader_dump2.txt).

## Not covered

* **NOT IMPLEMENTED** — `vkCmdDispatchIndirect` on v9. Harness exists
  ([`indirect_dispatch_test.c`](../tests/compute/indirect_dispatch_test.c)) but
  the driver path does not.
* **UNTESTED** — workloads larger than a single storage buffer. The practical
  ceiling of the 4 MB EXEC_VA zone for bigger shaders is unknown.
* **UNTESTED** — workgroup shared memory, barriers, atomics, multiple
  descriptor sets.
* **UNTESTED** — compute queue concurrency, multi-submit.
