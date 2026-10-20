#!/usr/bin/env python3
"""Run the installed TR benchmark with isolated output and a dedicated prefix."""

import argparse
import csv
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import time

from PIL import Image, ImageChops, ImageStat


TERMUX = Path('/data/data/com.termux/files/home')
ROOT = TERMUX / 'hangover/root/data/data/com.termux/files/usr'
WINE = ROOT / 'opt/hangover-wine/bin'
LIBS = TERMUX / 'wltest/imagefs/usr/lib'


def digest(path):
    with open(path, 'rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def words_in(image, crop):
    target = image.with_suffix('.ocr.png')
    with Image.open(image) as source:
        source.crop(crop).resize(((crop[2] - crop[0]) * 2,
                                 (crop[3] - crop[1]) * 2)).save(target)
    result = subprocess.run(['tesseract', str(target), '-', '--psm', '6', 'tsv'],
        env={**os.environ, 'OMP_THREAD_LIMIT': '1'}, capture_output=True,
        text=True, timeout=20, check=True)
    return [dict(row, x=crop[0] + (int(row['left']) + int(row['width']) / 2) / 2,
                 y=crop[1] + (int(row['top']) + int(row['height']) / 2) / 2)
            for row in csv.DictReader(result.stdout.splitlines(), delimiter='\t')
            if row['text'].strip()]


def activate(x, y, keyboard=False, evidence_dir=None):
    env = {**os.environ, 'DISPLAY': ':0'}
    subprocess.run(['xdotool', 'mousemove', str(round(x)), str(round(y))],
                   env=env, check=True)
    if keyboard:
        if evidence_dir is None:
            raise ValueError('Menu selection requires an evidence directory')
        for attempt in range(4):
            time.sleep(1.5)
            target = evidence_dir / f'menu-hover-{attempt}.png'
            subprocess.run(['import', '-window', 'root', str(target)],
                           env=env, check=True, timeout=15)
            title = ' '.join(w['text'].upper() for w in
                             words_in(target, (460, 130, 770, 185)))
            if 'START' in title and 'BENCHMARK' in title:
                break
            words = words_in(target, (110, 140, 350, 330))
            match = next((w for w in words if w['text'].upper() == 'BENCHMARK'), None)
            if match:
                subprocess.run(['xdotool', 'mousemove', str(round(match['x'])),
                                str(round(match['y']))], env=env, check=True)
        else:
            raise RuntimeError('Could not verify START BENCHMARK selection')
    time.sleep(.3)
    down, up, key = ('keydown', 'keyup', 'Return') if keyboard else (
        'mousedown', 'mouseup', '1')
    stamp = {'unix': time.time(), 'monotonic': time.monotonic()}
    if evidence_dir is not None:
        present = evidence_dir / 'present.csv'
        if present.exists():
            stamp['present_lines'] = len(present.read_text().splitlines())
    try:
        subprocess.run(['xdotool', down, key], env=env, check=True)
        time.sleep(.4)
    finally:
        subprocess.run(['xdotool', up, key], env=env, check=True)
    return stamp


def result_dialog(image, template):
    with Image.open(image) as actual, Image.open(template) as expected:
        if actual.size != expected.size:
            return False
        for rect in ((250, 159, 420, 175), (250, 283, 363, 316)):
            delta = ImageChops.difference(actual.convert('RGB').crop(rect),
                                         expected.convert('RGB').crop(rect))
            if max(ImageStat.Stat(delta).mean) >= 8:
                return False
    return True


def run(args):
    out = args.out.resolve()
    prefix = args.prefix.resolve()
    if prefix == (TERMUX / 'wltest/wpfx').resolve():
        raise SystemExit('Use a dedicated copy of the Wine prefix.')
    if not (prefix / 'system.reg').is_file():
        raise SystemExit('The dedicated prefix is not initialized.')
    out.mkdir(parents=True, exist_ok=False)
    lock = open(prefix.parent / (prefix.name + '.benchmark.lock'), 'w')
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    state_hashes = {}
    if args.state:
        state = json.loads((args.state / 'state.json').read_text())
        for name, expected in state['sha256'].items():
            actual = digest(args.state / name)
            if actual != expected:
                raise SystemExit(f'Initial state changed: {name}')
            state_hashes[name] = actual
        shutil.copy2(args.state / 'user.reg', prefix / 'user.reg')
        shutil.copytree(args.state / 'SaveGames', prefix / state['save_relative'],
                        dirs_exist_ok=True)
    game = out / 'game'
    game.mkdir()
    for src in (TERMUX / 'trde').iterdir():
        if src.suffix.lower() in {'.exe', '.dll', '.json', '.xml'}:
            shutil.copy2(src, game / src.name)
        elif (src.name.startswith('Priority0') or src.name == 'movies'
              or src.suffix.lower() in {'.png', '.config'}):
            (game / src.name).symlink_to(src.resolve())
    config = '''QualityLevel=0
Fullscreen=1
ExclusiveFullscreen=1
AspectRatio=0
VSyncMode=0
FullscreenWidth=848
FullscreenHeight=480
FullscreenRefreshRate=60
StereoEnabled=0
TextureQuality=2
BestTextureFilter=1
HairQuality=0
AntiAliasingMode=0
ShadowMode=0
ShadowResolution=0
SSAOMode=0
DOFQuality=0
ReflectionQuality=0
LODBias=0
EnablePostProcess=0
EnableHighPrecisionRT=0
EnableTessellation=0
'''
    (game / 'bench.ini').write_text(config)
    dlls = Path('/tmp/dxvk') / args.dxvk / 'system32'
    hashes = {}
    for name in ('d3d11', 'dxgi', 'd3d9', 'd3d10core', 'd3d8'):
        src = dlls / (name + '.dll')
        if src.is_file():
            shutil.copy2(src, prefix / 'drive_c/windows/system32' / src.name)
            hashes[str(src)] = digest(src)
    driver = args.driver.resolve() / 'libvulkan_panfrost.so'
    for src in (driver, LIBS / 'libvulkan_wrapper.so', game / 'TombRaider.exe',
                game / 'bench.ini', Path(__file__).resolve()):
        hashes[str(src)] = digest(src)
    if args.result_template:
        hashes[str(args.result_template.resolve())] = digest(args.result_template)
    icd = out / 'icd.json'
    icd.write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
        'library_path': str(LIBS / 'libvulkan_wrapper.so'),
        'api_version': '1.3.303'}}))
    controls = {
        'WINEPREFIX': str(prefix), 'WINEDEBUG': '-all', 'DISPLAY': ':0',
        'LD_LIBRARY_PATH': f'{ROOT}/lib:{LIBS}',
        'VK_ICD_FILENAMES': str(icd),
        'ADRENOTOOLS_DRIVER_PATH': str(driver.parent) + '/',
        'ADRENOTOOLS_HOOKS_PATH': str(LIBS) + '/',
        'ADRENOTOOLS_DRIVER_NAME': driver.name,
        'WINEDLLOVERRIDES': 'd3d11,dxgi,d3d9,d3d10core,d3d8=n',
        'DXVK_LOG_LEVEL': 'info', 'DXVK_LOG_PATH': str(out),
        'DXVK_STATE_CACHE': '0', 'DXVK_HUD': '',
        'MESA_SHADER_CACHE_DISABLE': 'true',
        'PANVK_UBO_PUSH': str(args.ubo), 'PANVK_V9_CLIP_KILL_FPK': '0',
        'PANVK_FRAME_PROF': str(out / 'frames.log'),
        'PANVK_FRAME_PROF_PASSES': '1',
    }
    if args.layer_dir:
        controls.update({
            'VK_LAYER_PATH': str(args.layer_dir.resolve()),
            'VK_INSTANCE_LAYERS': 'VK_LAYER_MESA_overlay',
            'VK_LAYER_MESA_OVERLAY_CONFIG':
                f'no_display=1,device=0,format=0,fps=0,frame=1,frame_timing=1,'
                f'present_timing=1,submit=1,draw=1,draw_indexed=1,'
                f'fps_sampling_period=0,output_file={out}/present.csv',
        })
        for src in args.layer_dir.iterdir():
            if src.is_file():
                hashes[str(src.resolve())] = digest(src)
    env = {k: v for k, v in os.environ.items() if not k.startswith(
        ('PANVK_', 'PAN_', 'pan_', 'MESA_', 'DXVK_', 'VK_', 'ADRENOTOOLS_'))}
    env.pop('LD_PRELOAD', None)
    controls.update(json.loads(Path(os.environ['PANVK_BENCH_PROFILE_CONTROLS']).read_text()))
    env.update(controls)
    command = [str(WINE / 'wine'), str(game / 'TombRaider.exe'),
               '-benchmark', '-benchmarkini', 'bench.ini']
    if args.launch_only or args.auto_benchmark:
        command = command[:2]
    manifest = {'command': command, 'cwd': str(game), 'controls': controls,
                'sha256': hashes, 'timeout_seconds': args.timeout,
                'start_unix': time.time(), 'screenshots': [],
                'initial_state_sha256': state_hashes, 'actions': [], 'system_samples': [],
                'launch_mode': 'menu' if args.auto_benchmark else
                               'manual' if args.launch_only else 'command-line'}
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    started = time.monotonic()
    timed_out = False
    completed = False
    result_source = None
    failure = None
    phase = 'launcher'
    with (out / 'process.log').open('w') as log:
        proc = subprocess.Popen(command, env=env, cwd=game, stdout=log,
                                stderr=subprocess.STDOUT)
        try:
            next_shot = 15
            next_system = 0
            while proc.poll() is None:
                elapsed = time.monotonic() - started
                if elapsed >= next_system:
                    sample = {'elapsed_seconds': elapsed}
                    for cpu in (0, 6):
                        path = Path(f'/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_cur_freq')
                        try:
                            sample[f'cpu{cpu}_khz'] = int(path.read_text())
                        except (OSError, ValueError):
                            sample[f'cpu{cpu}_khz'] = None
                    manifest['system_samples'].append(sample)
                    next_system += 5
                if list(game.glob('benchmarkresults*.txt')):
                    completed = True
                    result_source = 'file'
                    subprocess.run(['import', '-window', 'root', str(out / 'result.png')],
                                   env={**os.environ, 'DISPLAY': ':0'},
                                   stdout=log, stderr=log, timeout=15, check=True)
                    break
                if elapsed >= args.timeout:
                    timed_out = True
                    break
                if elapsed >= next_shot:
                    name = f'screen-{int(elapsed):04d}.png'
                    subprocess.run(['import', '-window', 'root', str(out / name)],
                                   env={**os.environ, 'DISPLAY': ':0'},
                                   stdout=log, stderr=log, timeout=15, check=False)
                    shot = {'file': name, 'capture_begin_seconds': elapsed,
                            'elapsed_seconds': time.monotonic() - started}
                    present = out / 'present.csv'
                    if present.exists():
                        shot['present_lines'] = len(present.read_text().splitlines())
                    manifest['screenshots'].append(shot)
                    print(f'{out.name}: screenshot at {elapsed:.1f}s', flush=True)
                    if (phase == 'benchmark' and args.result_template and
                            result_dialog(out / name, args.result_template)):
                        words = words_in(out / name, (240, 150, 615, 240))
                        result_text = ' '.join(w['text'] for w in words).upper()
                        (out / 'result-ocr.txt').write_text(result_text + '\n')
                        if all(w in result_text for w in ('BENCHMARK', 'STATISTICS', 'AVERAGE', 'FPS')):
                            shutil.copy2(out / name, out / 'result.png')
                            completed = True
                            result_source = 'dialog'
                            break
                    if args.auto_benchmark and phase != 'benchmark':
                        crop = (680, 180, 810, 240) if phase == 'launcher' else (
                            110, 140, 350, 330)
                        words = words_in(out / name, crop)
                        wanted = 'PLAY' if phase == 'launcher' else 'BENCHMARK'
                        match = next((w for w in words if w['text'].upper() == wanted), None)
                        if match:
                            action = {'action': wanted, 'screenshot': name,
                                      'x': match['x'], 'y': match['y']}
                            stamp = activate(match['x'], match['y'],
                                             keyboard=phase == 'menu', evidence_dir=out)
                            action.update(stamp)
                            action['elapsed_seconds'] = action.pop('monotonic') - started
                            manifest['actions'].append(action)
                            phase = 'menu' if phase == 'launcher' else 'benchmark'
                            (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
                            print(f'{out.name}: activated {wanted}', flush=True)
                    next_shot += 15
                time.sleep(1)
        except Exception as exc:
            failure = repr(exc)
        finally:
            subprocess.run([str(WINE / 'wineserver'), '-k'], env=env,
                           stdout=log, stderr=log, timeout=20, check=False)
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                proc.terminate()
                proc.wait(timeout=10)
    results = sorted(game.glob('benchmarkresults*.txt'))
    for src in results:
        shutil.copy2(src, out / src.name)
    manifest.update({'elapsed_seconds': time.monotonic() - started,
                     'exit_code': proc.returncode, 'timed_out': timed_out,
                     'stopped_after_result': completed,
                     'result_source': result_source,
                     'failure': failure,
                     'results': [p.name for p in results]})
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({k: manifest[k] for k in
        ('elapsed_seconds', 'exit_code', 'timed_out', 'results')}), flush=True)
    return 0 if not failure and not timed_out and (completed or (results and proc.returncode == 0)) else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--prefix', type=Path, required=True)
    parser.add_argument('--driver', type=Path, required=True)
    parser.add_argument('--dxvk', default='1.7.2')
    parser.add_argument('--ubo', type=int, choices=(0, 32), default=32)
    parser.add_argument('--timeout', type=int, default=420)
    parser.add_argument('--launch-only', action='store_true')
    parser.add_argument('--auto-benchmark', action='store_true')
    parser.add_argument('--state', type=Path)
    parser.add_argument('--layer-dir', type=Path)
    parser.add_argument('--result-template', type=Path)
    raise SystemExit(run(parser.parse_args()))
