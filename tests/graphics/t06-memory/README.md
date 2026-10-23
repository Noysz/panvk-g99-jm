# Memory and tiler probes

These are the sources used by the T06 checks. They load the driver directly
through `PANVK_ICD_SO`. Run on the documented G57 device; the dma-buf exporter
checks depend on the available Android system heaps.

Build the visibility probe with Termux clang and Vulkan headers:

```sh
clang -O2 -Wall -Wextra -Werror import_visibility.c -o import_visibility_v2 -ldl
python3 run_visibility.py /absolute/path/to/driver.so visibility-run --candidate
```

The runner creates a new output directory and checks three repetitions of each
memory combination. Existing output directories are rejected. To check its
data oracle, run a cached import with the deliberate wrong-reference option:

```sh
PANVK_ICD_SO=/absolute/path/to/driver.so \
PAN_I_WANT_A_BROKEN_VULKAN_DRIVER=1 MESA_SHADER_CACHE_DISABLE=true \
VISIBILITY_WRONG_EXPECTED=1 ./import_visibility_v2 system cached
```

Build the tiler probe and diagnostic ioctl interposer from this directory,
using headers from the matching Mesa checkout:

```sh
clang -O2 -Wall -Wextra -Werror tiler_test.c -o tiler_test -ldl -lm
clang -O2 -Wall -Wextra -Werror -shared -fPIC -I/path/to/mesa/include \
  tiler_ioctl_probe.c -o tiler_ioctl_probe.so -ldl
python3 run_tiler.py /absolute/path/to/driver.so tiler-on --enabled true
python3 run_tiler.py /absolute/path/to/driver.so tiler-off --enabled false
```

Keep the supplied cube shader fixtures in the working directory. The pixel
oracle excludes ambiguous rasterization edges. `--negative` changes the CPU
reference angle and must fail the pixel check; `--alternate --instances 8192`
exercises backing growth, and `--resubmit --instances 8192` reuses recorded work.
`--inject EINVAL`, `--inject ENOMEM` and `--inject strip` are diagnostic controls
for the allocation path. They are not normal driver settings.

Use a fresh output label for each run and one GPU runner at a time. Results are
correctness checks, not a frame-rate benchmark. The frozen run results and
remaining failures are in the [evidence directory](../../../evidence/validation/memory-2026-10-23/README.md).
