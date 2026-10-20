from pathlib import Path
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parent
output = root / 'oracle/empty'
deadline = time.monotonic() + 600
while True:
    report = json.loads((root / 'cts-comparison.json').read_text())
    if len(report['runs']) == 4:
        assert all(not run['changes'] for run in report['runs'])
        break
    if time.monotonic() > deadline:
        raise RuntimeError('CTS not complete')
    time.sleep(10)
records = []
for label, driver, enabled in [('validation', None, 0),
                               ('baseline', root / 'host-before.so', 0),
                               ('host-on', root / 'host-candidate.so', 1),
                               ('package-off', root / 'package/libvulkan_panfrost.so', 0),
                               ('package-on', root / 'package/libvulkan_panfrost.so', 1)]:
    env = dict(os.environ, MESA_SHADER_CACHE_DISABLE='true')
    if driver is None:
        env.update(json.loads((root.parent / 'gameplay-2026-10-19/occlusion-oracle/validation-env.json').read_text()))
    else:
        env.update(LD_LIBRARY_PATH='/data/data/com.termux/files/usr/lib',
                   PANVK_ICD_SO=str(driver), PANVK_DEFER_OQ=str(enabled),
                   PANVK_DEFER_OQ_EARLY_CONTROL='0', PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1')
    command = [str(output / ('oracle-host' if driver is None else 'oracle')), '--empty-outside']
    with (output / (label + '.log')).open('w') as log:
        result = subprocess.run(command, cwd=output, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
    text = (output / (label + '.log')).read_text()
    ok = result.returncode == 0 and 'VUID-' not in text and 'SYNC-HAZARD' not in text
    ok &= text.count('OQ_EMPTY_OUTSIDE cycle=') == 3
    record = {'label': label, 'command': command, 'returncode': result.returncode, 'ok': ok}
    records.append(record)
    (output / 'runs.json').write_text(json.dumps(records, indent=2) + '\n')
    print(json.dumps(record), flush=True)
    if not ok:
        raise SystemExit(1)

env = dict(os.environ, MESA_SHADER_CACHE_DISABLE='true',
           LD_LIBRARY_PATH='/data/data/com.termux/files/usr/lib',
           PANVK_ICD_SO=str(root / 'host-before.so'), PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1')
with (output / 'baseline-lifetime.log').open('w') as log:
    result = subprocess.run([str(output / 'oracle'), '--lifetime'], cwd=output,
                            env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
(output / 'baseline-lifetime.json').write_text(json.dumps({'returncode': result.returncode}) + '\n')
print('baseline lifetime exit', result.returncode, flush=True)
