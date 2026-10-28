# Test9 build and package order

Deliver the same file layout as the earlier test builds: a driver ZIP and
`README-v0.1.0-test9.txt` beside it. Put identical README bytes inside the
ZIP as `README.txt`. Publish checksums beside both files.

The [testing README](README-v0.1.0-test9.txt) contains installation, R0-R6
runs, T06-T10 environment defaults/functions, diagnostics and the issue-report
template. Existing baseline/logging controls are identified separately.
Unrelated backend options and deliberate fault-injection controls are omitted.

## 1. Pin the candidate

The candidate stack is published at
`7f28e47db6e50215fbe2935cecaf048f23d6dbfb`, based on upstream Mesa
`feeadb5f55f4ba14a5c5ddc84ba20177e419cffb` (26.3.0-devel).
Keep the existing tested binaries. Packaging those bytes needs no rebuild.

For a future rebuild, use a fresh Mesa checkout and new build directories.
Apply these groups in order, following each `series` file within a group:

1. `patches-upstream/0001` through `0005`.
2. `patches-candidates/2026-10-19/series` (checkpoint, cache and compiler).
3. `patches-candidates/2026-10-19-deferred-query/series`.
4. `patches-candidates/2026-10-23-memory/series`.
5. `patches-candidates/2026-10-26-api/series`.

These are plain diff patches. Check and apply them in sequence with
`git apply`; do not treat them as mailbox commits. The API directory's
[README](../../patches-candidates/2026-10-26-api/README.md) records the
prerequisites and limits. The older `patches/0001`-`0077` series targets a
different base and is not an additional layer on this stack.

Run the repository's source-chain verification before accepting a rebuild.
The published replay compares 12,929 files with zero differences, with its
two documented support-file exclusions and root-document mode exception.
This establishes source reconstruction, not reproducible clean-build bytes.

## 2. Build host tools before the Android target, if rebuilding

Use the Termux toolchain and the host/Android options in
[the build recipe](../../patches-upstream/README.md#build-termux-on-the-phone).
Record compiler, Meson and Ninja versions, effective Meson options and logs.
Use new directory names instead of replacing the successful build outputs.

Build the host PanVK target first. Make its `mesa_clc`, `panfrost_compile`
and `vtn_bindgen2` available to the Android build from that same source tree.
Then build the Android target with API 29, Android stubs, kbase/panthor,
release settings and assertions disabled as documented in that recipe.
Build one target at a time; the recorded candidate uses Ninja `-j2`.
Do not mix generated helpers from a different source revision.

Copy outputs to a new artifact directory and record SHA-256, ELF Build ID,
architecture, required libraries and RUNPATH. A fresh build with different
bytes is a new candidate and needs validation. Do not overwrite the frozen
libraries merely to obtain a new package name.

## 3. Check validation scope

The [combined-build evidence](../../evidence/validation/api-2026-10-26/README.md)
records feature repeats/controls, the tracked CTS comparison and application
checks for the frozen libraries. It also records the 14 remaining failures,
unresolved overlap lifetime, partial game coverage and other limits.

For unchanged bytes, retain those results and perform the package checks
below. For changed driver/dependency bytes, run the relevant regression
before promotion. A successful build is not a test result. Development
test9 and the roadmap's RC/1.0 qualification remain separate.

## 4. Assemble a new package

Use a new preparation directory. Preserve previous ZIPs and their checksum
files. The tested launcher staging pins these Android libraries:

| File | SHA-256 |
|---|---|
| `libvulkan_panfrost.so` | `595981cc9ec21f0b667a4127e6b31bb9a03c8d2aa4278b04a7daefff4f3dcf4e` |
| `libc++_shared.so` | `e09c2f45cf4cf8ae574f94b6c2650d99ead0d332d5396f6613f062a2d2d73540` |
| `libdrm.so` | `0e4e8f0bbc6809589206a757759a6506e0aa7c88e0b55de2fe8d54f7ef6a3323` |
| `libz.so.1` | `c47748ed4b6ca6a2ecd664bab04795f3493956ab1f5cddc4ba0efa737ef3b711` |

The `libdrm` and `libz` hashes differ from the old test8 ZIP. Use the pinned
tested dependencies; silently mixing old release dependencies would create
an untested combination. Preserve their required license notices.

ZIP root, without an extra containing directory:

```text
libvulkan_panfrost.so
libc++_shared.so
libdrm.so
libz.so.1
meta.json
LICENSE-libc++.txt
LICENSE-libdrm.txt
LICENSE-mesa.txt
LICENSE-zlib.txt
README.txt
```

Keep the historical metadata schema: `schemaVersion: 1`,
`name: FourFectVK-G57-v0.1.0-test9`, `author: panvk-g99-jm`,
`packageVersion: "2"`, `vendor: mesa`,
`driverVersion: 26.3.0-devel-fourfectvk-0.1.0-test9`, `minApi: 29`,
`libraryName: libvulkan_panfrost.so`. Describe it as experimental and
non-conformant. The driver reads the package metadata for its display suffix;
changing the package label does not require editing the library.

Create `FourFectVK-G57-v0.1.0-test9.zip`, the external README and
`SHA256SUMS.txt`. Record a separate manifest for every extracted member.
Check ZIP CRCs, filenames, metadata and licenses, extract to a fresh
directory, and compare every file hash with its input. Require the internal
and external README to match byte for byte.

## 5. Check the extracted package

Use the extracted directory, not the original build/staging path:

1. Check all four library hashes and record the libraries actually mapped
   by the launcher. RUNPATH inspection alone cannot establish routing.
2. Check Adrenotools and the existing launcher library, wrapper device
   creation and `dxvk_probe`, including depth bounds off/on. Confirm the
   test9 name derived from metadata. Record the exact environment.
3. Run a bounded DXVK render/readback smoke test and its deliberately wrong
   pixel-reference control. Correct readback must pass; the control must be
   detected. Use an isolated prefix and logs. The completed four-version
   matrix need not be repeated just because the package name changed.
4. Recheck hashes after execution and stop only the processes created for
   this check. Record any failure without retrying until its cause is clear.

These checks test archive/loading integrity on the recorded stack. They do
not replace an actual Winlator app/container install or multi-device tests.
Report a pending or failed check as such; do not call a prepared ZIP tested
until the package-specific results exist.

## 6. Review, then publish separately

Check the testing README against the final source and evidence. It must
retain the UBO default of 32 words, the presence-based `NO_FPK` behavior,
app-rule overrides, disabled overlap and opt-in depth bounds. Include the
known failures and make no gameplay speedup or RC-conformance claim.

Commit/push and GitHub prerelease publication are separate actions. A local
ZIP is a preparation artifact. Once publication is authorized, attach the
ZIP, external README and checksum file together and verify the downloaded
assets against those checksums. Keep private research outside the release.
