# FourFectVK Test Builds

For Mali-G57 MC2 / Helio G99 using Winlator's Wrapper driver. Each build has a driver ZIP and its testing README. Builds remain experimental prereleases.

## Test9

[Driver ZIP](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/FourFectVK-G57-v0.1.0-test9.zip) | [Testing README](test9/README-v0.1.0-test9.txt) | [Checksums](https://github.com/Noysz/panvk-g99-jm/releases/download/fourfectvk-g57-v0.1.0-test9/SHA256SUMS.txt) | [Full changelog](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)

Timestamp lifetime, vertex-input/GPL, conditional-rendering and cache/coherency fixes; optional depth bounds. The guide has four sessions with any game. [Environment reference](driver-environment.md).

ZIP SHA-256: `492816fb6872c1ce6916017d9c2793c8bfcc67d71cb23926b2813ad40dd81755`

## Test1-Test8

[Download builds and their READMEs](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-tests) | [Detailed results and switches](test-builds-1-8.md)

| Build | Main change |
|---|---|
| test8 | Shader executable VA, timestamps and UBO push. |
| test7 | Frame/GPU load profiling. |
| test6 | Upstream rebase, chained asynchronous submission and forward pixel kill. |
| test5 | Failed-job pointer audit. |
| test4 | Empty-texture fix and GPU-fault diagnostics. |
| test3 | Reuse command-buffer scratch memory. |
| test2 | Robust buffer access and command-buffer fixes. |
| test1 | Lazy memory allocation and memory profiling. |

## Building And Results

[Source and build order](test9/build-order.md) | [Combined validation](../evidence/validation/api-2026-10-26/README.md) | [Package checks](../evidence/validation/test9-package-2026-10-28/README.md)

Test9 retains 14 tracked CTS failures. It is not an RC, and no repeatable gameplay speedup is established.
