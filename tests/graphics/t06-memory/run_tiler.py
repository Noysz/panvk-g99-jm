import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('driver', type=Path)
p.add_argument('label')
p.add_argument('--enabled', choices=['true', 'false'], default='true')
p.add_argument('--instances', type=int, default=1)
p.add_argument('--frames', type=int, default=6)
p.add_argument('--alternate', action='store_true')
p.add_argument('--resubmit', action='store_true')
p.add_argument('--negative', action='store_true')
p.add_argument('--inject', choices=['EINVAL', 'ENOMEM', 'strip'])
a = p.parse_args()
root = Path(__file__).resolve().parent
out = root / a.label
out.mkdir()
driver = a.driver.resolve(strict=True)
env = {k: v for k, v in os.environ.items()
       if not k.startswith(('PANVK_', 'PAN_', 'MESA_', 'LD_', 'CUBE_', 'TILERTEST_'))}
env.update(PATH='/data/data/com.termux/files/usr/bin:/usr/bin:/bin',
           PANVK_ICD_SO=str(driver), PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1',
           MESA_SHADER_CACHE_DISABLE='true', PANVK_MEM_PROF='1',
           PANVK_TILER_HEAP_INCREMENTAL=a.enabled,
           CUBE_FRAMES=str(a.frames), CUBE_DIM='64',
           TILERTEST_INSTANCES=str(a.instances),
           LD_PRELOAD=str(root / 'tiler_ioctl_probe.so'))
if a.alternate: env['TILERTEST_ALTERNATE'] = '1'
if a.resubmit: env['CUBE_RESUBMIT'] = '1'
if a.negative: env['CUBE_LIE'] = '1'
if a.inject: env['TILERTEST_INJECT'] = a.inject
start = time.monotonic()
with (out / 'run.log').open('x') as log:
    try:
        result = subprocess.run([str(root / 'tiler_test')], cwd=root, env=env,
                                stdout=log, stderr=subprocess.STDOUT, timeout=180)
        rc = result.returncode
    except subprocess.TimeoutExpired:
        rc = 'timeout'
lines = (out / 'run.log').read_text()
summary = re.findall(r'CUBESUM .+', lines)
heaps = [float(x) for x in re.findall(r'committed: tiler ([0-9.]+)', lines)]
pixels = re.findall(r'CUBEFP frame=', lines)
fault_control = a.inject == 'ENOMEM'
expected_rc = 1 if fault_control else 2 if a.negative else 0
ok = (rc == expected_rc and
      (('TILER_INJECT ENOMEM' in lines and 'FAILED: vkCreateDevice' in lines)
       if fault_control else
       (len(pixels) == a.frames and len(summary) == 1 and len(heaps) >= 2 and
        ('verdict=FAIL' if a.negative else 'verdict=PASS') in summary[0] and 'DONE' in lines)))
if a.inject:
    ok = ok and re.findall(r'^TILER_INJECT (.+)$', lines, re.M) == [a.inject]
record = {'driver': str(driver), 'sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
          'args': vars(a) | {'driver': str(a.driver)}, 'returncode': rc,
          'expected_outcome': ok, 'summary': summary, 'tiler_mb': heaps,
          'checked_frames': len(pixels), 'wall_seconds': time.monotonic() - start,
          'timing_is_benchmark': False,
          'allocations': re.findall(r'HEAP_ALLOC .+', lines)}
(out / 'result.json').write_text(json.dumps(record, indent=2) + '\n')
print(json.dumps(record), flush=True)
raise SystemExit(0 if ok else 1)
