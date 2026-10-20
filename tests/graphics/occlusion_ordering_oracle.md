# Occlusion ordering oracle

`occlusion_ordering_oracle.c` is a standalone Vulkan 1.3 test. It explicitly
enables precise occlusion queries and dynamic rendering. The ICD is selected
with `PANVK_ICD_SO`; a Vulkan loader can also be selected through that variable
to enable external validation layers.

An oversized triangle covers a 64x64 single-sample framebuffer. Integer
scissors restrict the first draw to 32x16 pixels and a disjoint draw to 16x8
pixels. Depth is cleared to 1, the shader writes depth 0.5, and the pipeline
uses LESS with depth writes enabled. A second draw over the first rectangle
therefore fails depth. The expected query results are independently fixed:

| Query | Work | Samples |
|---|---|---:|
| 0 | First rectangle | 512 |
| 1 | No draw | 0 |
| 2 | Same rectangle, equal depth fails LESS | 0 |
| 3 | Disjoint second rectangle | 128 |
| 4 | No draw, then reset after a completed copy | 0, then unavailable |

Every framebuffer pixel is checked against the two exact red rectangles on
black, including alpha. Host query results and GPU copies with WAIT are checked
against the analytical counts. GPU copies cover both 32-bit and 64-bit results,
availability words, and padded strides. Destination bytes start poisoned.
Post-reset reads omit WAIT and must report unavailable; the host read must
return `VK_NOT_READY`.

The same recorded primary command buffer is submitted three times, with a
fence between submissions. `--secondary` records the query/draw sequence in a
secondary command buffer with dynamic-rendering inheritance and executes it
inside the primary render pass. Both modes exercise reset/reuse and sequential
resubmission. Inherited active queries are not tested. Explicit
memory barriers order the early snapshot and reset checks; the main query-to-
WAIT-copy path has no added blanket barrier between EndRendering and the copy.

`--early-copy-control` deliberately feeds a pre-query, non-waiting GPU copy to
the completed-result checker. The Vulkan operations remain valid. All four
query checks must fail in each cycle because availability is zero. This tests
rejection of an early unavailable snapshot; it does **not** inject premature
availability into the driver or prove that all stale-result races are caught.

## Deferred-candidate checks

- `--lifetime`: abandon four primary recordings after query ends but before
  EndRendering. Exercise command-buffer reset, pool reset, command-buffer free
  and pool destruction, then perform the usual three checked submissions.
  Allocation callbacks require zero live pool allocations at final destruction.
- `--simultaneous`: use SIMULTANEOUS_USE for primary and optional secondary;
  submit the same primary three times per cycle without an intervening host
  wait. A leading memory barrier orders shared resource reuse. This tests
  pending resubmission, not execution on multiple hardware queues.
- `--poll-availability`: scale the framebuffer and scissors by 32, giving
  counts 524288/0/0/131072. Enable hostQueryReset and reset before each submit
  so polling cannot mistake availability from a previous execution for the
  current result. Check query 0 immediately when it becomes available, before
  the fence wait, then check every final pixel and result. This mode cannot
  be combined with simultaneous resubmission.
- `--allocation-failure`: arm a failing command-pool allocator during the
  first EndQuery. The deferred candidate must return OUT_OF_HOST_MEMORY from
  EndCommandBuffer, recover after reset, and release all tracked allocations.
  This is a targeted driver test; an implementation that does not allocate
  at EndQuery cannot satisfy its injected-failure prerequisite.
- `--empty-outside`: after the normal cycles, reset/re-record the primary
  three times with an empty query outside rendering, a WAIT copy and a host
  read. All results must be zero and available. This targets closing a
  content-less producer with pending availability.

For the deliberately faulty candidate, combine `PANVK_DEFER_OQ=1` and
`PANVK_DEFER_OQ_EARLY_CONTROL=1` with `--poll-availability`. A successful
negative control exits 2 with three failed first-availability observations,
while all final counts and pixels still pass. A crash, timeout, or a final
rendering failure is not an equivalent outcome. Polling is timing-sensitive;
the faulty control must actually be detected on the test device.

The lifetime and failure callbacks count allocations routed through the
command pool allocator. They are not a general host/GPU memory-leak detector.
The candidate does not advertise inherited active queries.

## Build and run on Termux

Run in a separate output directory, with absolute source paths as needed:

```sh
glslangValidator -V --target-env vulkan1.3 tests/shaders/occlusion_oracle.vert -o occlusion_oracle.vert.spv
glslangValidator -V --target-env vulkan1.3 tests/shaders/occlusion_oracle.frag -o occlusion_oracle.frag.spv
clang -std=c11 -Wall -Wextra -Werror -O2 tests/graphics/occlusion_ordering_oracle.c -o occlusion_ordering_oracle -ldl
LD_LIBRARY_PATH=/data/data/com.termux/files/usr/lib PANVK_ICD_SO=/absolute/path/libvulkan_panfrost.so MESA_SHADER_CACHE_DISABLE=true timeout 120 ./occlusion_ordering_oracle
LD_LIBRARY_PATH=/data/data/com.termux/files/usr/lib PANVK_ICD_SO=/absolute/path/libvulkan_panfrost.so MESA_SHADER_CACHE_DISABLE=true timeout 120 ./occlusion_ordering_oracle --early-copy-control
```

Expected exits: 0 for the positive run, 2 with twelve rejected query checks for
the early-copy control. Missing support, loader errors, timeout or a generic
nonzero exit are not successful negative controls. A Vulkan fence wait has a
30-second timeout; the external process timeout also bounds a stuck API call.
Repeat both commands with `--secondary` to cover the secondary recording path.
Host-development ICDs may also require
`PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1` to enumerate this experimental device.

The historical `occlusion_query_test.c` is not the acceptance oracle for a
deferred-availability change: it does not enable `occlusionQueryPrecise`, its
`noreset` case uses a query without the required reset, and `reset_clears`
performs a host WAIT on the reset query. Those limitations invalidate using
its advertised case list as proof that these paths are correct.
