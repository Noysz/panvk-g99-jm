# patches/

Base commit for everything here: **`6598829019c0746aa8e473b4ae1c980cbfa6ea4b`**
(upstream Mesa), the commit the driver work tree sits on.

## Apply matrix — tested, not assumed

Each patch was checked with `git apply --check` against **pristine copies of the
23 target files extracted from the base commit**. This is the observed result,
not a guess:

| Patch | Applies to base? | Notes |
|---|---|---|
| `0001-panvk-add-v9-to-build-matrix` | ❌ no | HISTORICAL. Bare filename paths; authored against a Mesa revision whose `foreach` list contained arch 11. Superseded by `0014`. |
| `0002-panvk-physical-device-proto-v9` | ❌ no | HISTORICAL. Bare filename paths. Superseded by `0014`. |
| `0003-kbase-exec-va-pages` | ❌ no | HISTORICAL. Written against an intermediate `kbase_kmod.c`, so context does not match upstream. Superseded by `0013`. |
| `0004-panvk-v9-graphics-draw-path-WIP` | ✅ yes | HISTORICAL-SUPERSEDED by `0010`. Do not combine. |
| `0005-panvk-v9-kbase-jm-submit-and-debug` | ✅ yes | HISTORICAL-SUPERSEDED by `0012`. Do not combine. |
| `0010-panvk-v9-jm-graphics-draw-path` | ✅ yes | **current** |
| `0011-panvk-v9-jm-compute-dispatch` | ✅ yes | **current** |
| `0012-panvk-v9-jm-queue-submit-and-raw-capture` | ✅ yes | **current** |
| `0013-panfrost-lib-kbase-and-fb-fixes` | ✅ yes | **current** — contains the EXEC_VA fix |
| `0014-panvk-v9-arch-enablement-common` | ✅ yes | **current** — contains `jm_archs`, `foreach`, `PER_ARCH_FUNCS(9)` |
| `0015-termux-android-build-fixes` | ✅ yes | **current** |
| `0020-panvk-v9-fau-count-from-shader-metadata` | ❌ by design | Applies to **exactly one** tree state: the side-A source `panvk_vX_cmd_draw.c.ab_fau_off`, i.e. the post-`0010` draw path with these two lines removed. Verified to apply cleanly there. It does **not** apply to bare upstream (the code does not exist until `0010`) and does **not** apply to the current tree either (the change is already present, so the hunk context fails). Already contained in `0010`. |
| `0030-phase4-restab-instrumentation` | ✅ yes | **current** — observation only. Adds an env-gated `PANVK_DEBUG_RESTAB` dump of `used_set_mask`, `first_unused_set`, `res_count`, `entry_bytes`, and every resource-table entry. Emits nothing and changes no descriptor when the variable is unset, so the validated baseline is unaffected — confirmed by an unchanged 512/4096 pixel result. Applies cleanly to the pristine base file; does **not** apply to the active tree because it is already present there. |
| `9001-termux-android-detection-fixes.UPSTREAM-THIRDPARTY` | ✅ yes | third-party, from LukeValen/panvk-mali-g52 |

## The set that actually reproduces the current driver

Apply these six, in this order, to a clean checkout of the base commit:

```sh
git checkout 6598829019c0746aa8e473b4ae1c980cbfa6ea4b
git apply patches/0015-termux-android-build-fixes.patch
git apply patches/0014-panvk-v9-arch-enablement-common.patch
git apply patches/0013-panfrost-lib-kbase-and-fb-fixes.patch
git apply patches/0011-panvk-v9-jm-compute-dispatch.patch
git apply patches/0012-panvk-v9-jm-queue-submit-and-raw-capture.patch
git apply patches/0010-panvk-v9-jm-graphics-draw-path.patch
```

**Verified, not asserted.** This sequence was replayed against pristine
base-commit copies of all 23 target files. Every patch applied without conflict,
and the result was then compared file-by-file against the live driver work tree:

```
identical: 23   differing: 0   (of 23)
```

So these six patches reproduce the exact source that built the ICD which
produced the logs in [`../evidence/`](../evidence/).

Do **not** add `0004`, `0005`, `0001`, `0002`, `0003` or `0020` to that list.
`0004`/`0005` are earlier drafts of the same files as `0010`/`0012` and will
conflict or double-apply. `0001`/`0002`/`0003` do not apply at all.
`0020` is already inside `0010`.

Never use a `patches/*.patch` glob — it sweeps in the historical ones.

## Reproducing the FAU A/B experiment

`0020` exists so the causal change can be read as two lines instead of being
hunted for inside `0010`. To run the experiment, use the preserved source
variants rather than the patch — see
[`../docs/reproduction.md`](../docs/reproduction.md#5-reproduce-the-fau-ab-experiment).

## Caveats

These are working-tree extracts, not upstream submissions. They are not rebased,
not split per logical change, and their debug prints and comments are partly in
Indonesian. That is deliberate: the patches match the binary that produced the
logs in [`../evidence/`](../evidence/), and rewriting them would break that
correspondence.
