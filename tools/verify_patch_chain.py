#!/usr/bin/env python3
"""Reconstruct a patch stack and compare it with every Git-visible source file."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile


def run(*args, cwd=None, env=None):
    return subprocess.check_output(args, cwd=cwd, env=env, stderr=subprocess.PIPE)


def reconstruct(repo, base, patches, directory):
    env = {k: v for k, v in os.environ.items() if not k.startswith("GIT_")}
    objects = run("git", "rev-parse", "--git-path", "objects", cwd=repo).decode().strip()
    env["GIT_ALTERNATE_OBJECT_DIRECTORIES"] = str((repo / objects).resolve())
    gitdir = Path(directory) / "expected.git"
    fmt = run("git", "rev-parse", "--show-object-format", cwd=repo).decode().strip()
    run("git", "init", "--bare", "--quiet", f"--object-format={fmt}", str(gitdir), env=env)
    command = ["git", f"--git-dir={gitdir}"]
    run(*command, "read-tree", base, env=env)
    for patch in patches:
        run(*command, "apply", "--cached", "--whitespace=nowarn", str(patch), env=env)
    return command, env, fmt


def index_entries(command, env):
    entries = {}
    for record in run(*command, "ls-files", "--stage", "-z", env=env).split(b"\0"):
        if record:
            metadata, path = record.split(b"\t", 1)
            mode, oid, stage = metadata.decode().split()
            if stage != "0":
                raise ValueError("unmerged index entry")
            entries[os.fsdecode(path)] = (mode, oid)
    return entries


def worktree_entries(repo, paths, algorithm):
    entries, regular = {}, []
    for name in paths:
        path = repo / name
        try:
            info = path.lstat()
        except FileNotFoundError:
            continue
        if stat.S_ISLNK(info.st_mode):
            data = os.fsencode(os.readlink(path))
            oid = hashlib.new(algorithm, b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
            entries[name] = ("120000", oid)
        elif stat.S_ISREG(info.st_mode):
            regular.append((name, "100755" if info.st_mode & 0o111 else "100644"))
        else:
            entries[name] = ("unsupported", "")
    if regular:
        # Let Git honor attributes such as Mesa's CSV checkout CRLF conversion.
        names = "".join(json.dumps(name, ensure_ascii=False) + "\n" for name, _ in regular)
        hashes = subprocess.run(
            ["git", "-c", "core.safecrlf=false", "hash-object", "--stdin-paths"],
            input=names.encode(), cwd=repo, stdout=subprocess.PIPE,
            stderr=subprocess.PIPE, check=True,
        ).stdout.decode().splitlines()
        if len(hashes) != len(regular):
            raise ValueError("incomplete source hash output")
        for (name, mode), oid in zip(regular, hashes):
            entries[name] = (mode, oid)
    return entries


def verify(repo, base, patches, exclusions, allow_root_doc_pointer_mode=False):
    def excluded(name):
        return any(name.startswith(item) if item.endswith("/") else name == item
                   for item in exclusions)

    with tempfile.TemporaryDirectory(prefix="panvk-chain-") as directory:
        command, env, fmt = reconstruct(repo, base, patches, directory)
        expected = index_entries(command, env)
        actual = {os.fsdecode(p) for p in run(
            "git", "ls-files", "--cached", "--others", "--exclude-standard", "-z", cwd=repo
        ).split(b"\0") if p}
        paths = sorted(set(expected) | actual)
        contents = worktree_entries(repo, [name for name in paths if not excluded(name)], fmt)
        differences = []
        skipped = []
        root_doc_pointer_mode_differences = 0
        for name in paths:
            if excluded(name):
                skipped.append(name)
                continue
            want = expected.get(name)
            got = contents.get(name)
            if (allow_root_doc_pointer_mode and "/" not in name and name.endswith(".md")
                    and want and got and {want[0], got[0]} == {"100644", "120000"}
                    and want[1] == got[1]):
                root_doc_pointer_mode_differences += 1
                continue
            if want != got:
                differences.append({"path": name, "expected": want, "actual": got})
        return {
            "ok": not differences,
            "base": base,
            "expected_tree": run(*command, "write-tree", env=env).decode().strip(),
            "repository": str(repo),
            "patches": [{"path": str(p), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                        for p in patches],
            "compared_files": len(paths) - len(skipped),
            "exclusions": exclusions,
            "excluded_files": skipped,
            "differences": differences,
            "allow_root_doc_pointer_mode": allow_root_doc_pointer_mode,
            "root_doc_pointer_mode_differences": root_doc_pointer_mode_differences,
            "scope": "All expected files plus tracked and non-ignored untracked files; ignored build outputs are excluded.",
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", type=Path, required=True)
    parser.add_argument("--base", required=True)
    parser.add_argument("--exclude", action="append", default=[],
                        help="Explicit relative file or directory ending in /; recorded in the report")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--allow-root-doc-pointer-mode", action="store_true",
                        help="Allow a root Markdown symlink stored as a regular file with identical target bytes; source modes remain strict")
    parser.add_argument("patches", type=Path, nargs="+")
    args = parser.parse_args()
    try:
        repo = args.repo.resolve(strict=True)
        base = run("git", "rev-parse", "--verify", args.base + "^{commit}", cwd=repo).decode().strip()
        for item in args.exclude:
            if not item or Path(item).is_absolute() or ".." in Path(item).parts or item in (".", "./"):
                raise ValueError(f"invalid exclusion: {item!r}")
        result = verify(repo, base, [p.resolve(strict=True) for p in args.patches],
                        args.exclude, args.allow_root_doc_pointer_mode)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        detail = error.stderr.decode(errors="replace") if isinstance(error, subprocess.CalledProcessError) else str(error)
        result = {"ok": False, "error": detail}
    if args.report:
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    if "error" in result:
        print("ERROR: " + result["error"], file=sys.stderr)
        return 2
    for difference in result["differences"]:
        print("DIFF " + difference["path"])
    print(f"files {result['compared_files']} differences {len(result['differences'])} excluded {len(result['excluded_files'])}")
    print("PASS" if result["ok"] else "FAIL")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
