# Hardware and runtime

**Status: VERIFIED** — read from the live device.

## Device

| Property | Value | Source |
|---|---|---|
| Phone | Infinix X6853 (Note 40 4G) | `getprop ro.product.model` |
| SoC platform | `mt6789` (MediaTek Helio G99) | `getprop ro.board.platform` |
| Android release | 16 | `getprop ro.build.version.release` |
| GPU | Mali-G57 MC2 | product ID + core count below |
| Architecture | Valhall Gen 1 — Mesa arch bucket **v9** | GPU ID decode |
| Frontend | **JM** (Job Manager), not CSF | see below |
| Kernel driver | ARM `kbase` **r54p1** | vendor module |
| kbase UAPI | **11.46** | `KBASE_IOCTL_VERSION_CHECK` |
| Device node | `/dev/mali0`, char 10:97, mode `crw-rw-rw-` | `ls -l /dev/mali0` |

## GPU properties

From `KBASE_IOCTL_GET_GPUPROPS` — full dump in
[`results/gpuprops-g57-r54p1.txt`](../results/gpuprops-g57-r54p1.txt),
harness [`tests/test_kbase3.c`](../tests/raw-jm/test_kbase3.c).

```
PRODUCT_ID            0x9093  (37011)
RAW_GPU_ID            0x90930010
VERSION_STATUS        0
RAW_SHADER_PRESENT    0x5
RAW_L2_PRESENT        0x1
L2_NUM_L2_SLICES      1
L2_LOG2_LINE_SIZE     6      -> 64 B cache line
L2_LOG2_CACHE_SIZE    18     -> 256 KB
RAW_L2_FEATURES       0x8120206
```

`GPU_ID 0x90930010` → product `0x9093` → arch **v9**.

### SHADER_PRESENT is a sparse mask — use popcount

`RAW_SHADER_PRESENT = 0x5` is `0b101`: bits **0 and 2**, not bits 0 and 1. That
is **2 shader cores** (hence "MC2"). Computing `mask + 1` or
`log2(mask) + 1` gives 3 and is wrong. Use `popcount`.

### GPU_FREQ_KHZ_MAX is not a real value

It is a hardcoded kernel default on this part. Do not report it as a clock.

## Why the frontend matters

Valhall v9 uses the **Job Manager** submission model. v10 and later use **CSF**
(Command Stream Frontend). These are different submission paths with different
kernel ABIs, so almost all public PanVK work — which targets G610/G615/G710/G720
(v10+) — does not carry over.

In this tree the arch is routed to JM explicitly:

```
src/panfrost/vulkan/meson.build
  jm_archs  = [6, 7, 9]        # v9 uses JM submission, not CSF
  csf_archs = [10, 12, 13, 14]
```

Note the consequence: v9 is in `valhall_archs` (Valhall descriptors, shared with
v10) **and** in `jm_archs` (JM submission, shared with Bifrost v6/v7). It is the
only arch that straddles both, which is why neither the existing Bifrost `jm/`
code nor the existing Valhall `csf/` code works unmodified.

## kbase ioctl surface

33 ioctls, all 33 matching ARM's public GPL header by name **and** direction,
zero MediaTek-custom additions — so a `kbase_kmod.c` backend can be written
against ARM's public headers with no struct reverse-engineering. Full map:
[kbase-uapi-r54p1.md](kbase-uapi-r54p1.md).

`GET_GPUPROPS` works with **no context** — the state
`vkEnumeratePhysicalDevices` actually runs in. Writeup:
[gpuprops-without-context.md](gpuprops-without-context.md).

## EXEC_VA zone sizing — VERIFIED constraint

`KBASE_IOCTL_MEM_EXEC_INIT`'s `va_pages` is the size of the **entire** EXEC_VA
zone for all future executable allocations, not a per-call amount.

* `0x100000` — rejected `EPERM` on this device
* `4` (16 KB) — `EXEC_INIT` succeeds, but the first real shader-binary
  allocation then fails `ENOMEM`
* `1024` (4 MB) — works

Patch: [`patches/0003-kbase-exec-va-pages.patch`](../patches/0003-kbase-exec-va-pages.patch).
The practical ceiling for larger shaders is **untested**.

## Build host

Native Termux build on the device itself (aarch64), not cross-compiled.
Mesa work tree `~/panvk-g57/mesa`, build dir `~/panvk-g57/mesa/build`,
base upstream commit `6598829019c0746aa8e473b4ae1c980cbfa6ea4b`.
The proot shell reports `6.17.0-PRoot-Distro`; the real Android kernel is
`6.12.38-android16`.
