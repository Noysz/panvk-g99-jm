# Valhall-JM (v9) draw port — working log

Running log of the actual port work: what was built, what broke, which unit of
work owns each breakage, and what has *not* been verified.

The port itself lives in a local Mesa checkout, not in this repo. This document
records state and evidence so the work is resumable.

- Mesa checkout HEAD: `23373e9fce2af674bd61a2501548be2ed035880b` (Mesa 26.3.0-devel)
- Build logs and pre-edit file copies are kept in a local checkpoint directory,
  not committed here.

> Why a port is needed at all — rather than a build-matrix entry — is in
> [`why-v9-is-a-port.md`](why-v9-is-a-port.md). This file is the log of doing it.

---

## 1. Baseline build (2026-09-05)

Purpose: reproduce the current failure with the correct toolchain, from a clean
out-of-tree build directory, without changing any source. Establishes what the
existing local v9 work does and does not achieve.

**Toolchain** — Termux clang 21.1.8, target `aarch64-unknown-linux-android24`,
Meson 1.12.0, Ninja 1.13.2, `llvm-ar`, Termux `pkg-config`, Termux-first `PATH`
(see the toolchain trap in README §6), `--wrap-mode=nodownload`.

Original options retained: `-Dvulkan-drivers=panfrost -Dgallium-drivers=
-Dplatforms=x11 -Dbuildtype=release -Dandroid-libbacktrace=disabled
-Dc_link_args=-landroid-shmem -Dcpp_link_args=-landroid-shmem`.

**Target**

```
ninja -C <fresh-build-dir> -j1 -k0 src/panfrost/vulkan/libpanvk_v9.a
```

**Result — exit 1.**

| | |
|---|---|
| v9 objects produced | **28** |
| v9 objects required | 29 |
| only missing object | `libpanvk_v9.a.p/jm_panvk_vX_cmd_draw.c.o` |
| `libpanvk_v9.a` | not produced |
| new `libvulkan_panfrost.so` | not produced (link never attempted) |

So every v9 translation unit compiles **except** the JM draw path. Notably
`panvk_vX_cmd_desc_state.c.o`, `jm_panvk_vX_cmd_dispatch.c.o` and
`jm_panvk_vX_cmd_precomp.c.o` are among the 28 — meaning the Valhall
resource-table helpers (`cmd_prepare_shader_res_table`, `cmd_fill_dyn_bufs`)
and the v9 `COMPUTE_JOB/PAYLOAD` compute path already build for v9.

### ⚠️ Do not read the error count as progress

The run ended with:

```
fatal error: too many errors emitted, stopping now [-ferror-limit=]
20 errors generated.
```

`20` is clang's cap, not a count of remaining problems. An earlier
configuration (README §4) reported `69 errors` over `19 of 24` objects. These
numbers are **not comparable** — different file list in `meson.build`, and one
run was truncated by the error limit. Neither number measures how much of the
port is done.

### ⚠️ What compiling proves

Only that the C is well-formed for those translation units. It does **not**
demonstrate: Vulkan enumeration, device discovery, job submission,
synchronisation, CPU/GPU coherency, rendering, or Winlator compatibility. No
GPU execution of any kind has been performed against this build.

---

## 2. Blocker inventory (from the baseline run)

All 20 reported diagnostics are in `src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c`.
Line numbers are pre-edit baseline lines.

| Line | Diagnostic | Root cause | Owning unit |
|---|---|---|---|
| 48 | `field has incomplete type 'struct mali_invocation_packed'` | Bifrost `INVOCATION` embedded in `struct panvk_draw_data` | Unit 3 |
| 233, 391, 392 | `no member named 'rsd'` | `gfx.fs.rsd` is gated `#if PAN_ARCH < 9` | Unit 2 |
| 266, 271 | `MALI_RENDERER_STATE_LENGTH` / `_ALIGN` undeclared | v9 has no `RENDERER_STATE` descriptor | Unit 2 |
| 287 | `struct MALI_RENDERER_STATE` incomplete, `MALI_RENDERER_STATE_pack` / `_header` undeclared | same | Unit 2 |
| 291 | `pan_shader_prepare_rsd` undeclared | RSD helper does not exist at v9 | Unit 2 |
| 402 | `no member named 'layer_id' in 'struct panvk_draw_info'` | `layer_id` gated `#if PAN_ARCH < 9` | Unit 4 |
| 417 | `no member named 'link'` | `gfx.link` (`panvk_shader_link`) gated `< 9` | Unit 5 |
| 418 | `MALI_ATTRIBUTE_BUFFER_LENGTH` / `_ALIGN` undeclared | v9 has no `ATTRIBUTE_BUFFER` descriptor | Unit 5 |
| 432, 439 | `no member named 'indirect_varying_bufs_infos'` | gated `< 9` | Unit 6 |
| 434, 443 | `struct libpan_draw_helper_varying_buf_info` incomplete | libpan draw-helper structs are Bifrost-shaped | Unit 6 |

