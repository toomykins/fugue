"""Write clicks.json from what tools/stockprobe/stockprobe.py click printed for this world (see README.md).

usage: clicks.py <stockprobe output> <stock version> <date>
Keeps stock's lines only: a left click's CLICK line (the atom's name, icon-x, icon-y) and a right click's MENU lines
(each entry is known by its one verb, poke_<name>), topmost first.
"""
import json, re, sys
from pathlib import Path

cases = []; case = None; action = None
for line in Path(sys.argv[1]).read_text(encoding='utf-8-sig').splitlines():
    head = re.match(r'(\S+): \d+ actions;', line)
    if head:
        case = {'case': head.group(1), 'clicks': [], 'menus': []}; cases.append(case); continue
    step = re.match(r'  (\S+)$', line)
    if step:
        action = step.group(1)
        kind, _, at = action.rpartition(':')
        if kind == 'right': case['menus'].append({'at': [int(v) for v in at.split(',')], 'entries': []})
        continue
    said = re.match(r'    (?:same|DIFFER|stock) +(.*)$', line)
    if not said: continue
    text = said.group(1); kind, _, at = action.rpartition(':')
    click = re.match(r'CLICK (\S+) x=(\d+) y=(\d+)', text)
    if click and not kind:
        case['clicks'].append({'at': [int(v) for v in at.split(',')], 'over': click.group(1),
                               'iconX': int(click.group(2)), 'iconY': int(click.group(3))})
    entry = re.match(r'MENU [^:]*: poke (\S+)$', text)
    if entry and kind == 'right': case['menus'][-1]['entries'].append(entry.group(1))
for case in cases:
    if not case['menus']: del case['menus']
out = {'schema': 1, 'stock': f'DreamSeeker/DreamDaemon {sys.argv[2]}', 'date': sys.argv[3],
       'method': 'tools/stockprobe/stockprobe.py click with set=<case>: left and right clicks at MAP pixels in the stock '
                 'window. A click is what the world logged (the clicked atom\'s name, icon-x, icon-y); a menu is the '
                 'entries of the right-click menu stock showed, topmost first, each named by its verb',
       'cases': cases}
Path(__file__).resolve().parent.joinpath('clicks.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
