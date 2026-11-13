#!/usr/bin/env python3
"""Run the pinned CPU lifecycle fixtures; never open a GPU device."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import resource
import subprocess


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mesa", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--cc", default="cc")
    parser.add_argument("--cflag", action="append", default=[])
    parser.add_argument("--ubsan", action="store_true")
    args = parser.parse_args()
    fixtures = Path(__file__).resolve().parent / "fixtures"
    manifest = json.loads((fixtures / "manifest.json").read_text())
    for name, expected in manifest["fixture_sha256"].items():
        if digest(fixtures / name) != expected:
            raise SystemExit("Fixture identity mismatch: " + name)
    for name, expected in manifest["candidate_source_sha256"].items():
        if digest(args.mesa / name) != expected:
            raise SystemExit("Source differs from the qualified fixture: " + name)
    args.output.mkdir(parents=True, exist_ok=False)
    out = args.output.resolve()
    env = dict(os.environ)
    env.pop("LD_PRELOAD", None)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    flags = ["-std=c11", "-D_GNU_SOURCE", "-DPAN_ARCH=9", "-pthread",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-parameter",
             "-Wno-unused-function", "-Wno-unused-value", "-I" + str(fixtures)]
    flags += (["-O1", "-fsanitize=undefined", "-fno-sanitize-recover=all"]
              if args.ubsan else ["-O2"])
    flags += args.cflag
    controls = {
        "adapter-negative": ("k.completed_seqno==0",),
        "device-negative": ("cpu_closed", "frame_prof.enabled"),
    }
    records = []
    for name in ["cpu", "adapter", "adapter-negative", "device", "device-negative", "init"]:
        binary = out / name
        commands = [
            ("build", [args.cc, *flags, str(fixtures / (name + ".c")), "-o", str(binary)]),
            ("run", [str(binary)]),
        ]
        for phase, command in commands:
            proc = subprocess.run(command, capture_output=True, text=True,
                                  env=env, cwd=out, timeout=90)
            output = proc.stdout + proc.stderr
            (out / (name + "-" + phase + ".log")).write_text(output)
            negative = phase == "run" and name in controls
            ok = (proc.returncode != 0 and any(s in output for s in controls[name])
                  if negative else proc.returncode == 0)
            records.append({"name": name, "phase": phase, "rc": proc.returncode,
                            "expected_failure": negative, "ok": ok})
            (out / "result.json").write_text(json.dumps(records, indent=2) + "\n")
            if not ok:
                raise SystemExit(name + " " + phase + " failed; see " + str(out))
        print(name + ": " + ("expected assertion detected" if name in controls else "PASS"))
    print("CPU fixture checks passed; GPU terminal behavior is not tested.")


if __name__ == "__main__":
    main()
