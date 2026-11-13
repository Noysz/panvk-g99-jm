# November candidate reconstruction and build

The final reviewed composition is Mesa
`feeadb5f55f4ba14a5c5ddc84ba20177e419cffb`, the 19 Test9 patches, five early
November correctness patches, 17 stock JM development patches, then four
CPU-lifetime/fault patches. This is an opt-in source candidate; the
[Test9 build order](test9/build-order.md) and public binaries are unchanged.

Use the authorized existing development workspace. Preserve its dirty changes
and artifacts before applying anything. The lists below are ordered plain
diffs relative to this repository:

- [41-patch baseline](november-baseline.series)
- [45-patch lifecycle candidate](november-lifecycle.series)

Check and apply each patch in order with `git apply --check` and `git apply`.
Do not layer these full lists on top of an already patched tree.
The source-chain verifier in [tools/](../tools/README.md) can reconstruct
the expected index without creating another Mesa checkout.

The [source manifest](../evidence/validation/jm-lifecycle-2026-11-13/source.json)
records exact patch hashes and Git trees. The existing tested source and all
four frozen binary hashes were checked again during repository preparation.
No runtime source changed, so the existing complete build/CTS/performance
qualification is reused. Textual applicability alone was not used as proof.

## Build recipe

Use native Termux Clang/LLD 21.1.8, Meson 1.12.0 and Ninja 1.13.2 with the
exact options in [builds.json](../evidence/validation/jm-lifecycle-2026-11-13/builds.json).
`${PREFIX}` means the Termux installation prefix. Dependency hashes are
recorded there; a different dependency set is a new build requiring validation.

1. Select a new build-output directory, disable ccache and use
   `--wrap-mode=nodownload`.
2. Build Host first: `src/panfrost/vulkan/libvulkan_panfrost.so`,
   `src/compiler/clc/mesa_clc`, `src/panfrost/clc/panfrost_compile` and
   `src/compiler/spirv/vtn_bindgen2`, with Ninja `-j4`.
3. Expose those three freshly built helper executables on PATH. Configure a
   separate Android output with the recorded API 29/stub/release options;
   build `src/panfrost/vulkan/libvulkan_panfrost.so`.
4. Check compile commands for kbase, JM v6/v7/v9, owner source inclusion and
   effective Android macros. Record ELF identity, dependencies and SHA-256.
5. Run the [candidate validation](../evidence/validation/jm-lifecycle-2026-11-13/README.md)
   for changed bytes. Keep overlap and experimental physical retirement off.

This records the full successful build procedure, not a guarantee that a
new environment produces identical ELF bytes. Do not substitute generated
shaders or object files from another source composition.
