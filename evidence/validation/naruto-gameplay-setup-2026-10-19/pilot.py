import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

root = Path(__file__).resolve().parent
home = root.parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--name', required=True)
parser.add_argument('--clip-kill-fpk', choices=('0', '1'), default='0')
parser.add_argument('--timeout', type=int, default=600)
args = parser.parse_args()
out = root / args.name
out.mkdir(exist_ok=False)
lock = (root / 'pilot.lock').open('w')
fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
proton = home / 'panvk-refs/proton/p10-4-v14'
libs = home / 'wltest/imagefs/usr/lib'
driver = home / 'wltest/drvB19_cache_compiler/libvulkan_panfrost.so'
termux_lib = home / 'hangover/root/data/data/com.termux/files/usr/lib'
layer = home / 'panvk-refs/gameplay-2026-10-19/layer'
noshm = home / 'panvk-refs/proton/noshm/libnoshm.so'
(out / 'icd.json').write_text(json.dumps({'file_format_version': '1.0.0', 'ICD': {
    'library_path': str(libs / 'libvulkan_wrapper.so'), 'api_version': '1.3.303'}}))
controls = {
    'LD_PRELOAD': str(noshm), 'WINEPREFIX': str(root / 'prefix'),
    'WINEDEBUG': '-all', 'DISPLAY': ':0',
    'LD_LIBRARY_PATH': f'{termux_lib}:{libs}:{proton}/lib',
    'VK_ICD_FILENAMES': str(out / 'icd.json'),
    'ADRENOTOOLS_DRIVER_PATH': str(driver.parent) + '/',
    'ADRENOTOOLS_HOOKS_PATH': str(libs) + '/',
    'ADRENOTOOLS_DRIVER_NAME': driver.name,
    'WINEDLLOVERRIDES': 'd3d11,dxgi,d3d9,d3d10core,d3d8=n;winmm=n,b',
    'DXVK_LOG_LEVEL': 'info', 'DXVK_LOG_PATH': str(out),
    'DXVK_STATE_CACHE': '0', 'DXVK_HUD': 'fps,compiler,pipelines',
    'MESA_SHADER_CACHE_DISABLE': 'true', 'PANVK_V9_CLIP_KILL_FPK': args.clip_kill_fpk,
    'PANVK_DEFER_OQ': '0', 'PANVK_UBO_PUSH': '32',
    'PANVK_FRAME_PROF': str(out / 'frames.log'), 'PANVK_FRAME_PROF_PASSES': '1',
    'VK_LAYER_PATH': str(layer), 'VK_INSTANCE_LAYERS': 'VK_LAYER_MESA_overlay',
    'VK_LAYER_MESA_OVERLAY_CONFIG': f'no_display=1,device=0,format=0,fps=0,frame=1,'
        f'frame_timing=1,present_timing=1,submit=1,draw=1,draw_indexed=1,'
        f'fps_sampling_period=0,output_file={out}/present.csv',
}
env = {k: v for k, v in os.environ.items() if not k.startswith(
    ('PANVK_', 'PAN_', 'pan_', 'MESA_', 'DXVK_', 'VK_', 'ADRENOTOOLS_', 'IOLOG_'))}
env.update(controls)
paths = [driver, noshm, libs / 'libvulkan_wrapper.so', proton / 'bin/wine',
         root / 'game/NSUNS2.exe', root / 'game/config.ini', Path(__file__).resolve()]
paths += [root / 'prefix/drive_c/windows/system32' / (name + '.dll') for name in ('d3d11', 'dxgi')]
paths += [p for p in layer.iterdir() if p.is_file()]
hashes = {}
for path in paths:
    with path.open('rb') as stream:
        hashes[str(path)] = hashlib.file_digest(stream, 'sha256').hexdigest()
command = [str(proton / 'bin/wine'), 'explorer', '/desktop=shell,848x480',
           str(root / 'game/NSUNS2.exe')]
manifest = {'command': command, 'controls': controls, 'sha256': hashes,
            'start_unix': time.time(), 'screenshots': [], 'scope': 'Interactive gameplay setup pilot; not performance A/B.'}


def save():
    (out / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')


save()
start = time.monotonic()
next_shot = 15
with (out / 'process.log').open('w') as log:
    proc = subprocess.Popen(command, cwd=root / 'game', env=env, stdout=log, stderr=subprocess.STDOUT)
    try:
        while time.monotonic() - start < args.timeout and not (out / 'stop').exists():
            elapsed = time.monotonic() - start
            if elapsed >= next_shot:
                path = out / f'screen-{int(elapsed):04d}.png'
                result = subprocess.run(['import', '-window', 'root', str(path)],
                    env={**os.environ, 'DISPLAY': ':0'}, stdout=log, stderr=log, timeout=15)
                manifest['screenshots'].append({'file': path.name, 'elapsed_seconds': elapsed,
                                                'returncode': result.returncode})
                save()
                print(f'{out.name}: screenshot at {elapsed:.1f}s', flush=True)
                next_shot += 15
            time.sleep(.5)
    finally:
        subprocess.run([str(proton / 'bin/wineserver'), '-k'], env=env,
                       stdout=log, stderr=log, timeout=15)
        try:
            proc.wait(timeout=15)
        except subprocess.TimeoutExpired:
            proc.terminate()
            proc.wait(timeout=15)
        manifest.update({'duration_seconds': time.monotonic() - start,
                         'launch_returncode': proc.returncode,
                         'stopped_by_file': (out / 'stop').exists()})
        save()
