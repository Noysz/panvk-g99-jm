import hashlib
import json
import math
from pathlib import Path
import re
import statistics
import sys


def analyze(run):
    row = json.loads((run / 'result.json').read_text())
    assert row['completed'] and row['cleanup']['closed'] and row['mode'] == 'measure'
    c = row['controls']
    assert c['DXVK_HUD'] == '' and c['PANVK_UBO_PUSH'] == '0' and c['PANVK_DEFER_OQ'] == '0'
    assert c['MESA_SHADER_CACHE_DISABLE'] == 'true' and c['DXVK_STATE_CACHE'] == '0'
    assert not any(k.startswith('IOLOG_') or k in ('PANVK_FRAME_PROF', 'BIFROST_MESA_DEBUG') for k in c)
    log = (run / 'NSUNS2_d3d11.log').read_text() + (run / 'process.log').read_text()
    sizes = re.findall(r'Buffer size:\s*(\d+x\d+)', log)
    assert sizes and set(sizes) == {'640x360'}, sizes
    for option in ['dxvk.enableAsync = False', 'dxvk.enableGraphicsPipelineLibrary = True', 'DXVK: v2.3.1-gplasync']:
        assert option in log, option
    begin, end = row['window_start'], row['window_end']
    duration = end['monotonic'] - begin['monotonic']
    assert 90 <= duration <= 92, duration
    data = (run / 'present.csv').read_bytes()
    for pos in (begin['offset'], end['offset']):
        assert data[pos - 1:pos] == b'\n'
    selected = data[begin['offset']:end['offset']].decode().splitlines()[1:]
    rows = []
    for line in selected:
        cells = [c.strip() for c in line.split(',')]
        assert len(cells) == 7 and cells[0] == '', cells
        values = [int(c) for c in cells[1:]]
        assert values[0] == 1 and values[1] > 0
        rows.append(values)
    assert len(rows) > 100
    intervals = [r[1] for r in rows]
    measured = sum(intervals) / 1e6
    assert abs(measured - duration) < 1, (measured, duration)
    assert all(r[3] + r[4] > 50 for r in rows), 'Scene may have changed'
    gate = row['scene_gate']
    assert hashlib.sha256((run / gate['screenshot']).read_bytes()).hexdigest() == gate['sha256']
    assert row['pins_before'] == row['pins_after']
    assert row['original_state_before'] == row['original_state_after']
    samples = [s for s in row['system_samples'] if s['phase'] == 'measurement']
    clocks = {}
    for cpu in (0, 6):
        vals = [s['cpu%d_khz' % cpu] for s in samples if s['cpu%d_khz' % cpu] is not None]
        clocks[str(cpu)] = {'min': min(vals), 'median': statistics.median(vals), 'max': max(vals)} if vals else None
    return {'run': run.name, 'clip_kill_fpk': int(c['PANVK_V9_CLIP_KILL_FPK']),
            'frames': len(rows), 'interval_seconds': measured, 'wall_seconds': duration,
            'fps': len(rows) / measured, 'frame_ms_median': statistics.median(intervals) / 1000,
            'frame_ms_p95': sorted(intervals)[math.ceil(.95 * len(intervals)) - 1] / 1000,
            'draws_min_median_max': [min(r[3] + r[4] for r in rows), statistics.median(r[3] + r[4] for r in rows), max(r[3] + r[4] for r in rows)],
            'cpu_clocks_khz': clocks, 'csv_sha256': hashlib.sha256(data).hexdigest(),
            'csv_header': data.splitlines()[0].decode(), 'swapchain_sizes': sorted(set(sizes)),
            'runtime_hashes': row['runtime_hashes'], 'initial_state': row['restored_state']}


if __name__ == '__main__':
    print(json.dumps([analyze(Path(arg)) for arg in sys.argv[1:]], indent=2))
