"""Write clicks.json from what tools/stockprobe/stockprobe.py click printed for this world (see README.md).

usage: clicks.py <stockprobe output> <stock version> <date> [--add]
Keeps stock's lines only: a left click's CLICK line (the atom's name, icon-x, icon-y) and a right click's MENU lines
(each entry is known by its one verb, poke_<name>), topmost first. With --add the cases go into the clicks.json there
is, in place of any of the same name, each marked with its date when that is not the file's. With --gestures it writes
gestures.json instead: per case, every action and the mouse procs stock's world logged for it (ENTER, DOWN, DRAG,
DROP, UP and CLICK lines, as logged).
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
target = Path(__file__).resolve().parent.joinpath('clicks.json')
if '--gestures' in sys.argv[4:]:
    steps = []; case = None
    for line in Path(sys.argv[1]).read_text(encoding='utf-8-sig').splitlines():
        head = re.match(r'(\S+): \d+ actions;', line)
        if head: case = {'case': head.group(1), 'actions': []}; steps.append(case); continue
        step = re.match(r'  (\S+)$', line)
        if step: case['actions'].append({'action': step.group(1), 'logged': []}); continue
        said = re.match(r'    (?:same|DIFFER|stock) +((?:ENTER|MOVE|DOWN|DRAG|DROP|UP|CLICK) .*)$', line)
        if said: case['actions'][-1]['logged'].append(said.group(1))
    out = {'schema': 1, 'stock': f'DreamSeeker/DreamDaemon {sys.argv[2]}', 'date': sys.argv[3],
           'method': 'tools/stockprobe/stockprobe.py --stock-only --cursor click with set=<case>: pointer moves, presses '
                     'and releases at MAP pixels in the stock window, the real pointer put on each point. Per action the '
                     'mouse procs the world logged: the atom\'s name, for MouseDrag and MouseDrop the atom it is over, '
                     'and icon-x/y from the params',
           'cases': steps}
    target.with_name('gestures.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
    sys.exit()
if '--add' in sys.argv[4:]:
    old = json.loads(target.read_text(encoding='utf-8'))
    names = {case['case'] for case in cases}
    for case in cases:
        if sys.argv[3] != old['date']: case['date'] = sys.argv[3]
    old['cases'] = [case for case in old['cases'] if case['case'] not in names] + cases
    target.write_text(json.dumps(old, indent=1) + '\n', newline='\n')
    sys.exit()
out = {'schema': 1, 'stock': f'DreamSeeker/DreamDaemon {sys.argv[2]}', 'date': sys.argv[3],
       'method': 'tools/stockprobe/stockprobe.py click with set=<case>: left and right clicks at MAP pixels in the stock '
                 'window. A click is what the world logged (the clicked atom\'s name, icon-x, icon-y); a menu is the '
                 'entries of the right-click menu stock showed, topmost first, each named by its verb',
       'cases': cases}
target.write_text(json.dumps(out, indent=1) + '\n', newline='\n')
