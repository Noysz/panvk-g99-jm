#!/usr/bin/env python3
"""Validate benchmark completion and summarize a fixed window of present intervals."""

import argparse
import csv
import json
import math
from pathlib import Path
import re
import statistics


def read_present(path):
    with path.open(newline='') as stream:
        reader = csv.reader(stream)
        header = [value.strip() for value in next(reader)]
        rows = []
        for line, fields in enumerate(reader, 2):
            # Mesa overlay emits a leading comma when the device field is disabled.
            if len(fields) == len(header) + 1 and not fields[0].strip():
                fields = fields[1:]
            if len(fields) != len(header):
                raise ValueError(f'{path}:{line}: invalid column count')
            row = dict(zip(header, (int(value) for value in fields)))
            if row['frame'] < 1 or row['frame_timing(us)'] <= 0:
                raise ValueError(f'{path}:{line}: invalid frame count or interval')
            rows.append(row)
    if not rows:
        raise ValueError(f'{path}: no presents')
    return rows


def analyze(path, start, duration, variable='PANVK_UBO_PUSH'):
    if variable not in ('PANVK_UBO_PUSH', 'PANVK_DEFER_OQ'):
        raise ValueError('Unsupported comparison variable')
    manifest = json.loads((path / 'manifest.json').read_text())
    if any((path / name).exists() for name in ('excluded.json', 'manual-actions.json')):
        raise ValueError(f'{path}: excluded or manually steered run')
    controls = manifest['controls']
    if ('VK_LAYER_LUNARG_api_dump' in controls.get('VK_INSTANCE_LAYERS', '') or
            any(controls.get(key, '0').lower() not in ('0', 'false', 'off', 'no', '')
                for key in ('PANVK_KBASE_SYNC_SUBMIT', 'PANVK_KBASE_WAIT_SUBMIT',
                            'PANVK_DEFER_OQ_EARLY_CONTROL'))):
        raise ValueError(f'{path}: diagnostic profiling is not a normal-mode benchmark')
    if (not manifest.get('stopped_after_result') or manifest.get('timed_out')
            or manifest.get('failure')):
        raise ValueError(f'{path}: no completed benchmark')
    action, = [item for item in manifest['actions'] if item['action'] == 'BENCHMARK']
    anchor = action['present_lines'] - 1
    rows = read_present(path / 'present.csv')
    shots = manifest['screenshots']
    counts = [s['present_lines'] for s in shots if 'present_lines' in s
              and s['elapsed_seconds'] >= action['elapsed_seconds']]
    if counts != sorted(counts) or not 0 < anchor < len(rows):
        raise ValueError(f'{path}: present log reset or invalid benchmark anchor')
    clock = 0
    intervals = []
    selected_rows = []
    for index, row in enumerate(rows[anchor:], anchor):
        previous = clock
        clock += row['frame_timing(us)'] / 1e6
        if previous >= start and clock <= start + duration:
            if row['frame'] != 1:
                raise ValueError(f'{path}: grouped frames inside measurement window')
            intervals.append(row['frame_timing(us)'] / 1000)
            selected_rows.append(index)
    measured = sum(intervals) / 1000
    if len(intervals) < 30 or measured < duration * .95:
        raise ValueError(f'{path}: incomplete measurement window ({measured:.2f}s)')
    text = (path / 'result-ocr.txt').read_text()
    game_stats = {}
    for label in ('MIN', 'MAX', 'AVERAGE'):
        match = re.search(rf'{label}\s+FPS\s*:\s*(\d+(?:[.,]\d+)?)', text)
        if not match:
            raise ValueError(f'{path}: unreadable {label} FPS; inspect result.png')
        game_stats[label.lower()] = float(match[1].replace(',', '.'))
    if not 0 < game_stats['min'] <= game_stats['average'] <= game_stats['max']:
        raise ValueError(f'{path}: inconsistent benchmark result')
    sizes = re.findall(r'Buffer size:\s+(\d+x\d+)', (path / 'process.log').read_text())
    if not sizes or sizes[-1] != '848x480':
        raise ValueError(f'{path}: final swapchain is not 848x480')
    stack = {Path(key).name: value for key, value in manifest['sha256'].items()}
    if len(stack) != len(manifest['sha256']):
        raise ValueError(f'{path}: ambiguous artifact names')
    controls = {key: value for key, value in manifest['controls'].items()
                if key not in {variable, 'DXVK_LOG_PATH',
                               'VK_ICD_FILENAMES', 'PANVK_FRAME_PROF'}}
    controls['VK_LAYER_MESA_OVERLAY_CONFIG'] = re.sub(
        r'output_file=[^,]+', 'output_file=<run>', controls['VK_LAYER_MESA_OVERLAY_CONFIG'])
    values = sorted(intervals)
    slow_count = max(1, math.ceil(len(values) * .01))
    return {
        'run': path.name, 'ubo_words': int(manifest['controls']['PANVK_UBO_PUSH']),
        'defer_occlusion': int(manifest['controls'].get('PANVK_DEFER_OQ', '0')),
        'game_reported_fps': game_stats,
        'present_window': {
            'seconds_after_benchmark_action': [start, start + duration],
            'anchor': 'last logged present at recorded action; input delivery/recognition occurs later',
            'frames': len(values), 'seconds': measured,
            'first_csv_line': selected_rows[0] + 2, 'last_csv_line': selected_rows[-1] + 2,
            'fps': len(values) / measured, 'mean_ms': statistics.mean(values),
            'median_ms': statistics.median(values),
            'p99_ms': values[math.ceil(len(values) * .99) - 1],
            'slowest_one_percent_mean_fps': 1000 / statistics.mean(values[-slow_count:]),
            'worst_ms': values[-1],
        },
        'driver_sha256': next(value for key, value in manifest['sha256'].items()
                              if key.endswith('/libvulkan_panfrost.so')),
        'state_sha256': manifest['initial_state_sha256'],
        'stack_sha256': stack, 'shared_controls': controls,
        'visual_check_required': True,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--start', type=float, default=45)
    parser.add_argument('--duration', type=float, default=35)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--variable', choices=('PANVK_UBO_PUSH', 'PANVK_DEFER_OQ'),
                        default='PANVK_UBO_PUSH')
    args = parser.parse_args()
    if args.start < 0 or args.duration <= 0:
        parser.error('start must be nonnegative and duration positive')
    records = []
    errors = []
    for path in args.runs:
        try:
            records.append(analyze(path, args.start, args.duration, args.variable))
        except (OSError, ValueError, KeyError, StopIteration) as exc:
            errors.append(f'{path}: {exc}')
    if records and any(any(r[key] != records[0][key] for key in
                           ('stack_sha256', 'state_sha256', 'shared_controls'))
                       for r in records):
        errors.append('Stack, initial state or shared controls differ between runs')
    result = {'ok': not errors, 'runs': records, 'errors': errors, 'variable': args.variable,
              'scope': 'Present-call intervals, not scanout latency or GPU execution time. '
                       'Window statistics exclude startup; game results cover the built-in camera path. '
                       'Inspect screenshots before making a performance claim.'}
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0 if result['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
