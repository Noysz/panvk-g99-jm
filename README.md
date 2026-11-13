<p align="center">
  <img src="docs/assets/fourfectvk-immersive-banner.png" alt="FourFectVK — Vulkan for Mali-G57, an experimental graphics driver built on Mesa PanVK" width="100%">
</p>

<p align="center"><strong>Experimental Vulkan graphics for Mali-G57 GPUs on Android.</strong></p>
<p align="center">
  <a href="https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9"><strong>Download Test 9</strong></a>
  &nbsp;·&nbsp; <a href="#getting-started">Get started</a>
  &nbsp;·&nbsp; <a href="docs/README.md">Documentation</a>
  &nbsp;·&nbsp; <a href="#for-developers">Build from source</a>
  &nbsp;·&nbsp; <a href="https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml">Report an issue</a>
</p>

> **Current status:** Test 9 is the latest public driver. Newer November source changes are available to review, but have **not** been released as a new driver. FourFectVK is experimental and **not Vulkan-conformant**.

## About FourFectVK

FourFectVK is an open-source driver project built on [Mesa PanVK](https://docs.mesa3d.org/drivers/panfrost.html), focused on the **Mali-G57 MC2** GPU used in the **MediaTek Helio G99**. The goal is better graphics compatibility, stability and performance for Vulkan applications and Windows games running through tools such as Winlator and DXVK.

Development and testing primarily use the **Infinix Note 40 4G (X6853), Android 16**. Support for other devices is not yet established. This is **not** a drop-in replacement for every Android graphics driver.

## Getting started

1. **Download** the [Test 9 driver ZIP](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/FourFectVK-G57-v0.1.0-test9.zip) and check the [release notes and checksums](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9).
2. **Install** it through Winlator's custom-driver installer. Select the driver as the **Wrapper**, then restart the container.
3. **Try a game** with the default settings first. Follow the [testing guide](docs/test9/README-v0.1.0-test9.txt) if you want to report results.

[Driver settings](docs/driver-environment.md) · [Installation checks](docs/test9-readiness.md) · [Previous builds](docs/test-builds.md)

## Features and compatibility

- **Vulkan graphics and compute:** tested rendering, compute and X11 presentation paths.
- **Windows games through Vulkan:** recorded bring-up and compatibility tests with Wine and DXVK; results vary by game.
- **Graphics fixes:** Test 9 includes improvements for graphics pipelines, vertex inputs, timestamp queries and conditional rendering.
- **Memory handling:** cache and memory fixes are included, while longer-term stability improvements are still in progress.

Some advanced graphics features use experimental implementations. They may not work in every application. See [hardware and runtime support](docs/hardware-runtime.md) for details.

## Testing and known limitations

The **public Test 9 build** has the following recorded Vulkan test results, **per Host and Android build**:

| Passed | Not supported | Failed |
| ---: | ---: | ---: |
| **3,754** | **1,890** | **14** |

These are results from **5,658 tracked tests** per build, **not a Vulkan certification**. The known failures involve tessellation and transform feedback. [Full test record](evidence/validation/api-2026-10-26/README.md).

November development builds include fixes and additional stability tests, but those changes are **not part of the Test 9 download**. [November validation](evidence/validation/jm-lifecycle-2026-11-13/README.md).

**Still in progress:** wider phone compatibility, long-running game stability and GPU memory recovery after errors. Experimental overlapping graphics work remains **disabled** until its safety requirements are met. [Current development status](docs/november-development.md).

## What's being improved

| Focus | What it means |
| --- | --- |
| **Game compatibility** | Fix incorrect rendering, missing objects and unsupported game features. |
| **Performance** | Reduce overhead where testing shows a real benefit, without sacrificing correct graphics. |
| **Stability** | Improve memory management, error handling and longer game sessions. |
| **Testing** | Keep changes reproducible and track regressions before publishing new builds. |

[Development updates](docs/november-changelog.md) · [Roadmap](docs/followup-tasks-2026-10-19.md) · [Release requirements](docs/release-roadmap.md)

## For developers

The repository contains **source patches, build tools and test evidence**, rather than a complete Mesa source checkout. November's source changes are available on **main**, with a documented preparation tool for reconstructing the matching Mesa source.

```bash
git clone https://github.com/Noysz/panvk-g99-jm.git
cd panvk-g99-jm
```

[Build from source](docs/november-build.md) · [Ordered source changes](docs/november-lifecycle.series) · [Developer tools](tools/README.md) · [Validation evidence](evidence/validation/jm-lifecycle-2026-11-13/README.md)

### Reports and contributions

When reporting a bug, include your device, Android version, driver build, Winlator / Wine / DXVK versions, reproduction steps and a screenshot or log when possible. For performance reports, include resolution, test scene, duration and frame-time measurements, not just a peak FPS number.

[Open an issue or submit a test report](https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml).

---

<sub>FourFectVK builds on the work of the Mesa, Panfrost and PanVK communities, with thanks to hafiz for upstream rebase and Mali kbase submission contributions. Copyright, licenses and author attribution remain with their respective source patches.</sub>
