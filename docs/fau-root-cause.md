# FAU count root cause — v9 Shader Environment

**Status: VERIFIED (runtime hardware A/B, reproduced 2x each side)**

## One-line statement

On Valhall v9 the **Shader Environment** descriptor carries both an FAU
*pointer* and an FAU *count*. The port programmed only the pointer and left
`fau_count` at zero. With a zero count the hardware uploads no fast-access
uniforms, so the validated 64x64 triangle workload produced a correctly cleared
but empty framebuffer. Programming `fau_count` from compiled shader metadata is
**causal** for that workload rendering.

## Why a zero count is not "unset"

`fau_count` is the number of 8-byte FAU words the shader stage receives. Zero is
a legal encoding meaning "upload nothing". Nothing faults, because the job chain
is structurally valid:

* MALLOC_VERTEX job (type 11) reports `exception_status=0x01 (DONE)`
* FRAGMENT job (type 9) reports `exception_status=0x01 (DONE)`

Both before and after the fix. The GPU ran the chain in both cases. The
difference is only whether the shaders received their uniforms:

* the vertex shader gets no viewport / position uniforms, so no in-range
  geometry is produced;
* the fragment shader gets no blend-descriptor pointer.

The result presents as "clear works, draw does not", which is why this was
originally misfiled as a draw/tiler problem.

## The fix

`src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c`, two sites:

```c
/* vertex — position shader in the MALLOC_VERTEX job */
cfg.fau       = cmdbuf->state.gfx.vs.push_uniforms;
cfg.fau_count = vs->fau.total_count;

/* fragment — Shader Environment inside the DCD */
cfg.shader.fau       = cmdbuf->state.gfx.fs.push_uniforms;
cfg.shader.fau_count =
   panvk_shader_only_variant(get_fs(cmdbuf))->fau.total_count;
```

Isolated patch: [`patches/0020-panvk-v9-fau-count-from-shader-metadata.patch`](../patches/0020-panvk-v9-fau-count-from-shader-metadata.patch)

## Counts come from shader metadata — never hardcode

**Do not hardcode 4 and 3.** Those are the values *this* workload happens to
produce. Captured live from the running driver
([`evidence/descriptors/PANVK_G57_raw_capture_20260917-012821.log`](../evidence/descriptors/PANVK_G57_raw_capture_20260917-012821.log)):

```
[PANVK_DEBUG_VSFAU] gpu=0x7ca09713d0 count=4 bytes=32
[PANVK_DEBUG_FSFAU] gpu=0x7ca09713f0 count=3 bytes=24
```

`count=4` → 32 bytes and `count=3` → 24 bytes, i.e. 8 bytes per FAU word. Both
numbers are properties of the compiled shader (`fau.total_count`), not of the
architecture. Any other shader gives other counts, so the only correct source is
the shader metadata.

## A/B evidence

Identical build, identical test binary, identical workload. The only delta
between A and B is the two lines above. Sources kept verbatim next to the
driver as `panvk_vX_cmd_draw.c.ab_fau_off` / `.ab_fau_on`; the diff between them
is the patch body of `0020`.

| Run | `fau_count` | non-black pixels | verdict | log |
|---|---|---|---|---|
| A | absent | **0 / 4096** | CLEAR-ONLY | [`ab_A_run.log`](../evidence/logs/ab_A_run.log) |
| A2 (repeat) | absent | **0 / 4096** | CLEAR-ONLY | [`ab_A2_run.log`](../evidence/logs/ab_A2_run.log) |
| B | present | **4096 / 4096** | renders | [`ab_B_run.log`](../evidence/logs/ab_B_run.log) |
| B final (repeat) | present | **4096 / 4096** | renders | [`b_final_run.log`](../evidence/logs/b_final_run.log) |

The A/B pair used a fullscreen-covering triangle, hence 4096/4096. That
saturation is *why* a second, deliberately partial workload was then run — see
[graphics-progress.md](graphics-progress.md). A fullscreen result alone cannot
distinguish real rasterization from a full-surface blit or a mis-scaled clear.

