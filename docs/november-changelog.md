# November source changelog

## 1-13 November 2026 — candidate source, unreleased

- Version the five early correctness fixes: one-time options, option-cache
  allocation failures, indirect dispatch reissue, compute job accounting and
  tessellation transform feedback.
- Preserve the ordered stock JM development prerequisites, including memory,
  viewport/null-index/query correctness and previously tested scheduling/state
  preparation changes.
- Add CPU owner/admission/reader shutdown, profiler teardown ordering,
  failed-event accounting and scalar fault snapshots as a separate candidate.
- Add pinned CPU regression fixtures, complete build identities, targeted CTS,
  lifecycle/memory observations and all lifecycle comparison FPS observations.
- Separate current development decisions from public validation, with a
  focused README and links to the unchanged full roadmap.

These are local source commits and evidence, not a new binary release.
The [November status](november-development.md) records deferred and rejected
work. P0 remains BLOCKED; 0012 remains REWORK/default OFF; Test10 is PRIVATE.

## Latest public build

[Test9, 28 October 2026](https://github.com/Noysz/panvk-g99-jm/releases/tag/fourfectvk-g57-v0.1.0-test9)
remains the download target. Its validation and limitations have not been
replaced by results from November development binaries.
