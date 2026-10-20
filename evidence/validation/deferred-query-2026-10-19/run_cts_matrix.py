from pathlib import Path
import collections
import hashlib
import json
import os
import subprocess
import time

root = Path(__file__).resolve().parent
cts = root.parent.parent / 'cts'
query = set((root / 'query-cases.txt').read_text().splitlines())
batch = set((root / 'batch-cases.txt').read_text().splitlines())
all_cases = query | batch
(root / 'all-cases.txt').write_text('\n'.join(sorted(all_cases)) + '\n')


def read(group, expected):
    rows = [line.split('\t', 2) for line in (cts / 'runs' / group / 'results.tsv').read_text().splitlines()]
    data = {row[0]: row[1] for row in rows}
    if len(data) != len(rows) or set(data) != expected:
        raise RuntimeError(f'{group}: missing, duplicate or extra cases')
    return data


def execute(group, driver, enabled, file):
    if (cts / 'runs' / group).exists():
        raise RuntimeError(f'Output already exists: {group}')
    env = dict(os.environ, PANVK_ICD_SO=str(driver), PANVK_DEFER_OQ=str(enabled),
               PANVK_DEFER_OQ_EARLY_CONTROL='0', CTS_LOG_IMAGES='0', CTS_CHUNK='100')
    start = time.monotonic()
    with (root / (group + '.log')).open('w') as log:
        subprocess.run(['python3', str(cts / 'run_cts.py'), group, str(root / file)],
                       cwd=cts, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=900)
    return time.monotonic() - start


baseline = read('rQ19_deferred_baseline', query)
execute('rQ19_batch_baseline', root / 'host-before.so', 0, 'batch-cases.txt')
batch_baseline = read('rQ19_batch_baseline', batch)
assert all(baseline[k] == batch_baseline[k] for k in query & batch)
baseline.update(batch_baseline)
report = {'baseline_counts': dict(collections.Counter(baseline.values())),
          'cases': len(all_cases),
          'driver_sha256': hashlib.sha256((root / 'host-candidate.so').read_bytes()).hexdigest(),
          'runs': []}
for label, enabled in [('off', 0), ('on1', 1), ('on2', 1), ('on3', 1)]:
    group = 'rQ19_deferred_' + label
    seconds = execute(group, root / 'host-candidate.so', enabled, 'all-cases.txt')
    results = read(group, all_cases)
    changes = [{'case': k, 'baseline': baseline[k], 'candidate': results[k]}
               for k in sorted(all_cases) if baseline[k] != results[k]]
    record = {'group': group, 'seconds': seconds,
              'counts': dict(collections.Counter(results.values())), 'changes': changes,
              'lost': sum(x['baseline'] == 'Pass' for x in changes),
              'gain': sum(x['candidate'] == 'Pass' for x in changes)}
    report['runs'].append(record)
    (root / 'cts-comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(record), flush=True)
    if any(x['candidate'] != 'Pass' for x in changes):
        raise SystemExit(1)
