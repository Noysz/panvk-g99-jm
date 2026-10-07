# FourFectVK release roadmap (alpha → beta → 1.0)

Version scheme from `0.0.6` on (`alpha1`-`alpha5` came before it):

| Stage | Versions | Meaning |
|---|---|---|
| alpha | `0.0.x` | Feature bring-up on Mali-G57 MC2 (Helio G99). Features may be 🧪 EXPERIMENTAL, known gaps are listed in every release. |
| beta | `0.1.0` … `0.9.x` | Every feature DXVK 1.10.3 to 2.x asks for is there. Work moves to memory, speed and devices. |
| release candidate | `1.0.0-rc.N` | Freeze: only fixes, no features. |
| full release | `1.0.0` | Exit criteria below met. |

Rules for every release, same as for every phase: each change has a
falsifiable test, 3 runs and a negative control; full CTS regression
against the previous release; Winlator stack check (launcher, Wrapper,
`dxvk_probe`, d3d11 test program on real DXVK DLLs, AIO-Graphics-Test).
Releases are GitHub pre-releases until `1.0.0`.

## Alpha

| Version | Content | Gate |
|---|---|---|
| `0.0.6` (done) | transform feedback (`0060`), 64-bit shift compiler fix (`0061`), tessellation (`0062`), geometry shader (`0063`) | `dxvk_probe`: 1.10.3 D3D11 + D3D9 OK, 1.11.1 OK; 2.3.1 only misses `geometryStreams` |
| `0.0.7` ✅ | `geometryStreams`, XFB from a GS, GS after tessellation, indirect draws with tessellation or a GS (patches `0064`, `0065`) | 2.3.1 `dxvk_probe` OK; CTS `transform_feedback.*` streams cases, `geometry.*` and `tessellation.*` indirect cases |
| `0.0.8` ✅ | Phase 9 small features: `variableMultisampleRate`, `vertexPipelineStoresAndAtomics`, `multiViewport`, clip/cull distance in the driver, depth clamp fix (patch `0066`). `depthBounds` moved out (no v9 hardware field, see roadmap 9.1) | 1.10.3 at FL 11_1 ✅; the 568 tessellation CTS cases that need `multiViewport` run ✅ (248 pass, 320 need `shaderFloat64`) |
| `0.0.9` | Remaining correctness gaps: cube layered rendering, `primitive_id` to the FS, primitive restart before a GS, pipeline statistics | CTS `geometry.*` 0 fail |

## Beta

Entry (`0.1.0`): every DXVK profile in `dxvk_probe` is OK without the
EXPERIMENTAL guards, no CTS crash in the tracked groups, and at least one
real game per D3D level (9, 10, 11) reaches gameplay in Winlator.

| Version | Focus | Gate |
|---|---|---|
| `0.1.x` | Feature complete, stabilise | test matrix of games, every crash report reproduced or explained |
| `0.2.x` | Memory (Phase 8.6 + 12): RAM growth in games, heap size reported to apps, BO caching, poly heap and tiler heap budgets | Little Nightmares 1/2 no longer killed by Android; RAM curve logged over 30 min |
| `0.3.x` | Speed (Phase 13): asynchronous job dependencies (kbase `pre_dep`) instead of a CPU wait per job chain, fewer job barriers in the libpoly path | frame time A/B on the same scenes, no CTS change |
| `0.4.x` | Android native surface (Phase 8.5) and device coverage: other G57 SoCs (G100, Dimensity 6080/6100), atom stride detection, other Valhall v9 JM GPUs | at least 3 more devices confirmed by testers with logs |
| `0.5.x` … `0.9.x` | Fixes from tester reports, upstreamable cleanup of the patch stack | open crash reports closed or documented |

## 1.0.0 exit criteria

- All tracked CTS groups: 0 fail, 0 crash, 3 runs, on every release candidate.
- DXVK 1.10.3, 1.11.x, 1.12.1-sarek and 2.x start on every listed device.
- The game test matrix shows no crash in the first 30 minutes on Mali-G57 MC2.
- No release-candidate regression report for 2 weeks.
- The 🧪 EXPERIMENTAL tags are either removed (implementation verified
  against CTS) or documented as a permanent emulation with its limits.
