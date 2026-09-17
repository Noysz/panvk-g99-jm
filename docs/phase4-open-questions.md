# Open questions and false-positive risks — Phase 4.1 to 4.4

Written after the fact, deliberately looking for ways the published results could
be wrong rather than for reasons to trust them. Every entry says what was
actually measured, what the measurement cannot distinguish, and whether it was
followed up.

Labels: **RESOLVED** means it was chased down and settled. **OPEN** means it is a
real gap. **ACCEPTED** means it is a known limit that is not worth closing now
and is documented as scope rather than as a finding.

---

## 1. Cross-cutting: the instrumentation is ours

Almost every number in 4.2 and much of 4.4 was read from debug prints added by
this project (`PANVK_DEBUG_RESTAB`, `PANVK_DEBUG_MVJ`, the job-status dump). If a
print reads the wrong variable, the log agrees with itself and the error is
invisible.

Mitigation actually applied: the descriptor-level claims that matter were
cross-checked against something the instrumentation does not control — the
framebuffer contents. `res_count = 8` for four sets is a printed number, but the
four colour channels arriving correctly is not.

**Where that mitigation does not reach:** the two-preparations-per-draw
observation and the 544/32-byte driver-set sizes in 4.2 rest on the print alone.
Nothing in the pixel output would change if those numbers were misreported.
**Status: OPEN**, low consequence.

---

## 2. Phase 4.1, indexed draws

### 2.1 The original 4.1 conclusion was incomplete — RESOLVED, and it mattered

4.1 was closed claiming indexed draws work, on the strength of `a16`, `a32`,
`b16`, `voffset`, `degen` and `zero`. **`firstIndex` was never tested.** When it
was finally tested during 4.4, it turned out to be **broken** — ignored
entirely, because the INDICES section was pointed at the index buffer base
without folding in `firstIndex * index_size`.

So the honest reading is that 4.1 as originally published overstated its scope.
It validated index *fetch*, `vertexOffset`, and degenerate handling. It did not
validate `firstIndex`, and `firstIndex` did not work.

Fixed and now tested (`firstidx` case, 253 px). The lesson is the general one:
"indexed draws work" was a claim about a feature, while the tests only covered
some of its parameters.

### 2.2 Would `a16` pass even if the index buffer were ignored? — RESOLVED

Yes, and this is worth stating because it is the obvious trap. Indices `{0,1,2}`
are the same as the default vertex order, so a driver that ignored the index
buffer entirely would still produce the reference image for `a16`.

That is exactly why `b16` exists: indices `{3,4,5}` select a different triangle,
and it rendered the different triangle. Together with `voffset` landing
byte-identically on `b16` through a different API route, the index path is
genuinely exercised. `a16` alone would have proved nothing.

### 2.3 Out-of-range indices — ACCEPTED

v9 has no index-buffer size field. Upstream's own comment
(`panvk_vX_cmd_draw.c`, the `CmdBindIndexBuffer2` NullDescriptor path) works
around this only for v10+. Out-of-range index behaviour is therefore undefined by
construction on this hardware and was deliberately not tested. Nothing here
claims otherwise.

### 2.4 Only UINT16 and UINT32, only triangle lists — ACCEPTED

`VK_INDEX_TYPE_UINT8` untested. Primitive restart untested despite the field
being emitted. Strips and fans untested.

---

## 3. Phase 4.2, resource table

### 3.1 Could the UBO reads be constant-folded? — RESOLVED

If the compiler had folded the uniform values into the shader, the pixels would
be right for the wrong reason and no descriptor would be read.

`2sets_alt` rules that out: same shader binary, same bindings, different buffer
contents, and the centre pixel moved from `191,128,0` to `64,255,0`. The values
are read at runtime.

### 3.2 Only one descriptor type was ever tested — OPEN

Every 4.2 test used `VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER`, one binding per set.
Untested: storage buffers, combined image samplers, dynamic uniform/storage
buffers, input attachments, multiple bindings within one set, arrays of
descriptors.

The resource-table plumbing is shared, so those probably work, but "probably"
is the operative word. The published claim should be read as *uniform buffers
across multiple sets work*, not *descriptor sets work*.

### 3.3 The T4.2.4 conclusion was misread once — RESOLVED, worth recording

