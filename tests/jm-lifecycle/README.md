# CPU lifecycle regression fixtures

These are the exact C fixtures used for the
[November lifecycle candidate](../../patches-candidates/2026-11-13-jm-lifecycle/README.md).
They extract production queue, sync, owner, profiler and teardown functions,
with kernel/resource operations replaced by test stubs. They do not submit
GPU work or prove physical backing retirement.

The runner checks fixture hashes and nine relevant production source hashes
before compiling. It refuses changed source: regenerate and review the
fixtures before using them to qualify another implementation.

```sh
python3 tests/jm-lifecycle/run.py --mesa "$MESA_SOURCE" \
  --output "$TEST_OUTPUT" --cc cc --ubsan
```

Use a new output directory. For native Android, use the Termux compiler with
`--cflag=--target=aarch64-linux-android29` and omit `--ubsan`.
The fixtures intentionally support POSIX/Linux and native Android.

- Owner admission, callback/reader shutdown and sync payload references.
- Grouped/partial submission, read EFAULT at 0-32 events and delayed events.
- Latched failure cannot produce success retirement or original-ID reuse.
- Profiler teardown order and queue initialization failure cleanup.
- Fault metadata remains readable after the original object becomes inaccessible.

The two before-source controls must fail their specific assertions. A crash
without the expected assertion is not accepted. Core dumps are disabled.
The lower GPU/backing layers remain mocked; do not interpret a passing test
as a P0 fix. The extracted Mesa implementation retains its upstream licensing;
no third-party contributor attribution is replaced by these fixture names.
Original copyright and license notices are retained in [NOTICE](NOTICE).