### The list is a floor, not a complete inventory

Every reported diagnostic sits at line **≤ 443**. Clang hit its error limit
there and stopped, so it never reached:

- the Bifrost vertex attribute/attribute-buffer emitters (~line 500-714),
- `panvk_emit_vertex_dcd` / `panvk_emit_tiler_dcd` and the Bifrost `DRAW`
  sections (~line 805-1000),
- the `COMPUTE_JOB` / `TILER_JOB` / `INDEXED_VERTEX_JOB` payloads,
- the descriptor-table calls and FS copy-descriptor job in `prepare_draw`
  (~line 1224+),
- the indirect-draw helper argument structs (~line 1660+).

Those are known-broken for v9 by inspection, but they were never *reported*.
Any future comparison must re-run with `-ferror-limit=0` to get a real
inventory.

---

## 3. Why the descriptor model has to change (not just the guards)

Bifrost and Valhall do not describe a draw the same way, so widening
`#if PAN_ARCH < 9` guards cannot work. Verified against
`src/panfrost/genxml/v9.xml`:

| Bifrost (v6/v7) | v9 (Valhall-JM) |
|---|---|
| `INVOCATION` section | **absent** — folded into `Compute Payload` |
| `RENDERER_STATE` / RSD | **absent** — replaced by `Shader Program` (SPD) + `Shader Environment` |
| `ATTRIBUTE_BUFFER` | **absent** — vertex buffers are `BUFFER` descriptors inside the driver set |
| UBO / texture / sampler / image tables | single `Resource` table; entry 0 is the driver set |
| descriptor-copy pilot job | none — sets are referenced in place |
| `CONSTANT` pixel format (used to zero-fill unbound attributes) | **absent** from v9.xml |

Also confirmed present at v9: `Shader Program`, `Shader Environment`,
`Resource`, `Compute Payload`, `Compute Job`, `Attribute`, `Null Descriptor`,
and the `Attribute Type` / `Attribute Frequency` enums.

Reference implementations used (all in-tree, no reverse engineering):

- `src/gallium/drivers/panfrost/pan_jm.c` — the only in-tree **Valhall + JM**
  submission path. `jm_emit_shader_env()`, `jm_launch_grid()`,
  `jm_emit_malloc_vertex_job()`. Shows v9 IDVS uses `MALLOC_VERTEX_JOB`, not
  Bifrost's `INDEXED_VERTEX_JOB`.
- `src/panfrost/vulkan/csf/panvk_vX_cmd_draw.c` — PanVK's Valhall descriptor
  construction. Reusable for descriptor layout; its submission model (command
  stream) is **not** applicable to JM.
- `src/panfrost/vulkan/jm/panvk_vX_cmd_dispatch.c` — already-ported v9 compute
  path in this very backend; proves the `driver_set → res_table →
  COMPUTE_JOB/PAYLOAD` pattern works under JM.
- `src/panfrost/vulkan/panvk_vX_nir_lower_descriptors.c` — **dictates** the
  driver-set layout for `PAN_ARCH >= 9` (see Unit 1).

---

## 4. Port plan — one unit at a time

Each unit is meant to be independently reviewable and revertible. Bifrost
(v6/v7) behaviour must be preserved exactly; the v9 path is added beside it,
never on top of it.

| Unit | Scope | Status |
|---|---|---|
| 1 | Vertex-stage descriptors: driver set + resource table | **edited, not yet compiled** |
| 2 | Fragment state: `RENDERER_STATE`/RSD → SPD + `Shader Environment` | not started |
| 3 | Job payloads: `INVOCATION`/`PARAMETERS`/`DRAW` → `COMPUTE_JOB/PAYLOAD`; `struct panvk_draw_data` cleanup | not started |
| 4 | Layer handling (`layer_id` → tiler context) | not started |
| 5 | Varyings: `panvk_shader_link` + `ATTRIBUTE_BUFFER` → v9 varying descriptors | not started |
| 6 | Indirect draws + libpan draw-helper structs | not started |
| 7 | IDVS: `INDEXED_VERTEX_JOB` → `MALLOC_VERTEX_JOB` | not started |

Explicitly **out of scope** for all of the above: the missing `kbase` backend in
`pan_kmod`. That is a separate, independent blocker (README §1b, §3) — `pan_kmod`
registers only `panfrost_kmod` and `panthor_kmod`, so enumeration never touches
`/dev/mali0`. Fixing the draw port does not make the driver enumerate, and
fixing enumeration does not make it draw.

---

## 5. Unit 1 — vertex-stage driver set + resource table

**Status: source edited. Not compiled. Not run. No claim of correctness.**