The first reading of the `res_table = 0` control was that geometry died, because
the framebuffer came back 0/4096. That was wrong. The triangle was rasterizing
the whole time and painting `0,0,0` because all three of its colour channels
came from zeroed descriptors, which is indistinguishable from the clear colour.

T4.2.6 caught it by making one channel a shader constant. Without that follow-up
a false statement would have entered the record as a verified finding.

**Generalisation worth keeping:** any test whose failure mode is "the expected
colour is absent" cannot distinguish *not drawn* from *drawn in the background
colour*. At least one channel must be independent of the thing under test.

### 3.4 Stage attribution rests on one workload — OPEN

The 544 B driver set was attributed to vertex and the 32 B one to fragment
because the UBOs were declared fragment-only, so the stage reporting
`used_set_mask = 0x3` had to be the fragment stage. That reasoning is sound but
it is a single workload. A shader with vertex-stage descriptors would confirm or
break it, and was not run.

---

## 4. Phase 4.3, multiple render targets

### 4.1 The aliasing check has a real hole — OPEN

The test asserts RT0 and RT1 are not byte-identical. **Partial** aliasing would
pass it: if the two attachments overlapped in only part of their memory, the
buffers would still differ somewhere and the check would report PASS.

What would close it: write a known pattern into RT1, draw only to RT0, and verify
RT1 is unchanged byte for byte outside the drawn region. The `sq2only0` case is
close to this but not the same — it checks RT1 contains no blue, not that RT1 is
bit-for-bit what it was before the draw.

Not closed. The claim "attachments are independent" should be read as *not fully
aliased*.

### 4.2 Excluded pixels are not scored — ACCEPTED, with a caveat

64 pixels for the square and 128 for the circle were classified ambiguous and
excluded from mismatch scoring. If the GPU were wrong on exactly those pixels,
the test would not notice.

Their treatment was reported rather than scored, and it looked correct — for the
square, 32 drawn along the shared diagonal and 32 clear on the diagonal's
extension outside the shape. But "looked correct on inspection" is weaker than
"scored".

The ambiguity metric is also conservative in a way that costs coverage: it
measures distance to the infinite edge *line*, not the edge *segment*, so pixels
far from the triangle but near the line's extension get flagged. That direction
is safe — nothing wrong is scored as right — but it means fewer pixels are
checked than could be.

### 4.3 Two attachments only — ACCEPTED

The device reports `maxColorAttachments = 8`. Only 2 were tested. Mixed formats,
per-attachment blend state, and 3 through 8 attachments are all untested.

### 4.4 The circle result is a bound, not an equality — ACCEPTED

The square has an exact arithmetic answer (1024) and hit it. The circle has no
closed form, so the check is `1504 <= 1568 <= 1632` plus zero per-pixel
mismatches on scored pixels. That is weaker than the square's check. A
systematic error affecting only ambiguous boundary pixels would fit inside the
bound.

---

## 5. Phase 4.4, indirect draw and dispatch

### 5.1 The survey's prediction was wrong — RESOLVED

The survey predicted `vkCmdDispatchIndirect` would silently do nothing. Measured,
it **faulted the GPU** (`exception_status = 0x10258`, job `type = 0`).

The mechanism reasoning was right — the job is parked as `NOT_STARTED` and the
never-dispatched helper would have promoted it — but the predicted observable was
wrong. Recorded as a correction in the survey rather than quietly adjusted.

### 5.2 Placeholder sizing: the biggest false-positive risk — RESOLVED

The entry points record `vertex.count = 1` as a placeholder, and every early test
used `vertexCount = 3`. If anything inside `prepare_draw_v9()` were sized from
the placeholder, a small count could work by luck and a real workload would
break.

Followed up rather than left as a caveat. With a shader whose coverage scales
with vertex count:

| vertexCount | placeholder | direct | indirect | framebuffer sha256 |
|---|---|---|---|---|
| 3 | 1 | 2 px | 2 px | `c9e3a37288d9` both |
| 30 | 1 | 20 px | 20 px | `31d64ea896ea` both |
| 90 | 1 | 60 px | 60 px | `180df7ab58a1` both |

