"""Write clicks.json from what tools/stockprobe/stockprobe.py click --stock-only printed for this world (README.md,
"Clicks in the larger map").

usage: clicks.py <stockprobe output> <stock version> <date>
Keeps per left click the CLICK line (the atom's name, icon-x, icon-y and place), or null when the world logged none,
and per right click the menu's entries, topmost first, each known by its verb (poke_<kind>); an empty list when stock
opened no menu.
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
        point = [int(v) for v in at.split(',')] if at else None
        if kind == 'right': case['menus'].append({'at': point, 'entries': []})
        elif not kind: case['clicks'].append({'at': point, 'over': None})
        continue
    said = re.match(r'    (?:same|DIFFER|stock) +(.*)$', line)
    if not said: continue
    text = said.group(1); kind, _, at = action.rpartition(':')
    click = re.match(r'CLICK (\S+) x=(\d+) y=(\d+) at=(\d+),(\d+)', text)
    if click and not kind:
        case['clicks'][-1].update({'over': click.group(1), 'iconX': int(click.group(2)), 'iconY': int(click.group(3)),
                                   'place': [int(click.group(4)), int(click.group(5))]})
    entry = re.match(r'MENU [^:]*: poke (\S+)$', text)
    if entry and kind == 'right': case['menus'][-1]['entries'].append(entry.group(1))
out = {'schema': 1, 'stock': f'DreamSeeker/DreamDaemon {sys.argv[2]}', 'date': sys.argv[3],
       'method': 'tools/stockprobe/stockprobe.py --stock-only --map <the control> click with case=<case>: left and right '
                 'clicks at MAP pixels in the stock window. A click is what the world logged (the clicked atom\'s name, '
                 'icon-x, icon-y and place; screen objects stand at 0,0), or null when it logged nothing; a menu is the '
                 'entries of the right-click menu stock showed, topmost first, each named by its verb (poke_<kind>), '
                 'empty when it showed none',
       'cases': cases}
Path(__file__).resolve().parent.joinpath('clicks.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
