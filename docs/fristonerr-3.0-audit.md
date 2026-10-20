# FristOneRR 3.0 release references

Checked on 2026-10-20. These references update the rival inventory; they do
not establish a performance result or change the current task order.

| Item | Release/reference | Commit |
|---|---|---|
| Driver 3.0 | [Release](https://github.com/FristOneRR/FristOneRR-Panvk-Driver/releases/tag/v3.0) | `7a9c9a488b03363b40e8231dc142fefa389ebb4f` |
| Wrapper 3.0 | [Release](https://github.com/FristOneRR-Admin/FristOneRR-Wrapperv1/releases/tag/v3.0) | `2cc5c87d9e3891624cf99a0a2866f7d172eac983` |
| Public driver source checked | [Source commit](https://github.com/FristOneRR-Admin/FristOneRR-Panvk-Source/commit/efd07bab401b263a9ac114560ecdb9a2934f5d43) | `efd07bab401b263a9ac114560ecdb9a2934f5d43` |

The driver release was published at 01:26:28 UTC and the wrapper at 01:37:35
UTC on October 20. Their tag commits change README files only. The public
driver source reference checked is still the September 30 revision. The
wrapper repository contains older implementation source; a README-only
release commit does not identify the implementation used to build its new
binary. Neither tag alone proves the source revision behind the released
library.

Published ZIP asset SHA-256 digests:

```text
6c5f00cb39a7330e1840547a9b4dcb8d3bc9b6d939a2977592769c46908c0ac2  Panvk-Mali-G57-3.0.zip
6bbfe5b252df2ea038e7bb8b360b59b11107da3465ed1d951e97fba4449e9d41  FristOneRR-Wrapper-3.0.zip
```

The release notes claim D3D12 support through the paired driver/wrapper,
some rendering and crash fixes, and BC4-BC7 handling in the driver. They also
list loading hangs in some heavy D3D12 games. These are publisher statements;
no FR 3.0 game or CTS comparison is established by this inventory check.

Binary disassembly can identify instructions, symbols and control flow.
Decompiler output can help reconstruct behavior, but cannot recover the exact
original source or prove that a proposed backport is correct. Any resulting
implementation needs its own reproducer and validation on this device.
BC and wrapper changes remain deferred under the existing roadmap.
