# T09 probe sources

These are unchanged source copies of the timestamp, vertex-input,
conditional-predicate and stored-depth probes used in the
[validation record](../../../evidence/validation/api-2026-10-26/README.md).
They are diagnostic programs, not an automatic release test suite.

- `timestamp/`: query API and multiview probes. `import_visibility.c` supplies
  the Vulkan loading/allocation helpers. `permon_control.c` supplies the
  timestamp-counter control used by the local diagnostic.
- `vertex/`: loader and native-ICD versions, generated GLSL inputs and the
  frozen case matrix.
- `conditional/`: loader and native-ICD versions of the inherited/local
  predicate shadow reproducer; uses the vertex shaders from `vertex/`.
- `bounds/`: stored-depth image and depth readback oracle, with D16/D32 and
  early/late fragment shaders.

Use Vulkan headers from the matching Mesa source. Compile loader executables
with `cc -O2 -Wall -I<mesa>/include <probe.c> -o <output> -ldl -lm`; use
Termux's `clang` for native executables. The Android-only `permon_control.c`
interposer requires Termux's `clang -shared -fPIC`, not the host libc's ioctl
prototype. Compile GLSL with `glslangValidator -V --target-env vulkan1.3`.
Build into a fresh directory.
The include files must remain beside their corresponding C source. Shader
paths, environment controls and expected outcomes are recorded in the local
run manifests identified by the published audit files.

Always select the driver explicitly with `PANVK_ICD_SO`; the historical
fallback path inside the bounds source is not a portable default. Loader
variants and native variants use different entry points. Do not treat a
loader error or unsupported feature as a passing render test.

Run GPU diagnostics one at a time. Keep positive and negative controls in
separate output directories, and compare data/pixels as well as process exit
status. Existing captures and builds must not be overwritten.
