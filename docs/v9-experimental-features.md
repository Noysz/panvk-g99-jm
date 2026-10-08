# EXPERIMENTAL v9 features: multi-draw, robustness2, sampler min/max

> **EXPERIMENTAL.** Everything in this document turns on features that
> upstream panvk only exposes on v10 or later (CSF). On v9 they work by
> emulating them in the driver. They are tested against parts of CTS and
> nothing else. Do not treat any of them as conformant. Patches 0044-0046.

Labels and rules are the same as elsewhere in this repo: 3x plus a negative
control, and `MESA_SHADER_CACHE_DISABLE=true` for anything that acts at
shader compile time. CTS results are in [`evidence/cts/`](../evidence/cts/).

Running a robustness caselist in one deqp process got it killed with rc -9
before the first result (cause not established; free RAM was low at the
time). The robustness and sampler subsets are therefore run one case per
process with [`tests/cts/percase.sh`](../tests/cts/percase.sh).

## Why these features

The cases that came back NotSupported from the CTS groups run before
([cts-wsi-texture.md](cts-wsi-texture.md), [phase5-7.md](phase5-7.md)),
grouped by reason, and where each one is gated in source:

| reason | cases | gate | v9 hardware |
|---|---|---|---|
| `VK_KHR_draw_indirect_count` / `multiDrawIndirect` | 286 | `PAN_ARCH >= 10` | no multi-draw descriptor, but the v9 path already unrolls indirect draws |
| compute-only queue | 97 | one queue family | not attempted |
| `samplerFilterMinmax` | 60 | `PAN_ARCH >= 10` | **no** "Reduction mode" field in the v9 SAMPLER descriptor (v10.xml has one) |
| `VK_EXT_filter_cubic` | 6 | not exposed | no cubic filter in v9 or v10 |

