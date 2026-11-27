"""Headless wire input from an already owned synthetic server, one fresh guest per case.
Requires the configured Qt DLL directory on PATH. Does not launch/stop the server.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import uuid

p = argparse.ArgumentParser()
p.add_argument('runtime', type=Path)
p.add_argument('port', type=int)
p.add_argument('probe', type=Path)
p.add_argument('--label', default='wire')
a = p.parse_args()
if 'build' not in a.runtime.resolve().parts or not 1024 <= a.port <= 65535 or a.port in (1337, 13337):
    p.error('Use an owned build/ fixture server on a non-game port')
probe = a.probe.resolve()
if not probe.is_file():
    p.error('Build relay_wire_probe explicitly or specify an existing executable')
out = a.runtime/a.label
out.mkdir(exist_ok=False)
result = {'complete': False, 'phases': [], 'policy': 'fresh network guest per phase; no native GUI/input claims',
          'probe': str(probe), 'probeSha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
          'sources': {name: hashlib.sha256((a.runtime/name).read_bytes()).hexdigest()
                      for name in ('world.dm', 'world.dme', 'world.dmf', 'world.dmb', 'z.dmi', 'cases.json')}}
try:
    for index, case in enumerate(json.loads((a.runtime/'cases.json').read_text())):
        label = f'{index:02}-{case}'
        token = f'wire-{uuid.uuid4().hex}:{case}'
        (a.runtime/'phase.txt').write_text(token)
        state = a.runtime/f'{a.label}-guest-{index:02}'
        fields = out/f'{label}-fields.txt'
        subprocess.run([str(probe), str(a.port), str(state), str(fields)], check=True)
        assert 'READY '+token in (a.runtime/'events.log').read_text(), 'Missing phase acknowledgement'
        frames = []
        for line in fields.with_suffix('.txt.wire').read_text().splitlines():
            parts = line.split()
            if len(parts) >= 4 and parts[1] == 's2c' and parts[2] != '0001':
                frames.append((f'0x{parts[2]} len={parts[3]} '+(parts[4] if len(parts) > 4 else '')).rstrip())
        assert frames, 'No world frames'
        (out/f'frames-{label}.txt').write_text('\n'.join(frames)+'\n', newline='\n')
        (out/'res').mkdir(exist_ok=True)
        for asset in (state/'rsc').glob('*/*'):
            dest = out/'res'/asset.name
            assert not dest.exists() or dest.read_bytes() == asset.read_bytes()
            dest.write_bytes(asset.read_bytes())
        result['phases'].append({'case': case, 'label': label, 'phase': token})
        print(label, flush=True)
    result['complete'] = True
except BaseException as error:
    result['failure'] = repr(error)
    raise
finally:
    (out/'result.json').write_text(json.dumps(result, indent=2)+'\n')
