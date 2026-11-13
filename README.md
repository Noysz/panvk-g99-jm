# FourFectVK

**Mesa PanVK for Mali-G57 MC2 / Helio G99**

Vulkan driver development for the Valhall v9 Job Manager backend on stock Mali kbase.

[Download Test9](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9) | [Documentation](docs/README.md) | [Roadmap](docs/followup-tasks-2026-10-19.md) | [Report an issue](https://github.com/Noysz/panvk-g99-jm/issues)

> **Experimental.** Tested on Infinix Note 40 4G (X6853), Android 16, Mali-G57 MC2.
> Not Vulkan-conformant. Test9 is the latest public build; November candidates are unreleased.

## Getting started

1. Download the [Test9 driver ZIP](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/FourFectVK-G57-v0.1.0-test9.zip) and check its [release checksums](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9).
2. Import it through Winlator's custom-driver installer and select it as the Wrapper driver.
3. Start with defaults, then follow the bounded sessions in the [testing guide](docs/test9/README-v0.1.0-test9.txt).

[Environment variables](docs/driver-environment.md) | [All public builds](docs/test-builds.md) | [Test report form](https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml)

## Features and compatibility

- Native compute, offscreen graphics, X11 presentation and DXVK bring-up have recorded tests.
- Test9 includes timestamp lifetime, vertex-input/GPL, conditional rendering and cache/coherency fixes.
- Geometry, tessellation and transform-feedback paths include experimental v9 emulation; depth bounds is opt-in.
- Pass overlap and incremental tiler backing remain off. Other devices and native Android surfaces need further validation.

See the [hardware/runtime notes](docs/hardware-runtime.md) and [feature roadmap](docs/roadmap-phase8-13.md) for the tested scope. A feature exposed by the driver is not a promise that every application works.

## Validation

**Public Test9:** each Host and Android build ran 5,658 tracked CTS cases:
**3,754 Pass | 1,890 NotSupported | 14 Fail**. One Pass was gained against the previous driver; none was lost. The remaining failures concern TES/transform feedback.

[CTS evidence](evidence/validation/api-2026-10-26/README.md) | [Package/loading checks](evidence/validation/test9-package-2026-10-28/README.md)

**November development:** the early correctness series fixes those 14 failures on its own recorded binaries. The later combined lifecycle candidate has separate [build, CTS, memory and performance evidence](evidence/validation/jm-lifecycle-2026-11-13/README.md), including two inherited query failures. Neither result changes the public Test9 snapshot.

Package checks used the recorded Wine/Termux:X11 stack. Wider Winlator installation coverage, sustained game stability and release qualification remain open. No general gameplay FPS improvement is claimed.

## Development progress

Current work prioritizes CPU callback/reader lifetime, fault accounting, memory correctness and reproducible candidate stacks. Previously validated scheduling and state-reuse work is preserved with its dependencies.

GPU resource lifetime recovery (**P0**) remains **BLOCKED**. Geometry/fragment overlap (**0012**) remains **REWORK / default OFF**. **Test10 is PRIVATE / NO-GO** until its release gates are met.

[November decisions](docs/november-development.md) | [Source changelog](docs/november-changelog.md) | [Full T01-T10 ledger](docs/followup-tasks-2026-10-19.md) | [Release criteria](docs/release-roadmap.md)

## Development resources

| Resource | Start here |
|---|---|
| Patch stack and build | [Test9 order](docs/test9/build-order.md) / [November candidate](docs/november-build.md) |
| Candidate source | [Correctness](patches-candidates/2026-11-01-correctness/README.md) / [Stock JM](patches-candidates/2026-11-13-stock-jm/README.md) / [Lifecycle](patches-candidates/2026-11-13-jm-lifecycle/README.md) |
| Tests and tools | [Verification tools](tools/README.md) / [CPU lifecycle regression](tests/jm-lifecycle/README.md) |
| Engineering evidence | [Validation records](evidence/validation/) / [Documentation index](docs/README.md) |

Repository layout: `patches-upstream/` provides the upstream base;
`patches-candidates/` holds ordered development series; `tests/` and `tools/`
contain validation programs; `evidence/` preserves selected results.
The older `patches/` series targets a different base and is not an extra layer.

## Contributing and reporting

Use the [report template](https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml). Include device/firmware, driver version or SHA-256, Winlator/Wine/DXVK/Wrapper versions, environment overrides, reproduction steps, logs and a rendering capture.

For benchmarks, include resolution, scene/save, duration, cache state, frame times, power/thermal conditions and all comparable runs. Report missing objects or artifacts alongside FPS. Remove personal data and credentials from attachments.

Thanks to the Mesa/Panfrost/PanVK contributors and hafiz for the upstream rebase and kbase submission work. Upstream copyright, licenses and human contributor attribution remain with their respective patches. [Earlier bring-up credits](https://github.com/Noysz/panvk-g99-jm/blob/9c7820b52d452db5f28f4988aa4d7622b4c88575/README.md#credits--prior-art).