Byte-identical at every count, up to 90× the placeholder, zero faults. Also
`vcount6` and `idx_count6` at 6× via the two-triangle shader.

**What this still does not cover:** counts in the thousands, and any workload with
varyings or vertex buffers, where record-time sizing genuinely does depend on the
count. Those remain OPEN and are documented as scope.

### 5.3 The job ordering claim was reached the wrong way — RESOLVED, then re-opened

Two separate things were conflated here and they need pulling apart.

**What was claimed at first, and how, was wrong.** An early note recorded "job
order observed: type 4 helper, then type 11 draw, then type 9 fragment". That
sentence describes execution order and was read off the job-status dump. The dump
iterates `batch->jobs`, a dynarray appended to by hand at each call site
(`panvk_vX_cmd_buffer.c`, `panvk_vX_cmd_dispatch.c` and others). It reports the
order pointers were *recorded*, not the order the GPU *ran* them. The real dump
shows the opposite listing anyway — `job[0] type=11`, `job[1] type=4` — because
the draw job's pointer is appended before the helper is dispatched.

That claim never reached this repository, which was checked before writing this.
But the method was unsound: execution order was inferred from a dump that does
not report execution order.

**What is now properly established.** The dump was extended to decode `Index`,
`Dependency 1`, `Dependency 2` and `Next` from the job header
(`v9.xml` "Job Header": index at word 4 bits 16..31, the two dependencies in the
halves of word 5). Read back from real descriptors:

| case | helper | draw job | dependency |
|---|---|---|---|
| direct | none | `index=1` | `dep1=0 dep2=0` |
| indirect | `index=1` | `index=2` | **`dep2=1`**, pointing at the helper |
| `twodraws` | `index=1`, `index=3` | `index=2`, `index=4` | `dep2=1` and `dep2=3` |

So the dependency is explicit, points the right way, and each draw in a
multi-draw batch depends on its own helper.

**Ordering demonstrably holds**, and this does not depend on the dump at all. The
CPU placeholder is `vertexCount = 1`, which is degenerate and covers no pixels.
Every load-bearing case comes back with the helper's value instead. The draw job
therefore always read the patched descriptor, never the placeholder, in every run
recorded here.

**What is NOT established, and this is the re-opened part.** That the explicit
dependency is what enforces the ordering. An env-gated control
(`PANVK_INDIRECT_NODEP=1`) sets the dependency to 0 while leaving everything else
alone, confirmed in the dump as `dep1=0 dep2=0`. The output stays correct:

| case | with dependency | dependency removed |
|---|---|---|
| `firstvtx` | 253 px | 253 px, 15/15 runs |
| `twodraws` | 765 px | 765 px, 15/15 runs |
| `many90i` | 60 px | 60 px, 10/10 runs |

40 runs, one unique fingerprint per case, zero faults. **The negative control
failed to fail.** Ordering on this hardware for these workloads is sufficiently
guaranteed by chain-order serialisation — the helper sits earlier in the job
chain's `next` list — so the dependency is not the thing making it work.

The dependency is kept regardless, because expressing the constraint is the
architecturally correct thing to do and matches what Bifrost does. But it is
retained on principle, not because a test demonstrates it. **If a future change
reordered the chain, no test here would catch the regression.**

The experiment that would separate the two hypotheses is to add the draw job to
the chain *before* the helper while keeping the dependency: if ordering survives,
the dependency is doing the work; if it breaks, chain order was. That requires
restructuring the submission path, because the dependency needs an index that
does not exist until the helper is added, and it was not done.

### 5.3a `twodraws` could in principle be one draw — RESOLVED

765 non-black could be a single draw covering 765 pixels rather than two draws of
512 and 253. The quadrant breakdown settles it: `445,192,64,64`, where
445 = 192 + 253 in the top-left. Two distinct shapes in distinct places.

### 5.4 firstInstance is not implemented — ACCEPTED, and deliberately

The helper does not patch `PRIMITIVE.instance_offset`, because the direct path
does not write it either. Patching it would have made indirect behave
*differently* from direct, which would break the byte-identity property that all
the other evidence rests on.

This means `firstInstance` is ignored on v9 for both direct and indirect draws.
That is a pre-existing gap in the direct path and should be fixed there first.
Untested either way, since no test used a non-zero `firstInstance`.

