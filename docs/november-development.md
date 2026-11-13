# November development status

Snapshot: 13 November 2026. Latest public driver: **Test9**.
This update versions candidate patches and selected evidence; it does not
publish Test10 or change the public driver defaults.

Implementation, testing, source commits and release are separate milestones.
The patch series listed here are committed as development source. Only Test9
has a public driver package. The final lifecycle composition has fresh full
builds and targeted qualification; broader release gates remain open.

## Candidate decisions

| Work | Implementation / decision | Validation and dependency |
|---|---|---|
| Early November 0001-0005 | DONE at targeted scope; KEEP candidate | Atomic options, allocation failure, indirect reissue, compute accounting, TES/XFB. Test9 prerequisite; historical 5,658-case Host/Android runs: 3,768 P / 1,890 NS / 0 F |
| CPU admission/reader/callback lifetime | DONE at targeted scope; KEEP candidate | Queue owner and sync payload lifetime; pinned stock JM baseline |
| Profiler teardown | DONE at targeted scope; KEEP candidate | Requires CPU quiescence from the preceding lifecycle patch |
| Failed-event accounting | DONE at targeted scope; KEEP candidate | Latched loss cannot retire success or recycle original IDs; no ledger prerequisite |
| Fault metadata snapshot | DONE at targeted scope; KEEP candidate | Removes dead-object callback reads; live job-status diagnostics are deliberately unavailable |

The four lifecycle patches are validated together, not inferred from
standalone textual application to Test9.
[Build/replay procedure](november-build.md) and
[validation](../evidence/validation/jm-lifecycle-2026-11-13/README.md) define the
exact scope. No ledger, capture, lease or quarantine activation is required.

## Numbered development work

These IDs belong to the private development sequence; they are not a claim
that every number is implemented or ready for release.

| ID | Work | Decision and remaining gate |
|---|---|---|
| 0001-0004 | Heap allocation error, batch budget, trace guard/debug flag cache | KEEP candidate; approved private defaults preserved |
| 0005-0008 | Viewport clipping, budget default, upstream spill flags, logic-op destination reads | KEEP candidate; upstream attribution preserved |
| 0009 | Reduce redundant tiler-to-tiler dependency | KEEP candidate; ordering/CTS and historical comparative runs recorded |
| 0010 | Descriptor/blend reuse | KEEP candidate; state invalidation and reset behavior remain part of the stack |
| 0011 | Draw/depth-stencil template reuse | CONDITIONAL KEEP; depends on 0010; combined release qualification pending |
| 0012 v4 | Geometry/fragment overlap, including FS-write to UBO-read correction | EXPERIMENTAL / DEFAULT OFF; REWORK and P0 BLOCKED; excluded from candidate series |
| 0013 | Null index buffer | KEEP candidate; targeted null-descriptor failures corrected |
| 0014 | Sysval preparation experiment | REJECTED; not in selected source |
| 0015 | 256 MiB tiler VA, 128 MiB planning budget | KEEP candidate; does not prove safe backing-OOM recovery |
| 0016 | WSI extension dependencies and process-wide memory budget | KEEP candidate; correctness checks recorded |
| 0017 | LOW global priority acceptance | KEEP candidate; device initialization checks recorded |
| 0018 | Placed mapping, host memory and legacy ION variants | PARTIAL / PENDING VALIDATION; separate experiments, excluded from stock candidate |
| 0019 | Timestamp pools implemented even when advertisement is disabled | KEEP candidate; query results remain distinct from advertised feature controls |
| 0020 | Precompiled-helper allocation failure propagation | KEEP candidate; converts tested NULL-write paths to recording errors |
| 0021 | Idle cached-BO reclamation | BLOCKED / paused design; no selected runtime patch |

The [17-patch prerequisite series](../patches-candidates/2026-11-13-stock-jm/README.md)
preserves existing implementation and ordering. No new optimization or
unqualified default change is added by this repository update.

## Preserved outside the selected stack

- P0 ledger, original-generation tracking, physical pins, capture adapter and
  allocator quarantine: EXPERIMENTAL / DEFAULT OFF where applicable. Component
  validation does not prove complete GPU ownership or terminal recovery.
- Normal CPU teardown fixes: selected through the four lifecycle patches.
  Broader physical teardown and failure-path recovery remain BLOCKED.
- Concurrent-query/FS-counter, BeginQuery and AHB R8/R16/RGB888 corrections:
  KEEP LOCAL at their recorded scope; PENDING VALIDATION on this composition.
  The two inherited concurrent-query CTS failures remain visible.
- MVJ selective zeroing: REJECTED after regression. MVJ staging and FAU word
  iteration: REJECTED for lack of consistent improvement. Follow-up Draws
  profiling did not produce a qualified performance patch.
- Fullscreen preload skipping and other failed experiments remain excluded.
  Original source, patches, logs and negative results are retained privately.
- DriverLab routing, negative controls, render validation and benchmark
  records remain engineering tools/evidence. No game-specific workaround,
  clock override or kernel change is included.

Historical competitor measurements are not advertised as product performance.
Different source/binary versions and configurations must not be combined.
The selected lifecycle comparison uses only the pinned FourFectVK baseline
and candidate; all observations and method limits are linked in its evidence.

## Roadmap alignment and release gates

T01-T03 retain their recorded reconstruction/cache/compiler milestones.
T04 performance and gameplay coverage remain PARTIAL; no new FPS gain is
claimed. T06 memory and T08 overlap remain BLOCKED on unresolved failure
ownership/recovery. T07 compiler work is PARTIAL. T09 gains scoped correctness
candidates. T10 remains PRIVATE / NO-GO.

Stock kbase has no verified supported bounded terminal mechanism that covers
the required failure paths while keeping backing valid. Timeout, DEVICE_LOST,
POST_TERM, fd close and MMU flush are not terminal acknowledgments.
CPU metadata safety can improve independently; bounded physical cleanup and
all-path GPU resource lifetime are not established by normal CTS passes.

Before a driver release: complete the selected final-stack tracked regression,
remaining memory/compatibility and sustained game gates, resolve applicable
P0 requirements, validate packaging and obtain publication approval.
0012 additionally needs its lifetime and overlap qualification. The official
[phase roadmap](roadmap-phase8-13.md) and [release criteria](release-roadmap.md)
are unchanged.
