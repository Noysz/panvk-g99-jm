# JM v9 correctness candidate

Apply after the test9 stack (`docs/test9/build-order.md`, groups 1-5), then
this directory's `series`. Plain diffs; use `git apply`. These reproduce the
tested development build. They do not publish a test build or qualify an RC.

| Patch | Change |
|---|---|
| 0001 | Thread-safe one-time initialisation of the JM/v9 debug and option switches |
| 0002 | `os_get_option_cached`: handle key/value allocation failures without crashing or leaking |
| 0003 | Indirect dispatch: keep the parked job header fields when the helper promotes it (device lost on reissue) |
| 0004 | Compute dispatch: queue each job once in the batch job list (direct 2->1, indirect 3->2 entries) |
| 0005 | Transform feedback from a tessellation evaluation shader without a geometry shader |

No default changes. No performance change is claimed; scheduler, descriptor
reuse, pool-flush and other T04 experiments are not part of this group.

## Validation

Source: feeadb5 + test9 stack + this series gives tree `ea696dfb5359`, the
source of the tested libraries (host `726f4490`, Android `b6f8bfcc`).

- Tracked 5,658 CTS cases, host and Android: 3,768 Pass, 1,890 NotSupported,
  0 Fail. Against test9 (3,754 / 1,890 / 14): the 14 TES transform-feedback
  failures pass, no Pass lost, no other change.
- TES transform feedback, 30 cases (14 tracked + 16 GPL variants): Pass on
  host and Android; with `PANVK_V9_XFB_SKIP=1` all 30 fail again.
- Compute indirect dispatch, 44 cases: 32 Pass, 12 NotSupported, unchanged.
- Per-fix checks for 0001-0004 were run on their own builds.

See the [validation evidence](../../evidence/validation/correctness-2026-11-01/README.md).

Not covered: XFB buffer overflow clamp, isolines/point-mode capture, several
tessellated draws in one XFB session, counter resume across command buffers,
full Vulkan CTS, Winlator runtime and games.
