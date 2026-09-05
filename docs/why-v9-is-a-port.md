# Why PanVK v9 is a port, not a missing build-list entry

**TL;DR:** adding `9` to PanVK's meson build matrix is necessary but nowhere near
sufficient. In Mesa as of this test, PanVK's `jm/` command-buffer backend is
**Bifrost-only**, not JM-generic — it is written entirely against `PAN_ARCH < 9`.
v9 (Valhall generation, JM frontend) falls between the two taxonomies and has no
implementation at all. `jm_archs = [6, 7]` was accurate, not an oversight.

Measured 2026-09-05 against a native on-device Termux build of Mesa 26.3.0-devel
(clang 21.1.8 / NDK r29, `aarch64-unknown-linux-android24`).

## The patch that isn't enough

[`patches/0001-panvk-add-v9-to-build-matrix.patch`](../patches/0001-panvk-add-v9-to-build-matrix.patch)
— two lines in `src/panfrost/vulkan/meson.build`:

```diff
-jm_archs = [6, 7]
+jm_archs = [6, 7, 9]

-foreach arch : [6, 7, 10, 11, 12, 13, 14]
+foreach arch : [6, 7, 9, 10, 11, 12, 13, 14]
```

Meson accepts it cleanly (`panvk_v9` then appears 121× in `build.ninja`, and
`idep_libpan_per_arch['9']` already exists because
`src/panfrost/libpan/meson.build:14` has always listed `'9'`).

Then it fails to compile:

```
$ ninja -C build -k 0 src/panfrost/vulkan/libpanvk_v9.a
...
19 of 24 objects compile, 5 fail, 69 errors
```

Failing objects:

```
panvk_vX_cmd_frame_shaders.c      3 errors
jm/panvk_vX_cmd_buffer.c          7 errors
jm/panvk_vX_cmd_dispatch.c       19 errors
jm/panvk_vX_cmd_draw.c           19 errors
jm/panvk_vX_cmd_precomp.c        19 errors
```

Two completely unrelated causes.

## Cause 1 — two latent guard bugs in a shared file (small)

`panvk_vX_cmd_frame_shaders.c` is a *common* per-arch file, not a JM one, and it
still breaks at v9:

```
:754:53: error: no member named 'tsd' in 'struct panvk_cmd_graphics_state'
:758:11: error: no member named 'flags_2' in 'struct MALI_DRAW'
:759:11: error: no member named 'flags_2' in 'struct MALI_DRAW'
```

- `state.gfx.tsd` is declared under `#if PAN_ARCH >= 10`
  (`panvk_cmd_draw.h:234`), but the use site sits in the `#else` branch of
  `#if PAN_ARCH >= 12` — so its effective range is 9, 10, 11. Off by one.
- `MALI_DRAW.flags_2` is used unconditionally, but genxml only grows a `Flags 2`
  field in the `Draw` struct from `v10.xml` onward. `v9.xml` has:
  `Draw | Flags 0 | Flags 1 | Vertex array | Minimum Z | Maximum Z | Depth/stencil | Blend count | Blend | Occlusion | Shader`
  — `v10.xml` inserts `Flags 2` after `Vertex array`.

Both are real upstream Mesa bugs, but **latent** — invisible today because
nothing builds v9. Two guard adjustments would fix them. Fixing them alone does
not get v9 built, because of cause 2.

## Cause 2 — `jm/` is Bifrost-only (a real port)

The other 66 errors are all one thing: every structure member and helper the
`jm/` sources reach for is gated `#if PAN_ARCH < 9` in the shared headers.

**Struct members** (`src/panfrost/vulkan/panvk_cmd_draw.h`):

| gated member | line | used by |
|:---|---:|:---|
| `fb.bo_count`, `fb.bos`, `fb.needs_load`, `fb.needs_store` | 97-102 | `jm/panvk_vX_cmd_buffer.c` |
| `link` | 164-166 | `jm/panvk_vX_cmd_draw.c` |
| `fs.rsd` | 175-177 | `jm/panvk_vX_cmd_draw.c` |
| `vs.attribs`, `attrib_bufs`, `indirect_attribs_infos`, `indirect_attrib_bufs_infos`, `indirect_varying_bufs_infos` | 189-194 | `jm/panvk_vX_cmd_draw.c` |

**Helper functions:**

- `panvk_cmd_desc_state.h:69` — `#if PAN_ARCH < 9` wraps
  `cmd_prepare_dyn_ssbos` and `cmd_prepare_shader_desc_tables`. The `#else`
  branch provides *different* Valhall functions instead:
  `cmd_fill_dyn_bufs` and `cmd_prepare_shader_res_table`.
- `panvk_meta.h:178` — `#if PAN_ARCH < 9` wraps `meta_get_copy_desc_job`, which
  is implemented only in `bifrost/panvk_vX_meta_desc_copy.c:388`.

**And the hardware descriptors themselves don't exist at v9.** This is the part
that makes it a port rather than a refactor — it isn't a missing `#define`, the
GPU genuinely uses a different descriptor model:

| genxml struct | v6/v7 | v9+ |
|:---|:---:|:---:|
| `Renderer State` (RSD) | ✅ | ❌ — replaced by `Shader Program` (SPD) |
| `Attribute Buffer` | ✅ | ❌ — replaced by `Resource` tables |
| `Invocation` | ✅ | ❌ |

Job layouts differ too:

```
Compute Job   v6: Header / Invocation / Parameters / Draw
              v9: Header / Payload

Tiler Job     v6: Header / Invocation / Primitive / Primitive Size / Tiler / Padding / Draw
              v9: Header / Primitive / Instance Count / Vertex Count / Tiler / Scissor / Primitive Size / Indices / Draw
```

So `jm/panvk_vX_cmd_dispatch.c:139` packing `cfg.INVOCATION` and `:144`
`cfg.PARAMETERS` cannot work at v9 — those sections aren't in the v9 Compute Job
aggregate at all.

## The good news: there is a reference implementation

Mesa already knows how to drive Valhall-JM — just not from PanVK.
`src/gallium/drivers/panfrost/meson.build:86` lists `'9'` in
`panfrost_versions`, and `pan_cmdstream.c` carries ~15 `PAN_ARCH >= 9` /
`PAN_ARCH < 9` branches. The v9 JM command-stream code exists and is exercised by
the GL driver. A PanVK `jm/` v9 backend has something concrete to port from
rather than a blank sheet.

## Note on verifying `panvk_vX` in a built `.so`

`nm -D` is the wrong tool and will report **zero for every arch**. PanVK's
per-arch static libs are built with `gnu_symbol_visibility : 'hidden'`
(`src/panfrost/vulkan/meson.build:239`), so per-arch symbols never reach the
dynamic table — only the ICD entrypoints do. Use `nm --defined-only` on an
unstripped `.so`.

Stock Mesa, no patch, native build (this is the answer to "does a from-source,
non-arch-trimmed build surface v9 automatically?" — **no**):

```
panvk_v6  : 106     panvk_v10 : 128     panvk_v13 : 128
panvk_v7  : 106     panvk_v11 : 128     panvk_v14 : 126
panvk_v9  :   0     panvk_v12 : 128
```

## Reproducing

```bash
cd mesa
git apply patches/0001-panvk-add-v9-to-build-matrix.patch
ninja -C build -k 0 src/panfrost/vulkan/libpanvk_v9.a
```

Note that with the patch applied, the ordinary `libvulkan_panfrost.so` target
also fails, because `link_whole` pulls in `libpanvk_v9.a`. Revert the patch to
get a buildable tree back.