### 5.5 Instancing barely exercised — OPEN

`instanceCount` was only ever 1 or 0. An `instanceCount` of 2 or more was never
run, on any path. The instance-count patching in the helper is therefore
unverified for values above 1, even though it is the same 32-bit field write.

### 5.6 Indexed indirect skips the min/max index scan — ACCEPTED

Bifrost runs an index min/max search before an indexed indirect draw. That scan
exists to size varying and attribute buffers from the range of vertices the
indices reference. The v9 path here does not do it.

That is fine for workloads with neither varyings nor vertex buffers, which is
what was tested. It is not fine in general, and no test would currently catch the
difference. Directly connected to 5.2's remaining gap.

### 5.7 The compute FAU fix has an untested edge — OPEN

`cfg.compute.fau_count = cs->fau.total_count` was verified by a constant that
previously read as zero and now reads correctly. The field is 8 bits wide
(`v9_pack.h`, bits 0..7), so a shader needing more than 255 FAU words would
silently truncate. No check was added and no such shader was tried.

### 5.8 `dispatch_precomp` values were copied from the CSF path — PARTLY RESOLVED

Three values in the new v9 `dispatch_precomp` were taken from the CSF
implementation rather than derived independently: resource table 0, FAU count
from `DIV_ROUND_UP(sysvals + data_size, 8)`, and program from `shader->spd`.

**First, a correction to how this was described.** Earlier notes said v9's
`dispatch_precomp` "was an empty stub", which implied the stub came from
upstream. It did not. Upstream Mesa does not build a v9 Job Manager target at
all: `src/panfrost/vulkan/meson.build` has `jm_archs = [6, 7]` and the arch loop
is `[6, 7, 10, 12, 13, 14]`. This project added arch 9 to both, and having done
so, its own patch `0011` stubbed `dispatch_precomp` to an empty body for
`PAN_ARCH >= 9` so the shared object would link. Its comment said as much. The
stub was this project's scaffolding, not an upstream property.

**Why this matters more than a wording fix.** Because the stub was empty, every
precompiled-kernel path on v9 silently did nothing. Porting the function made
those paths reach real code, so a wrong value would have moved from "does
nothing" to "does something unverified". Three call sites are reachable from
ordinary API calls:

| kernel | reached from | before patch 0032 | after |
|---|---|---|---|
| `panlib_fill*` | `vkCmdFillBuffer` | silently did nothing | **VERIFIED-HW** |
| `panlib_clear_query_result` | `vkCmdResetQueryPool` | silently did nothing | still untested |
| `panlib_copy_query_result` | `vkCmdCopyQueryPoolResults` | silently did nothing | still untested |

**`vkCmdFillBuffer` is now tested.** `cmd_meta.c` picks one of four paths by
alignment and size, and all four are covered deliberately rather than
incidentally: `panlib_fill_uint4` and `panlib_fill_uint4_scalar` when address and
range are both 16-byte aligned, `panlib_fill` and `panlib_fill_scalar` otherwise.
Workgroups cover 32 elements, so cases are sized against a 512-byte or 128-byte
workgroup as appropriate.

The buffer is preloaded with a pattern that is a function of the byte offset, so
an untouched byte holds a known value rather than merely "not the fill value".
Each case verifies the region before the fill, the fill range, and the region
after, so **overrun is caught in both directions**, not only underrun.

A/B against a single build via `PANVK_PRECOMP_STUB=1`, which reproduces the
patch-0011 stub exactly:

| case | precomp active | stub |
|---|---|---|
| `u4_bulk` | 1024/1024, PASS | 4/1024, 1020 wrong, FAIL |
| `sc_tail` | 136/136, PASS | 1/136, 135 wrong, FAIL |
| `middle` | 512/512, PASS | 0/512, 512 wrong, FAIL |
| `nop` control | PASS | PASS |

All eight cases pass with precomp active, zero faults. The small non-zero counts
in the stub column are coincidental matches, since roughly 1 byte in 256 of a
pseudorandom pattern equals the expected fill byte, and 1024/256 = 4 exactly.
That the coincidence rate matches prediction is itself a check that the harness
counts real bytes.