DXVK also requires `VK_EXT_robustness2` (`nullDescriptor`,
`robustBufferAccess2`), gated at `PAN_ARCH >= 10` / `>= 11` (VERIFIED-SRC,
[phase5-7.md](phase5-7.md#5-dxvk-requirement-gap-phase-7-verified-src--reported-features)).

## 0044: multiDrawIndirect and drawIndirectCount (VERIFIED-HW in CTS)

The v9 Job Manager path emulates indirect draws: each draw gets a job plus a
helper kernel that patches its counts. The patch builds on that.

- `drawCount` / `maxDrawCount` are unrolled on the CPU, one job and helper
  pair per draw.
- `gl_DrawID` becomes a per-draw sysval. Before this it was the constant 0.
- `vkCmdDraw*IndirectCount`: the helper reads the GPU-side count and turns
  the draws at or past it into NULL jobs.
- `maxDrawIndirectCount` is 65535, the spec minimum for
  `multiDrawIndirect`, because every draw is a job.

Results:

- `draw.renderpass.indirect_draw`: **372/372 pass, 3x**. Before the patch it
  was 86 pass and 286 NotSupported
  ([before](../evidence/cts/indirect_draw_before_0044/)).
- Controls, same group:
  - `PANVK_MDI_NO_COUNT=1` (count buffer ignored): 138 fail.
  - `PANVK_MDI_NO_DRAWID=1` (`gl_DrawID` stays 0): 134 fail.
- Regression: `api.smoke` 8/8. The `draw_indirect_test.c` fingerprints match
  the committed logs.

Cost: recording time and job count grow linearly with `maxDrawCount`, even
when the GPU-side count is small.

## 0045: VK_EXT/KHR_robustness2, nullDescriptor (VERIFIED-HW in CTS; robustBufferAccess2 opt-in only)

A v9 null descriptor is type 0, which means all-zero bytes, and panvk already
writes it on v9. The patch exposes `nullDescriptor` and the extension on v9.

`dEQP-VK.robustness.robustness2.*.null_descriptor` subset (26 cases):
**16 pass, 3x**. The other 10 are NotSupported because they need
`vertexPipelineStoresAndAtomics`.

Negative controls:

- The first control left the slot unwritten. Every case still passed,
  because unwritten memory is already a valid null descriptor. That control
  is useless and was replaced.
- `PANVK_NULLDESC_POISON=1` writes 0xff bytes (an invalid type) instead:
  8 fail (storage image and texel buffers). The sampled-image and
  uniform-buffer cases still pass under it, so the control does not cover
  them.

Until `0071`, `robustBufferAccess2` was exposed only with
`PANVK_V9_RBA2=1`. Under that setting, the out-of-bounds subset gave 20
pass and 15 fail. All the failures were texel buffers: the expected
out-of-bounds value depends on the format, and the shader does not know the
format at compile time. UBO and SSBO cases passed.

## 0071: texel buffer out-of-bounds values, robustBufferAccess2 on by default (VERIFIED-HW in CTS)

Measured with `tests/compute/texel_buffer_oob_test.c` (views of 4 texels in
memory filled with a pattern, indices 0..7): v9 LEA_BUF + LD_CVT do bounds
check (out-of-range reads give 0 in R, G, B, out-of-range stores and atomics
are dropped), but the alpha is the wrong way round: 0 for formats without
alpha (`R32_UINT`, `R32G32_SFLOAT`, Vulkan wants 1) and 1.0 for
`R8G8B8A8_UNORM` (Vulkan wants 0).

- `pan_buffer_texture_emit` writes the element count into dword 5 and
  "the format has no alpha" into dword 6 of the BUFFER descriptor. The
  hardware does not read them (dword 7 already holds the conversion the
  same way).
- `pan_nir_lower_texel_buf_oob` (bifrost_nir.c) runs before the texel buffer
  accesses become LEA_BUF + LD_CVT, for `imageLoad` and `texelFetch` on
  buffers: index < count keeps the loaded value, otherwise (0, 0, 0, a) with
  a = 1 when dword 6 says no alpha. A null descriptor has count 0 and
  dword 6 = 0, so it still reads 0.
- It runs only when the pipeline asks for robustBufferAccess2 on uniform or
  storage buffers. Control `PANVK_V9_TEXEL_OOB_HW=1`.
- `robustBufferAccess2` is now on by default on v9; `PANVK_V9_NO_RBA2=1`
  hides it. DXVK 2.4+ and 3.x refuse a device without it.

Results (`evidence/cts/phase13b`):

- `texel_buffer_oob_test`: 3x PASS. `PANVK_V9_TEXEL_OOB_HW=1`: 16 bad
  out-of-range values (FAIL). `PANVK_V9_NO_RBA2=1`: not supported.
- The old 35-case subset: 35/35 (was 20 / 15 F).
- `robustness2.bind.notemplate`, 32-bit formats, `unroll.nonvolatile`, all
  texel buffer cases plus every 4th other case (2649): 926 P / 53 F /
  1670 NS. Control: 485 P / 494 F. 441 texel buffer cases fixed, 0 lost.
- The 53 left fail with the control too: out-of-range **vertex attribute
  fetch** (28: `len_32`, `len_39`, `len_252` and null descriptor) and
  null-descriptor sampled images read in a **vertex shader** (25). Both are
  open.

## 0046: sampler min/max reduction, emulated in the shader (VERIFIED-HW in CTS, cube maps BROKEN)

v9 has no reduction mode field in hardware, so the shader does the
filtering itself:

1. `panvk_vX_sampler.c` writes a tag, the mode and the
   min/mag/mipmap filters into **word 3** of the v9 SAMPLER descriptor.
   v9.xml defines no field there. Filling word 3 with 0xffffffff left the
   output of 5 texture cases byte-identical, so the hardware is taken not
   to read it (INFERENCE from that measurement). Every WEIGHTED_AVERAGE
   sampler keeps word 3 = 0.
2. [`panvk_v9_nir_lower_minmax.h`](../patches/0046-v9-EXPERIMENTAL-sampler-minmax-emulation.patch)
   wraps every float `texture`/`textureLod`/biased sample on 1D, 2D or 3D,
   array or not, in `if (word3 tag)`.
   - The emulation path computes the LOD the same way the existing
     `textureQueryLod` lowering does: sampler min/max LOD, level count,
     mipmap mode.
   - It fetches the filter footprint one texel at a time with `texelFetch`,
     inside a loop, and applies the address modes and the border colour in
     the shader.
   - It takes the component-wise min or max. With LINEAR mipmapping and a
     non-zero fraction, both levels are reduced together.
   - The else branch is the original hardware sample.

Two backend problems came up while building this, and are worked around in
the pass:

- `nir_def_rewrite_uses_after` only works within one block. Uses are
  rewritten explicitly instead.
- With the eight fetches unrolled, the scheduler issued all of them up
  front. The register pressure made the backend spill, and a phi copy kept
  an unallocated source, so `va_validate` crashed. The loop removes the
  spill (0 TLS loads/stores afterwards). The backend bug itself is **not**
  fixed.

Results:

- CTS `pipeline.monolithic.sampler`, `{1d,2d,2d_array,3d}` × 6 formats ×
  mag/min × min/max/average (132 cases): **132/132 pass, 3x**. Before this
  patch, all the min/max cases were NotSupported.
- `PANVK_V9_NO_MINMAX=1` (pass off, feature still exposed): **72 fail**.
  Every `average` case passes and the failures are min/max. 16 min/max
  cases still pass under the control, presumably because the average
  happens to equal the min or max there.
- The earlier `sampler.view_type.2d.format.r8g8b8a8_unorm` group: 73 pass /
  97 NotSupported / 0 fail, up from 55 / 115 / 0. The texture filtering and
  mipmap groups are unchanged.
- Own harness `tex_test.c`: 16/16 with the pass active.

**Known broken:** cube and cube array views are not handled, and they now
return the hardware average for a MIN/MAX sampler. CTS subset: 12 of 16
fail. Explicit gradients (`textureGrad`), depth compare and 16-bit results
also fall back to the average.

**Cost:** every float texture sample in every shader now does an extra
descriptor load and a branch, even when no MIN/MAX sampler is bound. This
has not been measured.

## Still open

- Cube and cube array for 0046.
- robustBufferAccess2 vertex attribute fetch out of range (28 CTS cases) and
  null-descriptor sampled images in a vertex shader (25), see 0071.
- `VK_EXT_filter_cubic`, with the same word-3 approach and 16 texels.
- Compute-only queue (97 cases).
- The backend spill/phi bug from 0046.
