"""Compare 1:1 grabs of the transform group fixture and read stock's order of filter and matrix (see README.md).

usage: compare.py <dir> <set> [<set> ...]
Each <dir>/<set>-events.log names the cases; <dir>/<set>-stock/f0000.png and <dir>/<set>-fugue/f0000.png are the
grabs (pair.sh). Prints per case the stock facts and Fugue's difference from stock, and writes <dir>/facts.json.
"""
import json
import sys
from pathlib import Path
from PIL import Image

def cases(d, st):
    return list(dict.fromkeys(l.split(None, 1)[1].strip() for l in open(d / f'{st}-events.log') if l.startswith('CASE')))

def close(rgb, ref, tol=90):
    return sum(abs(a - b) for a, b in zip(rgb, ref)) < tol

RED, GREEN, MAGENTA, FLOOR = (255, 0, 0), (0, 255, 0), (255, 0, 255), (16, 16, 16)

def facts(img, k):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels
    at = lambda x, y: img.getpixel((ox + x, oy + y))[:3]
    run = lambda pts, c: sum(close(at(x, y), c) for x, y in pts)
    # shape.png's 16 px box spans 8..23 of the tile; Scale(2,1) stretches it to 0..31 about the centre.
    return {
        # A 1 px outline drawn before a 2x stretch is 2 px wide on the stretched sides, 1 px on the others.
        'redColumnsLeft': run([(x, 16) for x in range(-8, 8)], RED),
        'redRowsAbove': run([(16, y) for y in range(0, 8)], RED),
        # drop_shadow x=6 under Turn(90): turned with the atom it lands below the box, not to its right.
        'shadowBelow': run([(16, y) for y in range(24, 32)], MAGENTA) > 0,
        'shadowRight': run([(x, 16) for x in range(24, 32)], MAGENTA) > 0,
        'green': run([(x, y) for x in range(0, 48) for y in range(0, 48)], GREEN) > 0,
        'drawn': any(not close(at(x, y), FLOOR, 12) for x in range(-16, 48) for y in range(-16, 48)),
    }

if __name__ == '__main__':
    d = Path(sys.argv[1]); out = {}
    for st in sys.argv[2:]:
        names = cases(d, st)
        stock = Image.open(d / f'{st}-stock/f0000.png').convert('RGB')
        fugue = Image.open(d / f'{st}-fugue/f0000.png').convert('RGB')
        out[st] = []
        for k, n in enumerate(names):
            ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32
            diffs = [max(abs(a - b) for a, b in zip(stock.getpixel((x, y)), fugue.getpixel((x, y))))
                     for y in range(max(0, oy - 32), min(384, oy + 64)) for x in range(max(0, ox - 32), ox + 64)]
            row = dict(case=n, **facts(stock, k), fugueDiffOver8=sum(v > 8 for v in diffs), fugueMaxDiff=max(diffs))
            print('%s %-22s %s' % (st, n, json.dumps(row)))
            out[st].append(row)
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
