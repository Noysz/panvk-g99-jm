import hashlib
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parent
output = root / 'oracle/final'
validation_env = json.loads((root.parent / 'gameplay-2026-10-19/occlusion-oracle/validation-env.json').read_text())
report = []


def run(name, driver, enabled, args, expected=0, early=False):
    env = dict(os.environ, MESA_SHADER_CACHE_DISABLE='true')
    for key in ('PANVK_DEFER_OQ', 'PANVK_DEFER_OQ_EARLY_CONTROL'):
        env.pop(key, None)
    if driver is None:
        env.update(validation_env)
    else:
        env.update(LD_LIBRARY_PATH='/data/data/com.termux/files/usr/lib',
                   PANVK_ICD_SO=str(driver), PANVK_DEFER_OQ=str(enabled),
                   PANVK_DEFER_OQ_EARLY_CONTROL=str(int(early)),
                   PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1')
    command = [str(output / ('oracle-host' if driver is None else 'oracle')), *args]
    with (output / (name + '.log')).open('w') as log:
        result = subprocess.run(command, cwd=output, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=120)
    lines = (output / (name + '.log')).read_text().splitlines()
    errors = [line for line in lines if 'VUID-' in line or 'SYNC-HAZARD' in line]
    ok = result.returncode == expected and not errors
    if driver is None:
        ok &= any('Insert instance layer "VK_LAYER_KHRONOS_validation"' in line for line in lines)
    if early:
        ok &= sum(line.startswith('OQ_POLL ') and 'verdict=FAIL' in line for line in lines) == 3
        ok &= 'failures=3 verdict=FAIL' in lines[-1]
    elif expected == 2 and '--early-copy-control' in args:
        ok &= 'failures=12 verdict=FAIL' in lines[-1]
    record = {'name': name, 'command': command, 'returncode': result.returncode,
              'last': lines[-1:], 'validation_errors': errors, 'ok': bool(ok),
              'driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest() if driver else None}
    report.append(record)
    (output / 'runs.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(record), flush=True)
    if not ok:
        raise SystemExit(1)


for label, driver, enabled in [('validation', None, 0),
                               ('host-baseline', root / 'host-before.so', 0),
                               ('host-off', root / 'host-candidate.so', 0),
                               ('host-on', root / 'host-candidate.so', 1),
                               ('android-off', root / 'android-candidate.so', 0),
                               ('android-on', root / 'android-candidate.so', 1)]:
    for secondary in (False, True):
        for mode in ('positive', 'lifetime', 'simultaneous', 'poll-availability', 'early-copy-control'):
            if label == 'host-baseline' and mode == 'lifetime':
                continue
            name = label + ('-secondary-' if secondary else '-primary-') + mode
            args = (['--secondary'] if secondary else []) + (['--' + mode] if mode != 'positive' else [])
            run(name, driver, enabled, args, 2 if mode == 'early-copy-control' else 0)
    if enabled:
        for repeat in range(1, 4):
            for secondary in (False, True):
                args = ['--secondary'] if secondary else []
                prefix = f'{label}-r{repeat}-' + ('secondary-' if secondary else 'primary-')
                run(prefix + 'allocation-failure', driver, enabled, [*args, '--allocation-failure'])
                run(prefix + 'premature-control', driver, enabled, [*args, '--poll-availability'], 2, True)
