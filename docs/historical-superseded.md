# Historical and superseded findings

Nothing here is deleted. Each entry states what was believed, why it looked
right, and what actually turned out to be true. Wrong turns are kept because the
reasoning that produced them is reusable — and because a reader who repeats the
same observation should find the correction instead of the dead end.

---

## 1. "Tiler heap is UNREADABLE, therefore the tiler did no work"

**Label: HISTORICAL-SUPERSEDED. This conclusion is false.**

The driver prints, after each submit:

```
[PANVK_DEBUG_HEAP] ... bo->flags=0x2 dev=0x... size=134217728 host=0x...
  mincore=absent -- UNREADABLE (I/O error)
  => pages not committed, tiler wrote nothing
```

The reasoning was: the tiler heap BO cannot be read from the CPU, `mincore`
reports the pages absent, so the tiler never wrote anything, so the tiler is
broken and that is why nothing renders.

**Why it is false:** the identical message appears in runs that render
correctly. Compare:

* [`ab_A_run.log`](../evidence/logs/ab_A_run.log) — UNREADABLE, 0/4096 pixels
* [`ab_B_run.log`](../evidence/logs/ab_B_run.log) — **UNREADABLE, 4096/4096 pixels**
* [`PANVK_G57_triangle_fresh_20260917-004051.log`](../evidence/logs/PANVK_G57_triangle_fresh_20260917-004051.log)
  — **UNREADABLE, 512/4096 pixels, correct partial triangle**

A message that is present in both the failing and the succeeding case carries no
information about the failure. CPU-unreadability of a GPU-private heap is
expected: the pages are not required to be CPU-mapped or committed for the GPU
to use them.

The trailing `=> pages not committed, tiler wrote nothing` text in the debug
print is itself the stale conclusion, still emitted by the driver. **Treat that
sentence as a comment from a past hypothesis, not as a measurement.** The
measurement is only `mincore=absent` / `I/O error`.

Exact location, for anyone who wants to remove it: the Indonesian original
`" tiler tidak menulis apa pun\n"` is at
`src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c:148` in the driver work tree, and is
therefore carried inside patches
[`0005`](../patches/0005-panvk-v9-kbase-jm-submit-and-debug.patch) and
[`0012`](../patches/0012-panvk-v9-jm-queue-submit-and-raw-capture.patch). The
code comment immediately above it repeats the same false reasoning —

```c
/* EIO here means the page is mapped but not backed: the GPU never
 * faulted it in, i.e. nothing wrote to the heap. */
```

— so both the comment and the printed string need correcting together. Both are
left in place deliberately so the published patches match the binary that
produced the published logs; changing them now would desynchronise the two.

**Superseded by:** [fau-root-cause.md](fau-root-cause.md).

---

## 2. "Non-zero `exception_status` means the job failed"

**Label: HISTORICAL-SUPERSEDED.**

An early version of the job-status print treated any non-zero exception status as
an error. `BASE_JD_EVENT_DONE` is `0x01` — success is non-zero. The check
inverted the meaning of every successful job and, per the note left in the
driver source, "sent the investigation down a dead end".

**Correct rule:** `0x00` = not started, `0x01` = done, `< 0x40` = non-fatal,
`>= 0x40` = actual fault. Table in
[raw-jm-progress.md](raw-jm-progress.md#event-codes).

---

## 3. "The fullscreen red result proves rasterization works"

**Label: HISTORICAL-SUPERSEDED — insufficient, though not wrong.**

The A/B runs produced 4096/4096 non-black pixels, all `255,0,0,255`. That was
initially read as "the triangle renders". It is weaker evidence than it looks:
full coverage is also what a mis-scaled clear, a full-surface blit, or a
degenerate-but-enormous triangle would produce.

**Superseded by:** the controlled partial workload — 512/4096, corner clear,
centre red. See [graphics-progress.md](graphics-progress.md). The fullscreen runs
remain valid as the A/B *comparison* (both sides used the same geometry, so the
delta is still attributable to `fau_count`); they are just not sufficient as
proof of rasterization on their own.

Preserved fullscreen baselines:
[`tests/graphics/triangle_draw_test_v2.c.HISTORICAL-fullscreen-baseline`](../tests/graphics/triangle_draw_test_v2.c.HISTORICAL-fullscreen-baseline),
[`tests/shaders/triangle.vert.HISTORICAL-fullscreen-baseline`](../tests/shaders/triangle.vert.HISTORICAL-fullscreen-baseline).

---

## 4. "v9 just needs adding to the build matrix"

**Label: HISTORICAL-SUPERSEDED.**

The first hypothesis was that PanVK already supported v9 and only lacked a
`meson.build` entry. Adding `9` to the arch matrix does make `libpanvk_v9.a`
compile, but `jm/` was structurally Bifrost-only, so nothing worked at runtime.

**Superseded by:** [why-v9-is-a-port.md](why-v9-is-a-port.md). The build-matrix
patch ([`0001`](../patches/0001-panvk-add-v9-to-build-matrix.patch)) is still
required — it is necessary but nowhere near sufficient.

---

## 5. "`MEM_EXEC_INIT` va_pages is a per-call size"

**Label: HISTORICAL-SUPERSEDED.**

`va_pages = 0x100000` was rejected `EPERM`, so it was lowered to `4`, which made
`EXEC_INIT` succeed. But `va_pages` sizes the **entire EXEC_VA zone** for all
future executable allocations. 16 KB was exhausted by internal overhead before
the first real shader binary could be allocated, giving `ENOMEM` at a completely
unrelated call site.

**Superseded by:** `va_pages = 1024` (4 MB) —
[`patches/0003-kbase-exec-va-pages.patch`](../patches/0003-kbase-exec-va-pages.patch).

---

## 6. "All-zero `Draw.flags_0` is a neutral default"

**Label: HISTORICAL-SUPERSEDED.**

Leaving DCD Flags 0 zeroed looked like "don't opt into anything". The v9 Pixel
Kill enum is `Force Early = 0`, `Weak Early = 2`, `Force Late = 3`, so zero
actively requests `FORCE_EARLY` on both `pixel_kill_operation` and
`zs_update_operation`, with the forward-pixel-kill permissions cleared. That is
an aggressive kill policy encoded by accident.

**Superseded by:** `pan_earlyzs_get()`-driven flags in the v9 draw path, mirroring
`build_dcd_flags()` in `csf/panvk_vX_cmd_draw.c`.

---

## 7. "`stride = sizeof(atom)` for `JOB_SUBMIT`"

**Label: HISTORICAL-SUPERSEDED.**

`sizeof(struct base_jd_atom_v2)` is 56 and `JOB_SUBMIT` takes a `stride`, so
passing 56 is the natural reading. The kernel requires **64**. Both variants are
preserved side by side — see
[raw-jm-progress.md](raw-jm-progress.md#the-atom-size--stride-discrepancy--verified).

---

## 8. Superseded exploratory scripts

The one-shot Python patch scripts under [`tools/`](../tools/) were written to
mutate driver source in place during bring-up. They are kept for chronology and
are **not** a supported build path — the patches in
[`patches/`](../patches/) are. Several encode assumptions that later changed;
they should be read as a record of what was tried, in the order it was tried, not
as instructions.
