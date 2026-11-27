"""Reproduce the independent flat master-output rows (build/ output only)."""
from pathlib import Path
import json
import struct
import subprocess
import sys

here = Path(__file__).resolve().parent
out = Path(sys.argv[1]).resolve()
assert 'build' in out.parts
subprocess.run([sys.executable, '-B', str(here/'prepare.py'), str(out)], check=True)
corpus = json.loads((here/'master-arithmetic.json').read_text())
block = ''
for i, row in enumerate(corpus['rows']):
    block += (f'                if(p == "{row["name"]}")\n'
              f"                    S.icon = '{'yellow' if row.get('yellow') else 'precision'}.png'\n"
              f'                    A.color = rgb({36+12*i},144,224)\n')
    for role, var in [('master', 'M'), ('root', 'R')]:
        fields = {**corpus['appearanceDefaults'], **corpus['appearances'][row[role]]}
        raw = next((bytes.fromhex(v['data']) for v in fields.get('vars', []) if v['bit'] == 1024), None)
        if raw is not None:
            values = [format(v, '.10g') for v in struct.unpack('<20f', raw)]
            assert struct.pack('<20f', *map(float, values)) == raw
            block += f'                    {var}.color = list('+','.join(values)+')\n'
        elif fields.get('hasColor'):
            rgba = [fields['color'+c] for c in 'RGBA']
            block += f'                    {var}.color = rgb('+','.join(map(str,rgba))+')\n'
world = out/'world.dm'
world.write_text(world.read_text().replace('                sleep(2)', block+'                sleep(2)'))
(out/'phase.txt').write_text('initial:control')
print('Observe with --cases', ' '.join(row['name'] for row in corpus['rows']))
