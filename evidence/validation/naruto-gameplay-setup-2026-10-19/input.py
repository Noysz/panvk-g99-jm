import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path(__file__).resolve().parent
out = root / sys.argv[1]
key = sys.argv[2]
hold = float(sys.argv[3]) if len(sys.argv) > 3 else .25
env = {**os.environ, 'DISPLAY': ':0'}
path = out / 'manual-actions.json'
actions = json.loads(path.read_text()) if path.exists() else []
start = json.loads((out / 'manifest.json').read_text())['start_unix']
record = {'key': key, 'hold_seconds': hold, 'unix': time.time(), 'elapsed_seconds': time.time() - start}
try:
    subprocess.run(['xdotool', 'keydown', key], env=env, check=True)
    time.sleep(hold)
finally:
    subprocess.run(['xdotool', 'keyup', key], env=env, check=True)
actions.append(record)
path.write_text(json.dumps(actions, indent=2) + '\n')
time.sleep(2)
shot = out / f'action-{len(actions):02d}.png'
subprocess.run(['import', '-window', 'root', str(shot)], env=env, check=True)
print(shot)
