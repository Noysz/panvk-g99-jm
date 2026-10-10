# patches-upstream: FourFectVK on upstream Mesa (since v0.1.0-test6)

From test6 on, FourFectVK is built on **upstream Mesa `feeadb5f`**
(26.3.0-devel, 2026-10-05) instead of the funnymdzz fork base that
[`../patches/`](../patches/) (`0001`-`0077`) applies to. The old series is
kept for history and for the test1-test5 builds.

| patch | what |
|---|---|
| [`0001-base-upstream-rebase-52a7497a.patch`](0001-base-upstream-rebase-52a7497a.patch) | **hafiz's rebase** (tree `52a7497a`): the funnymdzz kbase `pan_kmod` backend, panvk-g99-jm `0015`-`0059` 3-way merged onto upstream, and his phases 8.5-8.16: Android native surface and AFBC swapchains through the vendor IMapper, one kbase `JOB_SUBMIT` per `vkQueueSubmit` (8.9), forward pixel kill on v9 (8.10), read-only input attachment tile loads (8.11), multisampled storage images (8.13/8.14), asynchronous submission with kernel sync_file fences (8.15), linear Android swapchain buffers by default (8.16). Conflict notes: [`REBASE-RESOLUTIONS.md`](REBASE-RESOLUTIONS.md). |
| [`0002-fourfectvk-0060-0077-frame-prof.patch`](0002-fourfectvk-0060-0077-frame-prof.patch) | FourFectVK `0060`-`0077` ported onto that base (tessellation, geometry shaders, transform feedback, robustBufferAccess2, lazy device memory, pool cache cap, shared scratch, null texture slot, fault report and audit), two fixes the new base needed, and the `PANVK_FRAME_PROF` logger (test7). How each conflict was resolved: [`MERGE-0060-0077.md`](MERGE-0060-0077.md). |
| [`0003-fourfectvk-perf-knobs-2026-10-10.patch`](0003-fourfectvk-perf-knobs-2026-10-10.patch) | Tomb Raider performance work, everything off by default: `PANVK_FRAME_PROF_PASSES` (per-pass groups in the frame profiler), `PAN_LATE_VECTORIZE` (second UBO load vectoriser run), `PANVK_TILER_HMASK`, `PANVK_V9_NO_TILER_CHAIN`, `PAN_NIR_DUMP_UBO` (debug dump). None changed the Tomb Raider frame rate; results in [`../evidence/perf/tr-menu-2026-10-10/`](../evidence/perf/tr-menu-2026-10-10/). |

## Apply

```
git clone https://gitlab.freedesktop.org/mesa/mesa.git && cd mesa
git checkout feeadb5f55f4ba14a5c5ddc84ba20177e419cffb
git apply ../patches-upstream/0001-base-upstream-rebase-52a7497a.patch
git apply ../patches-upstream/0002-fourfectvk-0060-0077-frame-prof.patch
git apply ../patches-upstream/0003-fourfectvk-perf-knobs-2026-10-10.patch
```

Checked: applied to `feeadb5f`, the result is identical to the tree that was
built and tested (0001+0002: 125 files; with 0003: 129 files, 2026-10-10).

## Build (Termux on the phone)

Host build first (it also provides the host tools for the Android build):

```
meson setup buildU -Dbuildtype=debugoptimized -Dvulkan-drivers=panfrost \
  -Dgallium-drivers= -Dplatforms= -Dbuild-tests=false \
  -Dpanfrost-kmds=kbase,panthor -Db_ndebug=true -Dglx=disabled \
  -Dgles2=disabled -Dopengl=false -Degl=disabled -Dgbm=disabled
ninja -C buildU src/panfrost/vulkan/libvulkan_panfrost.so
```

Android build for Winlator (put `mesa_clc`, `panfrost_compile` and
`vtn_bindgen2` from `buildU` first in `PATH`):

```
meson setup build-android -Dbuildtype=release -Dstrip=true -Db_ndebug=true \
  -Dplatforms=android -Dandroid-stub=true -Dplatform-sdk-version=29 \
  -Dandroid-libbacktrace=disabled -Dvulkan-drivers=panfrost \
  -Dgallium-drivers= -Dpanfrost-kmds=kbase,panthor -Dopengl=false \
  -Degl=disabled -Dgles1=disabled -Dgles2=disabled -Dgbm=disabled \
  -Dglx=disabled -Dzstd=disabled -Dxmlconfig=disabled -Dzlib=enabled \
  -Dlibunwind=disabled -Dvideo-codecs= -Dtools= -Dvulkan-layers= \
  -Dmesa-clc=system -Dprecomp-compiler=system \
  -Dc_args=--target=aarch64-linux-android29 \
  -Dcpp_args=--target=aarch64-linux-android29 \
  -Dc_link_args=--target=aarch64-linux-android29 \
  -Dcpp_link_args=--target=aarch64-linux-android29 -Dandroid-strict=false
ninja -C build-android src/panfrost/vulkan/libvulkan_panfrost.so
```

`b_ndebug=true` matters: with assertions on, Tomb Raider aborts at its
first `vkDestroyDevice` (`vk_pipeline_cache_destroy`, see the merge notes).

## Verification

[`../evidence/cts/phase14/`](../evidence/cts/phase14/): CTS regression
7127 cases, 0 lost against test5 and test6; Winlator-stack device check;
AIO-Graphics-Test A/B test5 vs test6; `PANVK_FRAME_PROF` measurements.
