# FourFectVK

Mesa PanVK driver work for **Mali-G57 MC2 / Helio G99**, using the Valhall v9 Job Manager backend. This repository contains the patch stack, tests, results and Winlator test builds.

Test device: Infinix Note 40 4G (X6853), kbase r54p1, Android 16. Experimental; not Vulkan-conformant.

## Test9

[Download driver ZIP](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/FourFectVK-G57-v0.1.0-test9.zip) | [Testing README](docs/test9/README-v0.1.0-test9.txt) | [Changelog and checksums](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)

Import the ZIP through Winlator's custom-driver installer and select it as the Wrapper driver. Follow R0-R3 in the testing README with any game.

Test9 includes timestamp lifetime, vertex-input/GPL, conditional-rendering and cache/coherency fixes, plus optional depth bounds. UBO push defaults to 32 words. Pass overlap and incremental tiler backing remain off.

[Driver environment variables](docs/driver-environment.md) | [All test builds](docs/test-builds.md) | [Report a test](https://github.com/Noysz/panvk-g99-jm/issues/new?template=test9-report.yml)

## Roadmap Progress

Updated October 28, 2026. Native compute, offscreen graphics, X11 presentation and DXVK bring-up have passed their recorded tests. The [phase 8-13 roadmap](docs/roadmap-phase8-13.md) and [release roadmap](docs/release-roadmap.md) define the remaining work.

| Task | Work | Status | Remaining |
|---|---|---|---|
| T01 | Preserve source/builds; verify patch replay | Complete | Broader clean-build reproducibility belongs to T10. |
| T02 | Shader-cache keys and compile controls | Targeted checks complete | Gameplay comparisons remain in T04. |
| T03 | Compiler/cache backports | Targeted checks complete | No general gameplay gain established. |
| T04 | Gameplay baselines and clip/FPK comparisons | Partial | Three comparable A/B pairs and wider game coverage. |
| T05 | Deferred occlusion queries | Implementation checked; default off | Repeatable performance benefit. |
| T06 | Memory/import coherency and tiler backing | Partial | Sustained graphics/game runs; unresolved long-run timeout. |
| T07 | Matrix vectorization and register pressure | Partial | Register-pressure work and representative games. |
| T08 | Overlap between passes | Prototype; default off | Resource lifetime during GPU faults. |
| T09 | Timestamps, robustness, conditional rendering, depth bounds | Targeted implementation/tests complete | Broader compatibility; depth bounds stays opt-in. |
| T10 | Combined build and release qualification | Test9 published; RC open | Existing CTS failures, clean-build and release gates. |

[Task details and evidence](docs/followup-tasks-2026-10-19.md)

Still open in the broader roadmap: native Android surfaces outside Winlator, sustained game memory use, wider device coverage and D3D12 bring-up after memory/performance work. RC requires three zero-failure tracked regressions, the 30-minute game matrix and a two-week regression observation period.

## Validation

The final host and Android builds each run **5,658 tracked CTS cases: 3,754 Pass, 1,890 NotSupported, 14 Fail**. One case gains a Pass against the previous driver; no Pass is lost. The remaining failures concern TES/transform feedback.

Package loading and render checks use the reconstructed Wine/Termux:X11 stack. Actual Winlator installations and other devices need tester reports. No repeatable gameplay FPS gain is established.

[Combined results](evidence/validation/api-2026-10-26/README.md) | [Package checks](evidence/validation/test9-package-2026-10-28/README.md)

## Development

| Location | Contents |
|---|---|
| [Documentation](docs/README.md) | Build instructions, roadmaps, hardware notes and results. |
| [Test9 build order](docs/test9/build-order.md) | Source baseline, patch order and package checks. |
| [patches-upstream/](patches-upstream/) | Upstream rebase and base patches. |
| [patches-candidates/](patches-candidates/) | Compiler, cache, memory and API changes used by test9. |
| [tests/](tests/) / [tools/](tools/) | Test programs and verification tools. |
| [evidence/](evidence/) | Logs, captures, manifests and validation results. |

Thanks to the Mesa/Panfrost/PanVK contributors and hafiz for the upstream rebase and kbase submission work. [Earlier bring-up credits and notes](https://github.com/Noysz/panvk-g99-jm/blob/9c7820b52d452db5f28f4988aa4d7622b4c88575/README.md#credits--prior-art).
