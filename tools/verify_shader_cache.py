#!/usr/bin/env python3
"""Exercise disk-cache reuse and separation across independent Vulkan processes."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cts", type=Path, required=True)
    parser.add_argument("--driver", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--expect", choices=("separated", "shared"), default="separated")
    parser.add_argument("--default-cache", action="store_true")
    parser.add_argument("--knob", default="PANVK_V9_CLIP_KILL_FPK")
    parser.add_argument("--case", default="dEQP-VK.draw.renderpass.simple_draw.simple_draw_triangle_list")
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.repeats < 1:
        parser.error("repeats must be positive")
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    driver = args.driver.resolve(strict=True)
    cts = args.cts.resolve(strict=True)
    executable = cts / "build/external/vulkancts/modules/vulkan/deqp-vk"
    env = dict(os.environ, PANVK_ICD_SO=str(driver),
               PAN_I_WANT_A_BROKEN_VULKAN_DRIVER="1", MESA_SHADER_CACHE_SHOW_STATS="true")
    env.pop("MESA_SHADER_CACHE_DISABLE", None)
    if not args.default_cache:
        env["MESA_SHADER_CACHE_DISABLE"] = "false"
    records = []
    failures = []
    steps = [("default-cold", "0"), ("default-warm", "0"),
             ("control-after-default", "1"), ("control-warm", "1"),
             ("default-return", "false"), ("cache-disabled", "0")]
    for repetition in range(1, args.repeats + 1):
        cache = out / f"cache-{repetition}"
        for label, value in steps:
            name = f"{repetition}-{label}"
            run_env = dict(env, MESA_SHADER_CACHE_DIR=str(cache))
            run_env[args.knob] = value
            if label == "cache-disabled":
                run_env["MESA_SHADER_CACHE_DISABLE"] = "true"
                run_env["MESA_SHADER_CACHE_DIR"] = str(out / f"disabled-{repetition}")
            command = [str(executable), "--deqp-vk-library-path=" + str(cts / "shim/libvkshim.so"),
                       "--deqp-case=" + args.case, "--deqp-log-filename=" + str(out / (name + ".qpa")),
                       "--deqp-log-images=disable", "--deqp-log-shader-sources=disable",
                       "--deqp-watchdog=disable"]
            start = time.monotonic()
            with (out / (name + ".log")).open("w") as log:
                process = subprocess.run(command, cwd=executable.parent, env=run_env,
                                         stdout=log, stderr=subprocess.STDOUT, timeout=120)
            output = (out / (name + ".log")).read_text()
            stats = [(int(h), int(m)) for h, m in re.findall(r"hits = (\d+), misses = (\d+)", output)]
            hits, misses = (sum(h for h, _ in stats), sum(m for _, m in stats))
            passed = process.returncode == 0 and re.search(r"Passed:\s+1/1", output) is not None
            if label == "default-cold":
                passed &= misses > 0
            elif label == "control-after-default":
                passed &= misses > 0 if args.expect == "separated" else hits > 0 and misses == 0
            elif label == "cache-disabled":
                passed &= not stats and not (out / f"disabled-{repetition}").exists()
            else:
                passed &= hits > 0 and misses == 0
            record = {"name": name, "knob": value, "returncode": process.returncode,
                      "hits": hits, "misses": misses, "passed": bool(passed),
                      "seconds": time.monotonic() - start}
            records.append(record)
            print(json.dumps(record), flush=True)
            if not passed:
                failures.append(name)
    result = {"ok": not failures, "expect": args.expect, "knob": args.knob,
              "case": args.case, "driver": str(driver),
              "driver_sha256": hashlib.sha256(driver.read_bytes()).hexdigest(),
              "default_cache": args.default_cache, "records": records, "failures": failures}
    (out / "results.json").write_text(json.dumps(result, indent=2) + "\n")
    return 0 if result["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
