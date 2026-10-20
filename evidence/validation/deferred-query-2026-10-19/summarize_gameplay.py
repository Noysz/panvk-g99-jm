from pathlib import Path
import json
import statistics

root = Path(__file__).resolve().parent
analysis = json.loads((root / 'normal-analysis.json').read_text())
assert analysis['ok'] and len(analysis['runs']) == 6
runs = analysis['runs']
pairs = []
for i in range(0, 6, 2):
    off, on = runs[i:i + 2]
    assert off['defer_occlusion'] == 0 and on['defer_occlusion'] == 1
    a, b = [run['game_reported_fps']['average'] for run in (off, on)]
    pairs.append({'off': off['run'], 'on': on['run'], 'off_fps': a, 'on_fps': b,
                  'on_minus_off_fps': b - a, 'on_relative_change_percent': (b / a - 1) * 100})
groups = {}
for state, name in ((0, 'off'), (1, 'on')):
    selected = [run for run in runs if run['defer_occlusion'] == state]
    groups[name] = {
        'game_average_fps': [run['game_reported_fps']['average'] for run in selected],
        'median_game_average_fps': statistics.median(run['game_reported_fps']['average'] for run in selected),
        'median_window_fps': statistics.median(run['present_window']['fps'] for run in selected),
        'median_window_p99_ms': statistics.median(run['present_window']['p99_ms'] for run in selected),
    }
result = {'pairs': pairs, 'groups': groups,
          'primary_metric': 'Average FPS reported by the complete built-in camera benchmark.',
          'secondary_metric_limit': 'The present window uses an approximate menu-action anchor. Variable loading shifts the camera phase; it is not a frame-exact comparison.'}
(root / 'normal-summary.json').write_text(json.dumps(result, indent=2) + '\n')
logs = []
for run in runs:
    lines = (root / run['run'] / 'frames.log').read_text().splitlines()
    split = [line for line in lines if '(split)' in line]
    logs.append({'run': run['run'], 'defer_occlusion': run['defer_occlusion'],
                 'printed_split_group_rows': len(split), 'first_split_rows': split[:3],
                 'truncated_group_reports': sum('more groups' in line for line in lines)})
(root / 'pass-group-summary.json').write_text(json.dumps({
    'scope': 'Printed top-group rows over each full process lifetime; not pass or frame counts. Group truncation prevents inferring zero global splits. These are not GPU hardware timings.',
    'runs': logs}, indent=2) + '\n')
print(json.dumps(result, indent=2))
