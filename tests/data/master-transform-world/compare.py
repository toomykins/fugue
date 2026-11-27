"""Compare 1:1 grabs of the master transform fixture and write stock.json (see README.md).

usage: compare.py <dir> <case> [<case> ...]
<dir>/<case>-stock/f0000.png and <dir>/<case>-fugue/f0000.png are the grabs (tools/stockprobe/stockprobe.py pair with
set=<case>). Keeps the stock bounding boxes of the red world object and the blue overlay (MAP pixels, inclusive) and
Fugue's difference from stock over the 224 px map.
"""
import json
import sys
from pathlib import Path
from PIL import Image


def bbox(img, test):
    pts = [(x, y) for y in range(224) for x in range(224) if test(img.getpixel((x, y)))]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return [min(xs), min(ys), max(xs), max(ys)]


if __name__ == '__main__':
    d = Path(sys.argv[1]); rows = []
    for case in sys.argv[2:]:
        stock = Image.open(d / f'{case}-stock/f0000.png').convert('RGB')
        fugue = Image.open(d / f'{case}-fugue/f0000.png').convert('RGB')
        diffs = [max(abs(a - b) for a, b in zip(stock.getpixel((x, y)), fugue.getpixel((x, y))))
                 for y in range(224) for x in range(224)]
        row = {'case': case, 'red': bbox(stock, lambda c: c[0] > 120 and c[2] < 100),
               'blue': bbox(stock, lambda c: c[2] > 120 and c[0] < 100),
               'fugueDiffOver8': sum(v > 8 for v in diffs), 'fugueMaxDiff': max(diffs)}
        print(json.dumps(row)); rows.append(row)
    out = {'schema': 1, 'stock': 'DreamSeeker/DreamDaemon 516.1688', 'date': '2026-11-14',
           'method': 'native 1:1 grabs of stock and Fugue on one server; bounding boxes from the stock grab (half-covered '
                     'edge pixels of scaled boxes count); fugueDiff* compare the whole map after the change',
           'cases': rows}
    (d / 'stock.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
