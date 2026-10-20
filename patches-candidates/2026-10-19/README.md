# Compiler and cache candidate - 2026-10-19

Apply after `patches-upstream/0001` through `0005`, in `series` order.
This is a development candidate, not test9 or a release recommendation.

`0000` preserves the pre-existing driver changes after the published stack,
including K2/K4/K5. Local root-document metadata is omitted.
It does not establish new performance evidence for those changes.

`0001` separates shader cache keys for effective compile controls, including
the K2 negative control. The five numbered donor patches retain their original
authors, dates, message bodies and source hunks. A trailing subject annotation
is omitted from the published copies. `provenance.json` records separate
upstream and published hashes, plus the unchanged source-diff hashes, pinned
to Zenithblue main `5b87b74d3e2580d5816e7f2c629f6f016a462f28`.

- 152: live-node interference and first-fit register allocation lookup.
- 162: flat block/variable map for SSA repair.
- 163: skip optimization passes until another pass makes progress.
- 170: enable Android disk shader cache by default when its path is usable.
- 171: cache Android property lookups; environment variables are still read
  first. System property changes during the process lifetime are not observed
  after the first lookup of that option.

The allocator patches target compilation cost. Runtime FPS or shader-code
equivalence on a game corpus has not been established by applying them.
The Android cache patch changes shared utility behavior; this candidate is
for the existing Vulkan-only builds. `MESA_SHADER_CACHE_DISABLE=true` remains
the explicit off switch, including for causal compiler comparisons.

## Validation

Use `tools/verify_patch_chain.py`, not a touched-files-only comparison:

```sh
python3 tools/verify_patch_chain.py \
  --repo /data/data/com.termux/files/home/panvk-g57/mesa-up \
  --base feeadb5f55f4ba14a5c5ddc84ba20177e419cffb \
  --allow-root-doc-pointer-mode \
  --exclude MERGE-0060-0077.md \
  --exclude REBASE-RESOLUTIONS.md \
  --exclude src/panfrost/tools/snapdec.c \
  --exclude build-android-hosttools/ \
  patches-upstream/000[1-5]*.patch \
  patches-candidates/2026-10-19/0*.patch \
  patches-candidates/2026-10-19/1*.patch
```

The exclusions cover existing support files and local rebase notes. A clean
source checkout does not need them. The pointer-mode option accepts a root
Markdown symlink stored as a regular file containing the same target bytes.
All source-file contents and modes remain strict, as do document contents.
Twelve gate tests cover both accepted and rejected cases. Ignored build outputs
are outside its source-tree scope. The final replay including the deferred
query candidate is recorded in the deferred-query evidence directory.

Current task status and evidence are recorded in
`docs/followup-tasks-2026-10-19.md`. No commit or release is implied by this
directory.