The `nop` control passing in *both* columns is the point of it: nothing is
dispatched either way, so a failure there would mean the harness was wrong.

**The two query paths are now tested as well.** Occlusion queries were the right
vehicle because the answer is independently known: the validated triangle covers
exactly 512 pixels and its sibling 253, numbers established four separate ways in
Phase 4. With no depth or stencil attachment and one sample per pixel, a precise
occlusion query counts exactly the covered pixels, so the query is checked against
something outside the query machinery. `VK_QUERY_CONTROL_PRECISE_BIT` is required
for this; without it the driver selects `MALI_OCCLUSION_MODE_PREDICATE` and any
non-zero value would be conformant.

Three numbers are collected per case. Framebuffer coverage involves no query at
all. `vkGetQueryPoolResults` reads report memory directly on the host with no
kernel (`panvk_vX_query_pool.c:234`). `vkCmdCopyQueryPoolResults` goes through
`panlib_copy_query_result`. The middle one isolates the copy kernel, since only
the third runs it.

| case | pixels | host | device copy | verdict |
|---|---|---|---|---|
| `tri_a` | 512 | 512 | 512 | PASS |
| `tri_b` | 253 | 253 | 253 | PASS |
| `both` | 765 | 512 + 253 | matches host | PASS |
| `zero` | 0 | 0 | 0 | PASS |
| `reset_clears` | 512 | n/a | 512 | PASS |

All six cases pass, 3x each, identical fingerprints, zero faults. `both` summing
to 765 independently reproduces the `twodraws` figure from 4.4.

**The copy destination is poisoned with `0xDEADBEEFDEADBEEF` before submission.**
Without that, "the kernel wrote zero" and "the kernel did not write" would be
indistinguishable, which matters because the `zero` case legitimately expects 0.
Under `PANVK_PRECOMP_STUB=1` the poison survives untouched in every case, while
the host read still returns the right value — a direct demonstration that the two
readback paths are independent.

**A trap this test initially fell into.** The first five cases all passed *without*
`vkCmdResetQueryPool`, so they did not test `panlib_clear_query_result` at all.
The reason is that `CmdBeginQuery` zeroes the counter itself with `WRITE_VALUE`
jobs, to satisfy the spec requirement that a query starts at zero. A completely
broken clear kernel would still have produced correct sample counts. What
`vkCmdResetQueryPool` actually owns is *availability*.

The `reset_clears` case was added to target it: run a query and copy it, then
reset, then copy again without `WAIT_BIT`, since waiting on a query just made
unavailable would never return. Availability goes `1` then `0` with precomp
active. Under the stub the second copy never writes at all, which the case
reports as "cannot judge" rather than as a pass.

A second self-inflicted error is recorded in the harness comments: the host read
happens after the whole command buffer has run, so for `reset_clears` the query
is legitimately unavailable and zeroed by then. Asserting the host value there was
testing the test, and the assertion is now skipped for that case while the device
copy recorded before the reset is still checked.

### 5.8a Other precomp users beyond the reachable three — OPEN

`libpan_shaders_v9.h` also registers `PANLIB_PREFIX_SUM_TESS = 15`. Tessellation
is not exposed by this driver, so that kernel is unreachable today and untested.

---

## 6. Things that apply to all four sub-phases

### 6.1 One workload shape, over and over — OPEN

Nearly everything was validated with a 64×64 linear `R8G8B8A8_UNORM` offscreen
target, no depth, no blending, no MSAA, single layer, and shaders that avoid
vertex buffers by indexing a hardcoded array. That shape was chosen to isolate
variables and it did its job, but it means the results generalise less than the
number of passing tests suggests.

### 6.2 Nothing was displayed — ACCEPTED

Every image in this phase is a `vkMapMemory` readback of an offscreen buffer.
This driver has still never presented a frame. WSI is Phase 6.

### 6.3 Three-times repetition catches flakes, not systematic error — ACCEPTED

The 3× rule detects nondeterminism. It does nothing against a consistent
misunderstanding: a wrong expectation reproduces perfectly. The protection
against that is the negative controls and the byte-identity comparisons, not the
repetition.

### 6.4 No conformance testing — ACCEPTED

No CTS was run. "Works for this test" is the ceiling of every claim here.
