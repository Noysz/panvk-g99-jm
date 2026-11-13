# Build from source

This repository distributes FourFectVK as patches on Mesa. It is not a full
Mesa checkout. The November development source is available on `main`;
[Test 9](test9/build-order.md) remains the latest public binary release.

## Get the November source

Requires Git, Python 3.9 or newer, network access and space for Mesa and its
build outputs. No root access is needed. Run from a development directory:

```sh
git clone --branch main https://github.com/Noysz/panvk-g99-jm.git
cd panvk-g99-jm
git rev-parse HEAD
python3 tools/prepare_source.py --output ../mesa-fourfectvk
```

Already have this repository? Run only the last command from its root.
The destination **must not exist**, even as an empty directory. The helper
fetches the pinned upstream commit, verifies every patch's SHA-256 and order,
and applies all 45 patches. It checks both the intermediate and final Git
trees against the [source manifest](../evidence/validation/jm-lifecycle-2026-11-13/source.json).
It does not build or install a driver, execute source scripts, or create commits.

On success, `HEAD` is the upstream base and the patches are staged. Review
them with `git -C ../mesa-fourfectvk diff --cached`. On any error, stop:
partial output is preserved for inspection and must not be used as a build.
The helper never resets, cleans or overwrites an existing checkout.

| Source identity | Value |
|---|---|
| Mesa base commit | `feeadb5f55f4ba14a5c5ddc84ba20177e419cffb` |
| Tree after 41 patches | `d2e6775772b381ceec10ef121a854588076733a9` |
| Tree after all 45 patches | `87622157bda2463dc19cb09becbc067945f2da46` |

The [ordered list](november-lifecycle.series) contains 19 Test 9 patches,
five November correctness patches, 17 driver development patches, then four
driver stability patches. The [41-patch baseline](november-baseline.series)
is retained for comparisons. These are plain diffs, not `git am` mailboxes.
Do not add the older `patches/` series or apply a full list over patched source.
Copyright, licensing and upstream attribution remain in the patches and source.

### Verify without another checkout

For an existing Mesa repository containing the base commit:

```sh
python3 tools/prepare_source.py --verify-with /path/to/existing/mesa
```

This uses temporary index/object storage and reads existing Git objects. It
does not fetch, check out files, or change that workspace's source, index or
refs. It verifies the reconstruction, not the contents of a dirty workspace.
For a file-by-file comparison use [verify_patch_chain.py](../tools/verify_patch_chain.py).

**Verification scope:** the real 45-patch stack was replayed in index-only
mode and matched both recorded trees. Automated tests cover a local Git
fetch and checkout, binary files, symlinks, dirty-workspace preservation,
bad hashes/order, unavailable base, patch failure and tree mismatch. The
helper's fresh Mesa download/checkout path has not been run against the
upstream server; no extra Mesa workspace was created for this update.

## Build Host, then Android

The recorded full builds use native AArch64 Termux, Clang/LLD 21.1.8,
Meson 1.12.0 and Ninja 1.13.2. Install the build dependencies first; compiler
and dependency identities and the exact Meson options are in
[builds.json](../evidence/validation/jm-lifecycle-2026-11-13/builds.json).
This recipe assumes that environment is already available. It is not a
generic desktop cross-compilation recipe or a dependency installer.

Run from the patch repository root after successful preparation. Choose
unused build-directory names; keep previous binaries and logs.

```sh
export FOURFECTVK_REPO="$PWD"
export CC=clang CXX=clang++ AR=llvm-ar CCACHE_DISABLE=1
cd ../mesa-fourfectvk
python3 - <<'PY'
import json
import os
from pathlib import Path
import subprocess

record = Path(os.environ['FOURFECTVK_REPO']) / 'evidence/validation/jm-lifecycle-2026-11-13/builds.json'
recipes = json.loads(record.read_text())['recipes']
assert os.environ.get('PREFIX'), 'Run in the recorded Termux environment'
host = Path('build-november-host')
android = Path('build-november-android')
if host.exists() or android.exists():
    raise SystemExit('Choose unused build directories before continuing')

def setup(directory, variant):
    options = [os.path.expandvars(x) for x in recipes[variant]['meson_options']]
    subprocess.run(['meson', 'setup', str(directory), '--wrap-mode=nodownload', *options], check=True)

helpers = ['src/compiler/clc', 'src/panfrost/clc', 'src/compiler/spirv']
setup(host, 'host')
subprocess.run(['ninja', '-C', str(host), '-j4',
                'src/panfrost/vulkan/libvulkan_panfrost.so',
                'src/compiler/clc/mesa_clc', 'src/panfrost/clc/panfrost_compile',
                'src/compiler/spirv/vtn_bindgen2'], check=True)
os.environ['PATH'] = os.pathsep.join(str((host / x).resolve()) for x in helpers) + os.pathsep + os.environ['PATH']
setup(android, 'android')
subprocess.run(['ninja', '-C', str(android), '-j4',
                'src/panfrost/vulkan/libvulkan_panfrost.so'], check=True)
PY
sha256sum build-november-host/src/panfrost/vulkan/libvulkan_panfrost.so \
          build-november-android/src/panfrost/vulkan/libvulkan_panfrost.so
```

The commands express the recorded build options; they were not used to run
another build for this documentation update. The unchanged source's
[complete build and validation results](../evidence/validation/jm-lifecycle-2026-11-13/README.md)
remain applicable to the recorded binaries. Rebuilding with different tools
or dependencies may produce different bytes and needs fresh validation.

Before testing a rebuild, check compile commands for kbase/JM source and
effective Android macros, then record ELF identity, dependencies and SHA-256.
Use the host helpers from this source composition. A successful source
reconstruction or compilation alone does not qualify a new driver binary.

## Development limits

The November patches are available for review and development, not a new
release. CPU lifecycle validation does not prove complete GPU error recovery.
P0 remains **BLOCKED**, 0012 remains **REWORK / default OFF**, and experimental
physical retirement stays inactive. Test 10 remains **PRIVATE / NO-GO**.
See [development decisions](november-development.md) and the
[release gates](release-roadmap.md) before proposing runtime changes.
