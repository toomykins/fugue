"""Keep hoverprobe.py runs in hover.json: hover.py <hover.json> <date> <run>=<probe output>[@<replay>] ... [-<run>/<case>]

Each run is one hoverprobe.py output (one DreamDaemon and one stock client, cases in order); <replay> is the
replay/<name>.txt recorded for the same compile switches (stockprobe.py record), default base.txt. -<run>/<case> leaves
out a case, e.g. one the user's own pointer disturbed. An existing hover.json keeps its other runs.
"""
import json
import sys
from pathlib import Path

if len(sys.argv) < 4: raise SystemExit(__doc__)
out, date = Path(sys.argv[1]), sys.argv[2]
drop = {a[1:] for a in sys.argv[3:] if a.startswith('-')}
doc = json.loads(out.read_text()) if out.exists() else {'schema': 1}
doc['method'] = ('tools/stockprobe/hoverprobe.py --world tests/data/hover-world: the real pointer moved over stock '
                 'DreamSeeker\'s map (SetCursorPos), real button presses and releases, and moves posted to its window. '
                 'Per action the lines the world logged while it lasted (world.time, proc, atom, location, control, '
                 'params). Before each case the pointer goes outside the window for a second; lines logged then are '
                 'not kept. A run is one server and one client, its cases in order.')
runs = {r['run']: r for r in doc.get('runs', [])}
for arg in sys.argv[3:]:
    if arg.startswith('-'): continue
    name, _, rest = arg.partition('=')
    path, _, replay = rest.partition('@')
    probe = json.loads(Path(path).read_text())
    cases = [c for c in probe['cases'] if f"{name}/{c['case']}" not in drop]
    runs[name] = {'run': name, 'date': date, 'stock': probe['stock'], 'switches': probe['switches'],
                  'params': probe['params'], 'replay': replay or 'base.txt', 'cases': cases}
    if 'timer_ms' in probe: runs[name]['timer_ms'] = probe['timer_ms']   # system timer resolution; hoverprobe --timer1
doc['runs'] = list(runs.values())
out.write_text(json.dumps(doc, indent=1) + '\n', newline='\n')
