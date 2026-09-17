# Inventory — what was examined, what was published, what was left out

This repo is a curated extract of a much larger local workspace. This page
records what was inventoried so a reader can tell the difference between "does
not exist" and "exists but was not published".

Inventory date: **2026-09-17**.

## Source locations inventoried

| Local path | Role | Result |
|---|---|---|
| `~/panvk-g57/mesa` | **active driver work tree** — Mesa + v9/JM changes | 23 modified files, +1757/-85 lines → extracted to `patches/` |
| `~/panvk-g57/mesa/build` | meson build dir | **excluded** — build output |
| `~/panvk-g57/raw-jm` | raw `/dev/mali0` ioctl harnesses | C sources → `tests/raw-jm/`; compiled binaries excluded |
| `~/mesa` | test-harness working dir (also a Mesa checkout) | harnesses, shaders, tools, framebuffer output → `tests/`, `tools/`, `evidence/` |
| `~` (Termux home) | run logs, pandecode dumps, build logs, patch scripts | → `evidence/`, `tools/` |
| `~/panvk-mali-g57-master` | historical snapshot | **READ ONLY — not modified, not copied.** Newest mtime `2026-09-15 04:35`, predating this work |
| `~/panvk-g99-analysis` | vendor kbase headers + earlier gpuprops probe | already represented by `docs/kbase-uapi-r54p1.md` and `results/` |

Upstream base commit of the driver tree: `6598829019c0746aa8e473b4ae1c980cbfa6ea4b`.

**No local original was deleted or moved.** Everything here is a copy.

## Published

| Area | Count | Contents |
|---|---|---|
| `docs/` | 17 | status, per-area progress, root cause, limitations, reproduction, corrections |
| `tests/compute/` | 4 | dispatch, SPD readback, indirect (unimplemented) |
| `tests/graphics/` | 5 | triangle draw v1/v2/mex, begin/end rendering, fullscreen baseline |
| `tests/raw-jm/` | 14 | ioctl probes, `WRITE_VALUE` variants, atom-size checks |
| `tests/shaders/` | 12 | GLSL + prebuilt SPIR-V |
| `evidence/logs/` | 10 | the FAU A/B series + the partial-triangle run |
| `evidence/descriptors/` | 12 | pandecode dumps + raw descriptor hex |
| `evidence/framebuffer/` | 3 | `panvk_triangle.ppm` (real output), PNG upscale, pixel log |
| `evidence/schema/` | 1 | resource-table / genxml audit |
| `evidence/builds/` | 10 | build provenance for each A/B side |
| `evidence/historical/` | 7 | pre-port build failures + early triangle attempts |
| `patches/` | 13 | 0001–0003 enablement, 0004–0005 WIP, 0010–0015 current, 0020 FAU fix, 9001 third-party |
| `tools/` | 10 | bring-up scripts (not a supported build path) |
| `results/` | 2 | gpuprops dump, earlier job-status log |

## Deliberately excluded

| Excluded | Why |
|---|---|
| `~/panvk-g57/mesa/build/**` | build output; `libvulkan_panfrost.so` alone is ~117 MB |
| `~/mex_libvulkan_panfrost.so` (~125 MB) | third-party reference binary, not ours to redistribute |
| Compiled test binaries (`compute_test`, `triangle_draw_test*`, `kbase_*_test`) | rebuildable from the committed `.c`; see [reproduction.md](reproduction.md) |
| `*.zip` archives in `~/mesa` (`files.zip`, `panvk-stress-test.zip`, …) | redundant with the extracted sources |
| `~/realcmd.txt` (2.3 MB), `~/vulkaninfo_dump.txt` (54 KB) | bulk traces with no claim resting on them |
| Shell history, dotfiles, `.ssh`, `.aws`, `.env*`, any credential store | never in scope |
| Mesa's own upstream tree contents | this repo carries patches, not a Mesa fork |
| Unrelated projects in `~` | out of scope |

## Notes on two published items

**`evidence/historical/build_v9_full.HISTORICAL.log`** (166 KB) — the pre-port
build attempt. Contains **100 unique `error:` messages** and stops at
`[842/1029]`. Kept because [why-v9-is-a-port.md](why-v9-is-a-port.md) rests on
it. The companion `build_v9_check.HISTORICAL.log` has 16 unique errors.
The "69 errors" figure quoted in README §6 comes from an earlier build attempt
whose log is not in this repo — treat the two logs here as what they measurably
contain, not as the source of that number.

**`patches/9001-termux-android-detection-fixes...patch`** — third-party, from
[LukeValen/panvk-mali-g52](https://github.com/LukeValen/panvk-mali-g52).
Included because README §6's build path depends on it. Not our work; credited in
README.

## Integrity

SHA-256 for every file under `evidence/` and `results/`:
[`evidence/MANIFEST.md`](../evidence/MANIFEST.md).
