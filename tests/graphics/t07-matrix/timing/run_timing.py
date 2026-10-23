import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import tempfile
import time

fixture = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description='Run dynamic UBO matrix correctness and ABBA timing controls.')
parser.add_argument('driver', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--cc', default='/data/data/com.termux/files/usr/bin/clang')
parser.add_argument('--glslang', default='glslangValidator')
parser.add_argument('--spirv-val', default='spirv-val')
a = parser.parse_args()
helper = fixture.parent.parent / 't06-memory'
for proc in Path('/proc').iterdir():
    if not proc.name.isdigit() or int(proc.name) == os.getpid():
        continue
    try:
        args = (proc / 'cmdline').read_bytes().split(b'\0')
    except OSError:
        continue
    if args and Path(os.fsdecode(args[0])).name in ('deqp-vk', 'ninja', 'visibility_soak', 'tiler_soak'):
        raise RuntimeError('Another GPU runner/build is active: ' + proc.name)

out = a.output.resolve()
out.mkdir(parents=True)
print('Evidence: ' + str(out), flush=True)
env = {k: v for k, v in os.environ.items()
       if not k.startswith(('LD_', 'PANVK_', 'PAN_', 'MESA_', 'BIFROST_',
                            'MATRIX_', 'SCRATCH_', 'T06_', 'CTS_'))}
env['PATH'] = '/data/data/com.termux/files/usr/bin:/usr/bin:/bin'
commands = [[
    a.cc, '-O2', '-Wall', '-Wextra', '-Werror',
    '-I' + str(helper), str(fixture / 'matrix_timing.c'),
    '-o', str(out / 'matrix_timing'), '-ldl',
]]
for layout in ('row', 'column'):
    commands.extend([
        [a.glslang, '-V', '--target-env', 'vulkan1.1',
         str(fixture / (layout + '.comp')), '-o', str(out / (layout + '.spv'))],
        [a.spirv_val, '--target-env', 'vulkan1.1', str(out / (layout + '.spv'))],
    ])
for i, command in enumerate(commands):
    with (out / f'build-{i}.log').open('x') as log:
        subprocess.run(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                       check=True, timeout=120)
(out / 'build-commands.json').write_text(json.dumps(commands, indent=2) + '\n')
driver = a.driver.resolve(strict=True)
controls = dict(PANVK_ICD_SO=str(driver), PAN_I_WANT_A_BROKEN_VULKAN_DRIVER='1',
                MESA_SHADER_CACHE_DISABLE='true', PANVK_UBO_PUSH='0',
                PANVK_DEFER_OQ='0', PANVK_TILER_HEAP_INCREMENTAL='false')
env.update(controls)
paths = [driver, fixture / 'matrix_timing.c', fixture / 'row.comp',
         fixture / 'column.comp', fixture / 'run_timing.py', helper / 'import_visibility.c',
         out / 'matrix_timing', out / 'row.spv', out / 'column.spv']


def pins():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}


before = pins()
(out / 'manifest.json').write_text(json.dumps(dict(
    utc_started=datetime.datetime.now(datetime.timezone.utc).isoformat(),
    controls=controls, pins=before, order='3 blocks per layout: off,on,on,off; layout order alternates',
    clock_thermal_control='No clock locking; sysfs devfreq/thermal inaccessible',
    timing_scope='Host QueueSubmit-to-fence interval; includes submit/wait overhead',
    gameplay_benchmark=False), indent=2) + '\n')
records = []


def run(name, layout, enable, extra=None, expected=0):
    override = dict(PAN_LATE_VECTORIZE=enable) | (extra or {})
    command = [str(out / 'matrix_timing'), str(out / (layout + '.spv')), layout]
    started = time.monotonic()
    with (out / (name + '.log')).open('x') as log:
        rc = subprocess.run(command, env=env | override, stdout=log,
                            stderr=subprocess.STDOUT, timeout=180).returncode
    raw = (out / (name + '.log')).read_text()
    record = dict(name=name, layout=layout, enabled=enable, controls=override,
                  returncode=rc, expected=expected, wall_seconds=time.monotonic() - started)
    records.append(record)
    (out / 'commands.json').write_text(json.dumps(records, indent=2) + '\n')
    assert rc == expected, record
    return raw


