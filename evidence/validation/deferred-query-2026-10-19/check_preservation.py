from pathlib import Path
import hashlib
import json
import time

root = Path(__file__).resolve().parent
home = root.parent.parent
mesa = home / 'panvk-g57/mesa-up'


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


checks = {}
sources = json.loads((root / 'source-candidate.json').read_text())
for name, expected in sources.items():
    checks['source/' + name] = {'expected': expected, 'actual': digest(mesa / name)}
manifest = json.loads((root / 'build-manifest.json').read_text())
for name, record in manifest['artifacts'].items():
    checks['artifact/' + name] = {'expected': record['sha256'], 'actual': digest(root / name)}
state = json.loads((home / 'panvk-refs/gameplay-2026-10-19/initial-state/state.json').read_text())
prefix = home / 'wltest/wpfx'
for name, expected in state['sha256'].items():
    path = prefix / name if name == 'user.reg' else (
        prefix / state['save_relative'] / name.removeprefix('SaveGames/'))
    checks['original-prefix/' + name] = {'expected': expected, 'actual': digest(path)}
for name, expected in {
    'drvB19_cache_compiler': 'd77c3c46ecd24889b34f1fe1ebd80f284622be729673c568ce1e62c244dbd114',
    'drvM18': '05deba739c0a6691eab6357de7588a8dc29860777ef4952ad6a8f2ddde24ea2e',
    'drvM19': '0b891e7a9aed5062baef8da4e612ccf91aa37683cc05244c68f5846d35f650e7',
}.items():
    checks['validated-package/' + name] = {
        'expected': expected, 'actual': digest(home / 'wltest' / name / 'libvulkan_panfrost.so')}
bad = [name for name, record in checks.items() if record['expected'] != record['actual']]
result = {'ok': not bad, 'checked_unix': time.time(), 'checks': checks, 'mismatches': bad}
(root / 'preservation-final.json').write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps({'ok': not bad, 'checks': len(checks), 'mismatches': bad}))
raise SystemExit(bool(bad))
