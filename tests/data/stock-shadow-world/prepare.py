"""Stage the standalone shadow fixture; no compilation, launch or external writes."""
from pathlib import Path
import json
import shutil
import subprocess
import sys

source = Path(__file__).resolve().parent
if len(sys.argv) != 2:
    raise SystemExit('usage: prepare.py <new-build-runtime>')
target = Path(sys.argv[1]).resolve()
if target.exists() or 'build' not in target.parts:
    raise SystemExit('Use a new build/ runtime')
subprocess.run([sys.executable, '-B', str(source.parent/'local-relay-world'/'prepare.py'),
                str(target)], check=True)
shutil.copy2(source/'world.dm', target/'world.dm')
cases = {}


def add(name, **params):
    cases['shadow-'+name] = dict(size=0, offset=0, x=-32, y=0,
                                color='#ff0000', source_alpha=255) | params


add('opaque')
add('partial', source_alpha=128)
add('translucent', color='#ff000080')
add('both', source_alpha=128, color='#ff000080')
add('fractional', x=-32.5, y=.5)
add('grow', offset=1)
add('grow-partial', offset=1, source_alpha=128, color='#ff000080')
add('blur', size=4, color='#04080FAA')
add('tg', size=4, x=0, y=-2, color='#04080FAA')
add('inner', size=-4, x=0, y=-2, color='#04080FAA')
add('negative-offset', offset=-1)
for size in (.0001, .001, .5, 1, 6, 6.25, 12, 64, 100, 200):
    add('size'+str(size), size=size, x=3.25, y=-2.5, color='#8040c080', source_alpha=128)
for offset in (.5, 4.25, 33.25):
    add('growth'+str(offset), offset=offset, size=.5, x=-7.125, y=3.75, color='#8040c080', source_alpha=128)
for alpha in ('00', '01', 'fe'):
    add('tint'+alpha, color='#ff80c0'+alpha, size=1)
add('repeat2', repeat=2, size=1, x=-8.5, y=3.25, color='#8040c080', source_alpha=128)
add('repeat8', repeat=8, size=0, x=2, y=-2, color='#8040c080', source_alpha=128)
add('chain', before=True, after=True, size=1, offset=1.5, x=-8.5, y=3.25, color='#8040c080', source_alpha=128)
add('edge', edge=True, size=4, x=0, y=-2, color='#04080FAA')
add('edge-fractional', edge=True, size=1, offset=4.25, x=-.5, y=.25, color='#8040c080', source_alpha=128)
add('target', target=True, size=4, x=0, y=-2, color='#04080FAA')
(target/'cases.json').write_text(json.dumps(cases, indent=2)+'\n')
(target/'phase.txt').write_text('initial:control')
