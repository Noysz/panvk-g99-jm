from pathlib import Path
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parent
repo = Path('/data/data/com.termux/files/home/panvk-g99-jm')
deadline = time.monotonic() + 3600
while True:
    try:
        comparison = json.loads((root / 'cts-comparison.json').read_text())
    except (FileNotFoundError, json.JSONDecodeError):
        comparison = {'runs': []}
    if any(not run['ok'] for run in comparison['runs']):
        raise SystemExit('CTS regression; Android checks were not started')
    if len(comparison['runs']) == 3:
        break
    if time.monotonic() > deadline:
        raise SystemExit('CTS completion not observed within one hour')
    time.sleep(5)

command = ['python3', str(repo / 'tools/verify_shader_cache.py'),
           '--cts', '/data/data/com.termux/files/home/cts',
           '--driver', str(root / 'android-candidate.so'),
           '--output', str(root / 'android-default-cache'), '--default-cache']
with (root / 'android-default-cache.log').open('w') as log:
    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
print('Android default-cache checks passed', flush=True)

with (root / 'dxvk-validation.log').open('w') as log:
    subprocess.run(['bash', '/data/data/com.termux/files/home/panvk-g57/phase4/src/verify_dxvk.sh',
                    '/data/data/com.termux/files/home/wltest/drvB19_cache_compiler',
                    str(root / 'dxvk-validation')],
                   env=dict(os.environ, ONLY='1'), stdout=log, stderr=subprocess.STDOUT, check=True)
print('DXVK validation passed', flush=True)
