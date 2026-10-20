from pathlib import Path
import collections
import hashlib
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parent
cts = Path('/data/data/com.termux/files/home/cts')
casefile = root / 'compiler-cases.txt'
expected = set(casefile.read_text().splitlines())


def results(group):
    rows = [line.split('\t', 2) for line in (cts / 'runs' / group / 'results.tsv').read_text().splitlines()]
    data = {row[0]: row[1] for row in rows}
    if len(data) != len(rows) or set(data) != expected:
        raise RuntimeError(f'{group}: duplicate, missing or unexpected cases')
    return data


baseline = results('rB19_compiler_baseline')
driver = root / 'host-candidate.so'
env = dict(os.environ, PANVK_ICD_SO=str(driver), CTS_LOG_IMAGES='0', CTS_CHUNK='100')
report = {'baseline': 'rB19_compiler_baseline',
          'baseline_counts': dict(collections.Counter(baseline.values())),
          'driver_sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
          'cases_sha256': hashlib.sha256(casefile.read_bytes()).hexdigest(), 'runs': []}
for number in range(1, 4):
    group = f'rB19_compiler_candidate{number}'
    if (cts / 'runs' / group).exists():
        raise RuntimeError(f'{group} already exists')
    start = time.monotonic()
    with (root / f'cts-candidate{number}.log').open('w') as log:
        subprocess.run(['python3', str(cts / 'run_cts.py'), group, str(casefile)],
                       cwd=cts, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
    candidate = results(group)
    changes = [{'case': name, 'baseline': old, 'candidate': candidate[name]}
               for name, old in baseline.items() if old != candidate[name]]
    lost = [x for x in changes if x['baseline'] == 'Pass']
    gained = [x for x in changes if x['candidate'] == 'Pass']
    unexpected = [x for x in changes if x['candidate'] != 'Pass']
    record = {'group': group, 'cases': len(candidate),
              'counts': dict(collections.Counter(candidate.values())),
              'lost': len(lost), 'gain': len(gained), 'changes': changes,
              'seconds': time.monotonic() - start, 'ok': not unexpected}
    report['runs'].append(record)
    (root / 'cts-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(record), flush=True)
    if not record['ok']:
        raise SystemExit(1)