File touched: `src/panfrost/vulkan/jm/panvk_vX_cmd_draw.c`
Diff: **193 insertions, 0 deletions** — no pre-existing line was modified or
removed. The Bifrost code is byte-identical, only wrapped in `#if PAN_ARCH < 9`.
A pre-edit copy of the file is kept outside the build tree.

### Added, for `PAN_ARCH >= 9` only

- `panvk_draw_emit_vs_attrib()` — packs a v9 `ATTRIBUTE` descriptor pointing at
  a `BUFFER` descriptor in the driver set (`cfg.table = 0`).
- `panvk_draw_prepare_vs_driver_set()` — builds the driver set.
- `panvk_draw_prepare_vs_desc()` — driver set, then
  `cmd_prepare_shader_res_table(..., repeat_count = 1)`.

### Gated to `PAN_ARCH < 9`

`panvk_draw_emit_attrib_buf()`, `panvk_draw_emit_attrib()`,
`panvk_draw_prepare_vs_attribs()`, `panvk_draw_prepare_attributes()`,
`panvk_draw_prepare_vs_copy_desc_job()`, and in `prepare_draw()` the
`cmd_prepare_shader_desc_tables` / `cmd_prepare_dyn_ssbos` calls for the vertex
stage.

### The driver-set layout is not a free choice

Worth writing down, because getting it wrong is silent corruption rather than a
compile error. `panvk_vX_nir_lower_descriptors.c` hardcodes the layout for
`PAN_ARCH >= 9` in `create_copy_table()`:

```
vertex stage:  dummy_sampler_idx = 16          (== MAX_VS_ATTRIBS)
fragment:      dummy_sampler_idx = num_varying_attr_descs
compute:       dummy_sampler_idx = 0
               dyn_bufs_start    = dummy_sampler_idx + 1
```

and dynamic-buffer handles are emitted as
`pan_res_handle(0, dyn_bufs_start + idx)` — resource table **0**, i.e. the
driver set. So for the vertex stage the driver set must be:

```
[0 .. 15]                       ATTRIBUTE   (MAX_VS_ATTRIBS = 16)
[16]                            SAMPLER     (dummy)
[17 .. 17+dyn_bufs.count-1]     BUFFER      (dynamic buffers)
[17+dyn_bufs.count .. ]         BUFFER      (vertex buffers) / NULL_DESCRIPTOR
```

This is shader-side ABI in shared per-arch code — not a CSF convention. The
same layout appears in `csf/panvk_vX_cmd_draw.c`, which is why that file is a
valid reference here.

### Two deliberate JM-vs-CSF differences

1. **Base instance is folded into the descriptor offset.** CSF keeps the base
   instance in a command-stream register and patches the descriptor when it
   changes (`attribs_changing_on_base_instance`, `desc_repeat_count`). JM has no
   such register and cannot patch a recorded job, so the offset is baked in from
   `draw->info.instance.base`, exactly as the Bifrost path already does. The
   driver set is therefore rebuilt every draw and `repeat_count` is always 1.
2. **Unbound attributes use the OOB-table trick.** Bifrost fills them with
   `MALI_PACK_FMT(CONSTANT, 0000, L)`; v9.xml has no `CONSTANT` format, so that
   is unavailable. Following CSF, the descriptor is pointed at an invalid
   resource table (`cfg.table = 17`) to get the zero default.
   **Caveat:** this relies on out-of-bounds resource reads returning zero. That
   behaviour is inherited from the CSF (v10+) path and has **not** been
   confirmed on v9 hardware.

### Verification status — and why the error count will not improve

Nothing has been compiled since the edit:

- Bifrost regression check (rebuild v6 + v7 `jm_panvk_vX_cmd_draw.c.o`): **not run**.
- v9 diagnostic check: **not run**.

When it is run, the v9 error output is expected to look *unchanged*, because the
first diagnostic is the `struct mali_invocation_packed` field at line 48 —
before any code this unit touched. Unit 1 removes none of the 20 reported
diagnostics.

The correct verification for this unit is therefore **not** an error count:

1. v6 and v7 objects still build → Bifrost preserved.
2. v9 compiled with `-ferror-limit=0`, then confirm **zero** diagnostics fall
   inside the newly added functions — the remaining errors must all map to
   Units 2-7 in the table above.

---

## 6. Standing non-claims

Stated explicitly so nothing here gets over-read:

- No v9 `libpanvk_v9.a` has ever been produced.
- No v9-capable `libvulkan_panfrost.so` has ever been produced.
- No PanVK build has enumerated this GPU. No Winlator test has been run against
  any v9 build, because no v9 build exists.
- No GPU job has been submitted by any of this code.
- The existing v9 compute/dispatch/precomp code compiles, and that is all that
  is currently known about it.
