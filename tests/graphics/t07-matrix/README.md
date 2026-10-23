# Dynamic UBO matrix probes

`correctness/` contains the small row/column matrix oracle. `timing/` uses a
larger dispatch, warmup rounds and repeated submit-to-fence measurements.
Both include the memory helpers from `../t06-memory/import_visibility.c`.
They load the supplied driver directly and check output, input and guards.

For the timing campaign, install Vulkan headers, Termux clang,
`glslangValidator` and `spirv-val`, then run from any directory:

```sh
python3 /path/to/repo/tests/graphics/t07-matrix/timing/run_timing.py \
  /absolute/path/to/driver.so /absolute/path/to/new-results
```

The output directory must not exist. The runner compiles into that directory,
pins its inputs, checks compiler statistics, runs three ABBA blocks per layout
and a wrong-reference control, then verifies that the inputs stayed unchanged.
`--cc`, `--glslang` and `--spirv-val` select the installed tools. Use one GPU
runner at a time and retain failed runs as well as passing ones.

To build the smaller probe, run these commands from `correctness/`:

```sh
clang -O2 -Wall -Wextra -Werror -I../../t06-memory \
  matrix_probe.c -o /tmp/matrix_probe -ldl
glslangValidator -V --target-env vulkan1.1 row.comp -o /tmp/matrix-row.spv
spirv-val --target-env vulkan1.1 /tmp/matrix-row.spv
PANVK_ICD_SO=/absolute/path/to/driver.so \
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 MESA_SHADER_CACHE_DISABLE=true \
PANVK_UBO_PUSH=0 PAN_LATE_VECTORIZE=true /tmp/matrix_probe /tmp/matrix-row.spv row
```

Repeat with `PAN_LATE_VECTORIZE=false`, then compile `column.comp` and pass
`column` as the last argument. `MATRIX_WRONG_EXPECTED=1` deliberately corrupts
one expected output and must fail the oracle. Hardware results and their
limits are in the [validation report](../../../evidence/validation/matrix-2026-10-23/README.md).
