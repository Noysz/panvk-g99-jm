#!/usr/bin/env python3
"""Prepare the pinned November source, or verify it without a Mesa checkout."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parents[1]
MANIFEST = "evidence/validation/jm-lifecycle-2026-11-13/source.json"
SERIES = "docs/november-lifecycle.series"
UPSTREAM = "https://gitlab.freedesktop.org/mesa/mesa.git"


def git(repo, *args, env=None):
    clean = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    clean.update(env or {})
    return subprocess.check_output(
        ["git", "-c", "core.hooksPath=" + os.devnull, "-c", "gc.auto=0",
         "-C", str(repo), *args], env=clean, stderr=subprocess.PIPE,
    ).decode().strip()


def load_stack(root):
    root = root.resolve()
    manifest = json.loads((root / MANIFEST).read_text())
    names = [line.strip() for line in (root / SERIES).read_text().splitlines()
             if line.strip() and not line.lstrip().startswith("#")]
    records = manifest["patches"]
    if (names != [item["path"] for item in records]
            or len(names) != manifest["candidate_patch_count"]
            or len(names) != len(set(names))):
        raise ValueError("patch order/count differs from the source manifest")
    for key in ("upstream", "baseline_tree", "candidate_tree"):
        if not re.fullmatch(r"[0-9a-f]{40}", manifest[key]):
            raise ValueError("invalid source identity: " + key)
    if not 0 < manifest["baseline_patch_count"] <= len(names):
        raise ValueError("invalid baseline patch count")
    patches = []
    for item in records:
        name = Path(item["path"])
        path = (root / name).resolve(strict=True)
        if name.is_absolute() or ".." in name.parts or root not in path.parents:
            raise ValueError("patch path escapes the repository")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != item["sha256"]:
            raise ValueError("SHA-256 mismatch: " + item["path"])
        patches.append(data)
    return manifest, patches


def apply_stack(repo, manifest, patches, directory, env=None, cached=False):
    option = "--cached" if cached else "--index"
    for number, data in enumerate(patches, 1):
        # Apply the bytes already checked, even if the public patch files change.
        patch = directory / (str(number) + ".patch")
        patch.write_bytes(data)
        git(repo, "apply", option, "--check", str(patch), env=env)
        git(repo, "apply", option, "--whitespace=nowarn", str(patch), env=env)
        if number == manifest["baseline_patch_count"]:
            if git(repo, "write-tree", env=env) != manifest["baseline_tree"]:
                raise ValueError("baseline source tree differs from the manifest")
    tree = git(repo, "write-tree", env=env)
    if tree != manifest["candidate_tree"]:
        raise ValueError("candidate source tree differs from the manifest")
    return tree


def verify_with(repo, manifest, patches):
    repo = repo.resolve(strict=True)
    if git(repo, "rev-parse", "--verify", manifest["upstream"] + "^{commit}") != manifest["upstream"]:
        raise ValueError("base commit differs from the manifest")
    objects = Path(git(repo, "rev-parse", "--git-path", "objects"))
    if not objects.is_absolute():
        objects = repo / objects
    with tempfile.TemporaryDirectory(prefix="fourfectvk-source-") as temporary:
        directory = Path(temporary)
        (directory / "objects").mkdir()
        # Only read existing objects; never write the workspace's index or objects.
        env = {
            "GIT_INDEX_FILE": str(directory / "index"),
            "GIT_OBJECT_DIRECTORY": str(directory / "objects"),
            "GIT_ALTERNATE_OBJECT_DIRECTORIES": str(objects.resolve()),
            "GIT_OPTIONAL_LOCKS": "0",
        }
        git(repo, "read-tree", manifest["upstream"], env=env)
        return apply_stack(repo, manifest, patches, directory, env, cached=True)


def prepare(output, manifest, patches, upstream=UPSTREAM):
    # Refuse even an empty existing directory. A failed run is left for inspection.
    output = output.absolute()
    output.mkdir()
    git(output, "init", "--quiet", "--template=", "--object-format=sha1")
    git(output, "remote", "add", "origin", upstream)
    git(output, "fetch", "--no-tags", "--depth=1", "origin", manifest["upstream"])
    if git(output, "rev-parse", "FETCH_HEAD^{commit}") != manifest["upstream"]:
        raise ValueError("fetched base differs from the manifest")
    git(output, "checkout", "--detach", manifest["upstream"])
    with tempfile.TemporaryDirectory(prefix="fourfectvk-patches-") as temporary:
        tree = apply_stack(output, manifest, patches, Path(temporary))
    git(output, "diff", "--quiet")
    return tree


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--output", type=Path,
                      help="fetch upstream and prepare source in a NEW directory")
    mode.add_argument("--verify-with", type=Path,
                      help="verify using existing Mesa objects; no checkout or network")
    args = parser.parse_args()
    try:
        manifest, patches = load_stack(ROOT)
        if args.output:
            tree = prepare(args.output, manifest, patches)
        else:
            tree = verify_with(args.verify_with, manifest, patches)
    except (OSError, ValueError, KeyError, TypeError, subprocess.CalledProcessError) as error:
        detail = error.stderr.decode(errors="replace") if isinstance(error, subprocess.CalledProcessError) else str(error)
        print("ERROR: " + detail, file=sys.stderr)
        if args.output:
            print("Existing/partial output is preserved; do not build a failed preparation.", file=sys.stderr)
        return 1
    print(f"PASS: {len(patches)} patch hashes, order, base and source trees verified.")
    print("Base: " + manifest["upstream"])
    print("Tree: " + tree)
    if args.output:
        print("Source prepared; patches are staged, with HEAD at the upstream base.")
        print("No build or runtime qualification was performed by this command.")
    else:
        print("Index-only verification; existing source files, index and refs unchanged.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
