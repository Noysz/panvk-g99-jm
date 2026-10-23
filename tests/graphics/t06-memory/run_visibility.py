import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('driver', type=Path)
p.add_argument('label')
p.add_argument('--candidate', action='store_true')
a = p.parse_args()
root = Path(__file__).resolve().parent
out = root / a.label
out.mkdir()
driver = a.driver.resolve(strict=True)
env = {k: v for k, v in os.environ.items()
       if not k.startswith(('PANVK_', 'PAN_', 'MESA_', 'LD_', 'VISIBILITY_'))}
env.update(PATH='/data/data/com.termux/files/usr/bin:/usr/bin:/bin',
           PANVK_ICD_SO=str(driver), PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1',
           MESA_SHADER_CACHE_DISABLE='true')
report = {'driver': str(driver), 'sha256': hashlib.sha256(driver.read_bytes()).hexdigest(),
          'test_sha256': hashlib.sha256((root / 'import_visibility_v2').read_bytes()).hexdigest(),
          'runs': []}
cases = [('native', 'coherent'), ('native', 'cached'), ('export', 'coherent'),
         ('export', 'cached'), ('import', 'coherent'), ('import', 'cached'),
         ('system', 'coherent'), ('system', 'cached'), ('system', 'device'),
         ('system-uncached', 'coherent'), ('system-uncached', 'cached')]
for repeat in range(1, 4):
    for mode, kind in cases:
        baseline_rc = (1 if mode in ('export', 'import') else
                       2 if (mode, kind) == ('system', 'coherent') else
                       77 if mode in ('system', 'system-uncached') and kind == 'cached' else 0)
        candidate_rc = (77 if (mode, kind) in [('system', 'coherent'),
                                              ('system-uncached', 'cached')] else 0)
        expected = candidate_rc if a.candidate else baseline_rc
        logpath = out / f'r{repeat}-{mode}-{kind}.log'
        with logpath.open('x') as log:
            try:
                completed = subprocess.run([str(root / 'import_visibility_v2'), mode, kind],
                                           cwd=root, env=env, stdout=log,
                                           stderr=subprocess.STDOUT, timeout=60)
                rc = completed.returncode
            except subprocess.TimeoutExpired:
                rc = 'timeout'
        content = logpath.read_text()
        summary = re.findall(r'VISIBILITY .+', content)
        expected_outcome = rc == expected
        if expected == 0:
            expected_outcome &= len(summary) == 1 and 'verdict=PASS' in summary[0]
            if mode in ('export', 'import'):
                expected_outcome &= 'EXPORTED_FD verified' in content
        elif expected == 2:
            expected_outcome &= len(summary) == 1 and 'verdict=FAIL' in summary[0]
        elif expected == 77:
            expected_outcome &= 'UNSUPPORTED memory' in content
        else:
            expected_outcome &= 'GetMemoryFdKHR' in content and ': -2 at' in content
        record = {'repeat': repeat, 'mode': mode, 'kind': kind, 'returncode': rc,
                  'expected_returncode': expected, 'expected_outcome': expected_outcome,
                  'summary': summary, 'fd_bits': re.findall(r'fd memoryTypeBits=0x\w+', content)}
        report['runs'].append(record)
        (out / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(record), flush=True)
        if not expected_outcome:
            raise SystemExit(1)