stats = {}
for layout in ('row', 'column'):
    stats[layout] = {}
    for enable in ('false', 'true'):
        raw = run('compile-' + layout + '-' + enable, layout, enable,
                  dict(MATRIX_COMPILE_ONLY='1', BIFROST_MESA_DEBUG='statsfull,shaders'))
        metrics = {}
        for key, regex in {
            'registers': r'Work registers:\s+(\d+)',
            'code_bytes': r'Code size:\s+(\d+) bytes',
            'stack_bytes': r'Stack size:\s+(\d+) bytes',
            'spills': r'Spills/fills:\s+(\d+)/\d+',
            'fills': r'Spills/fills:\s+\d+/(\d+)',
        }.items():
            found = re.findall(regex, raw)
            assert len(found) == 1, (key, found)
            metrics[key] = int(found[0])
        assert 'no_dispatch=1' in raw
        stats[layout][enable] = metrics
(out / 'compile-stats.json').write_text(json.dumps(stats, indent=2) + '\n')
assert stats['row']['true']['code_bytes'] < stats['row']['false']['code_bytes']
assert stats['column']['true'] == stats['column']['false']


def samples(raw, negative=False):
    summary = re.findall(r'^MATRIX values=96 rounds=3 output_bad=(\d+) input_bad=(\d+) guard_bad=(\d+) verdict=(PASS|FAIL)$', raw, re.M)
    assert summary == [('48' if negative else '0', '0', '0', 'FAIL' if negative else 'PASS')], summary
    rows = re.findall(r'^SAMPLE round=(\d+) sample=(\d+) warmup=(\d+) invocations=262144 elapsed_us=([\d.]+) bad=(\d+)$', raw, re.M)
    assert len(rows) == 48
    assert [(int(r[0]), int(r[1])) for r in rows] == [(r, s) for r in range(3) for s in range(16)]
    assert all(int(r[2]) == (int(r[1]) < 4) and int(r[4]) == int(negative) and float(r[3]) > 0 for r in rows)
    return [float(r[3]) for r in rows if r[2] == '0']


samples(run('negative', 'row', 'true', dict(MATRIX_WRONG_EXPECTED='1'), expected=2), True)
measurements = []
for block in range(3):
    for layout in (('row', 'column') if block % 2 == 0 else ('column', 'row')):
        for index, enable in enumerate(('false', 'true', 'true', 'false')):
            name = f'block{block}-{layout}-{index}-{enable}'
            values = samples(run(name, layout, enable))
            item = dict(name=name, block=block, layout=layout, enabled=enable,
                        samples_us=values, median_us=statistics.median(values),
                        p10_us=sorted(values)[3], p90_us=sorted(values)[32])
            measurements.append(item)
            (out / 'measurements.json').write_text(json.dumps(measurements, indent=2) + '\n')
            print(json.dumps({k: v for k, v in item.items() if k != 'samples_us'}), flush=True)
comparisons = []
for block in range(3):
    for layout in ('row', 'column'):
        group = [m for m in measurements if m['block'] == block and m['layout'] == layout]
        medians = {enable: statistics.mean(m['median_us'] for m in group if m['enabled'] == enable)
                   for enable in ('false', 'true')}
        comparisons.append(dict(block=block, layout=layout, mean_process_median_us=medians,
                                enabled_reduction_pct=100 * (1 - medians['true'] / medians['false'])))
assert pins() == before
report = dict(correctness_passed=True, positive_processes=len(measurements),
              negative_processes=1, comparisons=comparisons, pins_unchanged=True,
              default_promoted=False, gameplay_benchmark=False,
              limitation='Uncontrolled DVFS/temperature and submit overhead; interpret column control and all blocks before claiming improvement.')
(out / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report), flush=True)
