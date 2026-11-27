"""Click the fixture's points in stock and Fugue and record both answers (see README.md).

usage: compare.py points            print the client-area points for tools/stockprobe/stockprobe.py click
       compare.py <dir>             read <dir>/stock-clicks.txt and <dir>/fugue-clicks.txt (the CLICK lines each client
                                    produced for those points, in order) and write <dir>/stock.json
The first point is a throwaway floor click: it activates Fugue's window. Points are MAP pixels relative to the case
tile's top-left; icon-x/y are DM's (1-based, y up). usr is dropped (it is the clicking key).
"""
import json
import re
import sys
from pathlib import Path

CASES = ['turf-member', 'mob-member', 'inherit-id', 'plain-member', 'vis-turned', 'screen-turned', 'kt-root', 'outline',
         'shadow', 'image', 'maptext', 'image-turned-target', 'wave', 'kt-outline', 'scaled-member', 'kt-turned',
         'filter-turned', 'maptext-big']
POINTS = [('kt-outline', 'prime-floor', 56, 56),
          ('turf-member', 'turf-copy', 3, 3), ('turf-member', 'turf-content', 16, 16), ('mob-member', 'mob', 16, 16),
          ('mob-member', 'mob-empty', 3, 3), ('inherit-id', 'inherit', 16, 16), ('plain-member', 'plain', 16, 16),
          ('vis-turned', 'turned-in', 16, 5), ('vis-turned', 'turned-corner', 9, 9), ('screen-turned', 'hud-in', 16, 5),
          ('screen-turned', 'hud-corner', 9, 9), ('kt-root', 'kt-root', 10, 16), ('kt-root', 'kt-overlay', 30, 16),
          ('outline', 'outline-ink', 6, 16), ('outline', 'outline-box', 16, 16), ('shadow', 'shadow-ink', 27, 16),
          ('image', 'image', 16, 16), ('image', 'image-outline', 6, 16), ('maptext', 'maptext', 2, 16),
          ('image-turned-target', 'image-overlay', 29, 15), ('image-turned-target', 'image-root', 16, 16),
          ('wave', 'wave-ink', 6, 10), ('kt-outline', 'kt-outline-root', 10, 16), ('kt-outline', 'kt-outline-overlay', 30, 16),
          ('kt-outline', 'kt-outline-ink', 37, 16), ('scaled-member', 'scaled-out', 2, 16), ('scaled-member', 'scaled-in', 16, 16),
          ('kt-turned', 'ktturn-root', 16, 12), ('kt-turned', 'ktturn-overlay', 16, 30), ('filter-turned', 'fturn-box', 16, 10),
          ('filter-turned', 'fturn-ink', 15, 3), ('filter-turned', 'fturn-corner', 9, 9),
          ('maptext-big', 'maptext-a', 4, 22), ('maptext-big', 'maptext-b', 15, 25)]


def origin(case):
    k = CASES.index(case)
    return (1 + (k % 6) * 3) * 32, (2 + (k // 6) * 3) * 32


def parse(line):
    m = re.match(r'CLICK (.*?) usr=\S+ x=(\d*) y=(\d*) loc=(.*)$', line.strip())
    return {'src': m.group(1), 'x': int(m.group(2) or 0), 'y': int(m.group(3) or 0), 'loc': m.group(4)}


if __name__ == '__main__':
    if sys.argv[1] == 'points':
        print(';'.join(f'{origin(c)[0] + x},{origin(c)[1] + y}' for c, _, x, y in POINTS))
        raise SystemExit
    d = Path(sys.argv[1])
    stock = [parse(l) for l in open(d / 'stock-clicks.txt')]
    fugue = [parse(l) for l in open(d / 'fugue-clicks.txt')]
    rows = []
    for i, (case, label, x, y) in enumerate(POINTS):
        row = {'case': case, 'label': label, 'point': [x, y], 'stock': stock[i],
               'fugueMatches': i < len(fugue) and fugue[i] == stock[i]}
        rows.append(row)
        print('%-2s %-20s %s' % ('' if row['fugueMatches'] else '!!', label, stock[i]))
    out = {'schema': 1, 'stock': 'DreamSeeker/DreamDaemon 516.1688', 'date': '2026-11-14',
           'method': 'tools/stockprobe/stockprobe.py click left-clicks each point in the stock and the Fugue window on one server; '
                     'each atom logs Click(); fugueMatches compares src, icon-x/y and location after the change',
           'clicks': rows}
    (d / 'stock.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
