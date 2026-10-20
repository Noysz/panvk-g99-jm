from pathlib import Path
import json
import subprocess
import time

root = Path(__file__).resolve().parent
prior = root.parent / 'gameplay-2026-10-19'
plan = [('visual-off', 0), ('ab-normal-on-1', 1), ('ab-normal-off-2', 0),
        ('ab-normal-on-2', 1), ('ab-normal-off-3', 0), ('ab-normal-on-3', 1)]
(root / 'normal-gameplay-plan.json').write_text(json.dumps({
    'plan': plan, 'declared_unix': time.time(),
    'reason': 'The initial on run was stopped without a result after a suspected loading-transition issue. The complete off comparison is included before observing its benchmark result; then alternate on/off for three pairs.'}, indent=2) + '\n')
deadline = time.monotonic() + 300
while True:
    manifest = json.loads((root / 'visual-off/manifest.json').read_text())
    if 'elapsed_seconds' in manifest:
        assert manifest['stopped_after_result'] and not manifest['failure'] and not manifest['timed_out']
        break
    if time.monotonic() > deadline:
        raise RuntimeError('First off comparison did not finish')
    time.sleep(10)
results = [{'name': 'visual-off', 'enabled': 0, 'returncode': 0}]
for name, enabled in plan[1:]:
    time.sleep(10)
    command = ['python3', str(root / 'measurement_runner.py'),
               '--out', str(root / name), '--prefix', str(prior / 'prefix'),
               '--driver', str(root / 'package'), '--dxvk', '1.7.2',
               '--ubo', '32', '--defer-oq', str(enabled),
               '--auto-benchmark', '--state', str(prior / 'initial-state'),
               '--layer-dir', str(prior / 'layer'),
               '--result-template', str(prior / 'pilot4-state/final-scene.png'),
               '--timeout', '420']
    with (root / (name + '-runner.log')).open('w') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=470)
    record = {'name': name, 'enabled': enabled, 'returncode': result.returncode, 'command': command}
    results.append(record)
    (root / 'normal-gameplay-runs.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(record), flush=True)
    if result.returncode:
        raise SystemExit(1)