Both A/B sides also carry pandecode descriptor dumps for byte-level comparison:
[`ab_A_pandecode.ctx-0.txt`](../evidence/descriptors/ab_A_pandecode.ctx-0.txt),
[`ab_B_pandecode.ctx-0.txt`](../evidence/descriptors/ab_B_pandecode.ctx-0.txt).

## Single-stage isolation — the vertex count is the gate

**Status: VERIFIED at descriptor level.**

Two further runs each programmed only one stage's count. The framebuffer results
disagree, and the pandecode dumps say why.

First, stage attribution of the two FAU blocks:

| FAU block | stage | count | bytes |
|---|---|---|---|
| `...3d0` | vertex | 4 | 32 |
| `...3f0` | fragment | 3 | 24 |

Evidence for each side, stated at its real strength:

* **Fragment (`...3f0`) — structurally established.** The `Draw:` descriptor
  contains `Shader: FAU: 0x...3f0` alongside `Blend`, `Depth/stencil` and the
  render-target mask, so that Shader Environment is the fragment one by position
  in the descriptor, independent of any naming. Pandecode independently decodes
  **3** words at `...3f0`, matching `FAU count: 3`.
* **Vertex (`...3d0`) — established by naming plus corroboration, not by an
  independent structural label.** The driver print
  `[PANVK_DEBUG_VSFAU] gpu=0x7ca09713d0 count=4 bytes=32` names it, pandecode
  decodes **4** words at that address (consistent with count=4), the contents are
  viewport/position constants rather than a blend pointer, and it is the only
  remaining Shader Environment once the fragment one is accounted for. That is
  strong, but it is corroboration and elimination — treat "the `...3d0` block is
  the vertex position Shader Environment" as **well-supported rather than
  independently proven**.

The `Draw:` descriptor's `Shader: FAU: 0x...3f0` therefore identifies
`Draw.Shader.FAU count` as the **fragment** Shader Environment. The vertex count
lives in the MALLOC_VERTEX job's position Shader Environment.

Now the isolation runs:

| Run | vertex count | fragment count | non-black | log |
|---|---|---|---|---|
| A | 0 | 0 | **0 / 4096** | [`ab_A_run.log`](../evidence/logs/ab_A_run.log) |
| fragment only | 0 | set | **0 / 4096** | [`fs_only_run.log`](../evidence/logs/fs_only_run.log) |
| vertex only | set | **0** | **4096 / 4096** | [`vs_only_run.log`](../evidence/logs/vs_only_run.log) |
| B (both) | 4 | 3 | **4096 / 4096** | [`ab_B_run.log`](../evidence/logs/ab_B_run.log) |

The `vertex only` row is read directly off
[`vs_only_pandecode.ctx-0.txt`](../evidence/descriptors/vs_only_pandecode.ctx-0.txt):
`FAU count: 0` in the Draw/fragment Shader Environment, yet the vertex block
`FAU @7bc98ae3d0` is present and decoded. In run A that vertex block is absent
entirely and `FAU count: 0`.

**Conclusion: the vertex position Shader Environment FAU count is what gates
rasterization for this workload.** The vertex shader's FAU carries the viewport
scale/offset and position constants (run B decodes `42000000` = 32.0f,
`3F800000` = 1.0f, `BF800000` = -1.0f — consistent with a 64x64 viewport), so a
zero count leaves the VS without the data it needs to emit in-range geometry and
nothing survives to rasterization.

The fragment count was **not** required for pixels to appear here only because
this fragment shader writes a constant colour. That is a property of the test
shader, not a general result — which is exactly why the shipped fix programs
**both** stages from metadata rather than only the one that happened to be
load-bearing in this test.

## Scope of the claim — read this

VERIFIED: the missing v9 Shader Environment FAU count is causal for **this
validated graphics workload**, and the vertex-stage count is the gating one.

NOT claimed: that this is the only remaining v9 graphics bug, that it fixes
arbitrary applications, or that it affects CSF (v10+) paths. It is one causal
defect on one validated workload. See [known-limitations.md](known-limitations.md).
