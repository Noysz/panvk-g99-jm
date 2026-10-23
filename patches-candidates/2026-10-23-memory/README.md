# JM memory candidate

Apply `0001-jm-import-coherency-incremental-tiler.patch` after the upstream
stack, the compiler/cache candidate, and the deferred-query candidate, in
that order. `series` lists this directory's patch. The source diff is identical
to the frozen T06 v2 build; it contains no later queue-error changes.

The patch fixes cached dma-buf allocation/import properties on kbase JM v9.
It distinguishes the known system exporters, rejects incompatible Vulkan
memory types, and flushes queued CPU cache operations at the explicit flush
API boundary. Unknown exporters do not acquire a host-visible mapping based
on an assumption about their cache policy.

`PANVK_TILER_HEAP_INCREMENTAL=true` also requests a 2 MiB initial tiler backing
and limits the initial descriptor top to that backing. The option defaults
to false. Unsupported allocation flags fall back on `EINVAL`; other allocation
errors remain errors. This does not implement pass overlap.

Six 929-case CTS runs, Android visibility/render controls and device lifecycle
checks pass their recorded expectations. Two existing CTS failures remain.
The tested tiler workloads use the same final backing with the option on and
off, so there is no demonstrated memory saving. A long cached-import run on
this frozen build ended with a fence timeout; its cause remains unresolved.

This is a development candidate. Cache-sync error reporting, sustained
graphics and game validation remain open. See the [evidence](../../evidence/validation/memory-2026-10-23/README.md)
and [task ledger](../../docs/followup-tasks-2026-10-19.md).
