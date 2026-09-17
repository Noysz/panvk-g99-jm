# Raw JM progress — direct `/dev/mali0` submission

These harnesses bypass Mesa entirely and talk to kbase through `ioctl()`. They
exist to answer "is the kernel/ABI doing what I think" without the driver in the
way. Sources: [`tests/raw-jm/`](../tests/raw-jm/).

## `WRITE_VALUE` atom executes — VERIFIED

Harness: [`tests/raw-jm/kbase_write_value_core_req_test.c`](../tests/raw-jm/kbase_write_value_core_req_test.c).
Committed artifact: [`evidence/logs/raw_jm_write_value_20260917-004008.log`](../evidence/logs/raw_jm_write_value_20260917-004008.log) — three
consecutive runs, with the source and rebuilt-binary SHA-256 recorded in the same
file. Output:

```
tgt_gpu=0x41000 cpu_va=0x77a971b000
WRITE_VALUE core_req=0x2 submitted, atom_size=56 stride=64
event=0x4, target=0x2a2a2a2a
```

`target=0x2a2a2a2a` is the value the GPU was asked to write, read back from the
target buffer after the wait. The atom really executed.

## The atom size / stride discrepancy — VERIFIED

This is the finding that cost the most time, so it is stated plainly:

```
sizeof(struct base_jd_atom_v2) == 56      /* enforced by _Static_assert */
kbase_ioctl_job_submit.stride  == 64      /* what the kernel requires */
```

The struct is 56 bytes. `JOB_SUBMIT` must nevertheless be given `stride = 64`.
Passing `stride = sizeof(atom)` — the obvious thing to write — does not behave.
Both variants are preserved so the difference is inspectable rather than
described:

| File | `stride` |
|---|---|
| [`kbase_write_value_core_req_test.c`](../tests/raw-jm/kbase_write_value_core_req_test.c) | 64 — current |
| [`kbase_write_value_core_req_test.c.stride64_working`](../tests/raw-jm/kbase_write_value_core_req_test.c.stride64_working) | 64 — known-good snapshot |
| [`kbase_write_value_core_req_test_stride56.c`](../tests/raw-jm/kbase_write_value_core_req_test_stride56.c) | 56 — the wrong one |
| [`kbase_write_value_core_req_test_stride56_debug.c`](../tests/raw-jm/kbase_write_value_core_req_test_stride56_debug.c) | 56 + extra tracing |
| [`kbase_write_value_core_req_test.c.HISTORICAL-before_atom56`](../tests/raw-jm/kbase_write_value_core_req_test.c.HISTORICAL-before_atom56) | before the 56-byte layout was pinned down |

Independent size checks: [`verify_atom_size.c`](../tests/raw-jm/verify_atom_size.c),
[`verify_atom_size_v2.c`](../tests/raw-jm/verify_atom_size_v2.c).

## Event codes {#event-codes}

**`event_code = 0x4` is `BASE_JD_EVENT_TERMINATED`. It is not `DONE`.**

Verified against the r54p1 UAPI header `jm/mali_base_jm_kernel.h`, and encoded in
the driver's own decoder (`src/panfrost/vulkan/jm/panvk_vX_gpu_queue.c`):

```
BASE_JD_EVENT_NOT_STARTED              = 0x00
BASE_JD_EVENT_DONE                     = 0x01   <- success
BASE_JD_EVENT_STOPPED                  = 0x03
BASE_JD_EVENT_TERMINATED               = 0x04
BASE_JD_EVENT_ACTIVE                   = 0x08
RANGE_HW_FAULT_OR_SW_ERROR_START       = 0x40
```

Two consequences, both learned the hard way:

* `0x01` is **success**. Do not treat "non-zero" as an error — an earlier version
  of the driver's job-status print did exactly that and sent the investigation
  down a dead end. Only `>= 0x40` is an actual fault.
* `0x4` on the `WRITE_VALUE` atom means the atom was **terminated**, even though
  its side effect (the write) is visibly present in memory. So "the value landed"
  and "the atom reported DONE" are separate questions, and only the first is
  true here. Why a completed write reports TERMINATED is **NOT RESOLVED**.

## Other raw harnesses

| Harness | Purpose | State |
|---|---|---|
| [`kbase_handshake.c`](../tests/raw-jm/kbase_handshake.c) | `VERSION_CHECK` + `SET_FLAGS` minimum viable open | VERIFIED |
| [`kbase_gpuprops_test.c`](../tests/raw-jm/kbase_gpuprops_test.c) | `GET_GPUPROPS`, incl. no-context path | VERIFIED |
| [`kbase_submit_test.c`](../tests/raw-jm/kbase_submit_test.c) | generic atom submission | VERIFIED |
| [`kbase_fau_self.c`](../tests/raw-jm/kbase_fau_self.c) | FAU behaviour probe outside Mesa | IN PROGRESS |
| [`test_kbase.c`](../tests/raw-jm/test_kbase.c), [`test_kbase2.c`](../tests/raw-jm/test_kbase2.c) | early ioctl / mmap / coherency probes | VERIFIED |
| [`../test_kbase3.c`](../tests/raw-jm/test_kbase3.c) | full gpuprops decode, 83 props | VERIFIED |

## Confirmed working ioctl chain

```
open("/dev/mali0")            -> ok
KBASE_IOCTL_VERSION_CHECK     -> UAPI 11.46
KBASE_IOCTL_SET_FLAGS         -> context active
KBASE_IOCTL_MEM_ALLOC         -> gpu_va = 0x41000
mmap()                        -> ok
write 0xAB x 16384, read back -> matches   (CPU<->GPU coherency)
KBASE_IOCTL_JOB_SUBMIT        -> ok with stride=64
poll() + read() base_jd_event_v2 -> event delivered
```

The kernel side is not a blocker and has not been one since the first probe.
