# Naruto GPL and K2 follow-up, 2026-10-20

Naruto Storm 2 renders its title and initial village with GPL enabled when
`dxvk.enableAsync = False` is set in the installed DXVK 2.3.1-gplasync package.
The K2 descriptor change is observed, but the two comparable village pairs
show no performance gain. T04 remains partial because its three-pair target
was not met.

The [evidence directory](../evidence/validation/naruto-k2-2026-10-20/README.md)
contains selected images, complete timing CSVs, controls, analyses and
exclusions. These runs use drvB19, SHA-256
`d77c3c46ecd24889b34f1fe1ebd80f284622be729673c568ce1e62c244dbd114`,
Build ID `0ba47c48e7eaed58d0ceff16127c0e954612ba02`, and the baseline wrapper
`3b4c285550847a0caa6d110614298d6c0c57feefd9c96460713ad707a3c8304e`.
They measure a diagnostic configuration, with UBO push and deferred occlusion
disabled, rather than the maximum-performance preset.

## Title rendering

| Capture | GPL | Async | Title | Draws in the captured UI submission |
|---|---|---|---|---:|
| on-ui | True | Package default | Background/panels missing | 12 |
| off-ui | False | Package default | Background/panels visible | 34 |
| on-noasync | True | False | Background/panels visible | 34 |

With GPL=True and async=False, the 34 fragment shaders match the earlier
GPL-on machine-code dump. They still use descriptor varyings and LD_VAR_IMM.
Eight draws using the 96x96 texture and eleven bottom-panel draws return.
The difference is already present in submitted draws, before fragment shading.
The earlier black GPL-on title therefore does not establish a varying bug.

The public gplasync 2.3.1-1 patch queues asynchronous compilation when a
pipeline is missing. The corresponding DXVK reference source returns early
from compilation when GPL is forced on. This supports an explanation for
skipped draws, but the exact source provenance of the installed ARM64EC DLL
has not been established. The directly measured result is that the explicit
async=False control restores draws and the image with GPL still enabled.
The reference patch URL and hash are in
[`dxvk-reference-origin.json`](../evidence/validation/naruto-k2-2026-10-20/title/dxvk-reference-origin.json).

These counts cover the selected submission, not a whole frame. Animations are
not synchronized. The off-ui runner missed its snapshot marker and exited 1;
the saved manual check verifies the complete snapshot and submission order.
That failure state remains in the evidence. The positive title control was
one process, not three repeated title tests. No new varying fix follows from
this result, and no claim of general GPL correctness is made.

## K2 descriptors

A means `PANVK_V9_CLIP_KILL_FPK=0`, with metadata restoration active.
B means `PANVK_V9_CLIP_KILL_FPK=1`, with conservative metadata. Both retain
clip/cull lowering.

Each village capture contains 143 draws and 34 fragment jobs. Of 109 matched
shader/state groups, 15 groups covering 17 draw instances change only
`Allow forward pixel to kill`, from false in B to true in A. Shader machine
code and the compared primitive, depth/stencil, blend and unaffected flags
match. All affected draws have Index count 6, Index type None and depth
function Always. This establishes that K2 reaches the descriptor; it does
not establish a reduction in total frame cost.

The first pilot snapshot caught compute work only and was excluded. The
capture trigger was corrected before the two render captures. No capture or
shader dump ran during the timed comparisons.

## Village measurements

All runs use the same frozen initial save/configuration, a fresh process,
30-second warmup and 90-second measurement, followed by a 30-second cooldown.
The swapchain is 640x360 inside an 848x480 Wine desktop. GPL=True,
async=False, `PANVK_UBO_PUSH=0`, `PANVK_DEFER_OQ=0`, both shader/state caches
disabled, and the display of the timing overlay hidden.

| Run | K2 | FPS | Median frame ms | P95 ms | Draw min/max | Comparison |
|---|---|---:|---:|---:|---:|---|
| A1 | Active | 10.024 | 96.181 | 136.365 | 451/480 | Excluded pair |
| B1 | Control | 10.345 | 90.881 | 141.892 | 466/466 | Excluded pair |
| A2 | Active | 10.531 | 90.076 | 132.605 | 466/466 | Primary |
| B2 | Control | 10.645 | 85.829 | 154.626 | 466/466 | Primary |
| A3 | Active | 9.605 | 92.531 | 162.774 | 466/466 | Primary |
| B3 | Control | 10.415 | 88.480 | 142.539 | 466/466 | Primary |
| A4 | Active | 10.910 | 82.734 | 149.047 | 466/466 | Excluded pair |
| B4 | Control | 9.945 | 90.248 | 167.872 | 451/469 | Excluded pair |

A1 loses part of its HUD before timing and differs from B1 at the end. The
cause is unknown; the initial idle-fade explanation was unproven. One
replacement pair was planned before its results were known. B4 has complete
endpoint images, but 212 of 895 measured intervals have different draw
counts: 58 at 469 draws and 154 at 451, between approximately seconds 4.02
and 27.26 of the selected intervals. No image was captured during that window,
so its cause is also unknown. A4/B4 is excluded despite A4's higher FPS.
No window was trimmed to select a favorable or more comparable portion.

Only pairs 2 and 3 remain: active K2 records -1.074% and -7.776% FPS relative
to control. The medians of the two runs per condition are 10.068 and 10.530
FPS, a descriptive difference of -4.388%. The target of three comparable
pairs remains unmet; the one-replacement limit was respected.

The analyzer sums complete frame intervals inside recorded byte boundaries,
discarding the first interval that could cross the start. The runner sends
no input and takes no screenshots during timing. External input was not
comprehensively monitored. NPC phases vary, and A always precedes B, so
ordering effects are not separated. CPU clocks vary; GPU clocks and
temperatures are unavailable. Two pairs cannot establish a general regression.

Saved process/D3D logs contain no device-lost or GPU-fault match. Fontconfig,
EDID, OpenVR and audio messages remain; A3 also has an ALSA assertion and B2
a Wine bad-file-descriptor message. Both complete timing, but untimestamped
logs cannot locate these messages within the run. This is not a kernel-log
audit or proof that the runtime is error-free.

## Short Free Battle check

COM vs COM, Single Match, with in-game Random Select for each character and
the arena. The result is Kakashi versus Shino; the arena name was not recorded.
The combat images span 179.7 seconds. The run stops before a KO.

The sampled images show both fighters, the arena, HUD, chakra glow,
substitution, a hit counter, Hidden Jutsu: Beetle Sphere and camera changes.
This is visual sampling without a blob/golden pixel reference or a complete
match. It does not establish correctness for every effect.

A 20-second interval between screenshots records 281 complete frame intervals
over 19.955 seconds: 14.082 FPS, median 70.454 ms, p95 86.296 ms and maximum
122.214 ms. No screenshot or interval over 250 ms occurs in that window.
An earlier observational sample records 11.428 FPS and an 851.022 ms maximum,
with capture activity elsewhere in the run. The scenes differ, so these are
not K2 A/B results. Sustained low FPS remains without screenshots; its CPU/GPU
cause has not been isolated.

## Remaining work

Explain the transient HUD/draw changes before further comparable measurements,
then profile CPU, submission/wait and GPU costs with complete rendering.
The driver, wrapper and original save/configuration retain their recorded
hashes. The active source already includes T05 and is not identical to the
source state behind the drvB19 binary used here.

These results do not change defaults or qualify a release. T04 remains
partial, T05 remains opt-in, and the existing T06-T10 sequence is unchanged.
