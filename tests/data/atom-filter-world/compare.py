"""Compare 1:1 grabs of the atom filter fixture and read which parts each filter reaches (see README.md).

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

def close(rgb, ref, tol=60):
    return sum(abs(a - b) for a, b in zip(rgb, ref)) < tol

RED, GREEN = (255, 0, 0), (0, 255, 0)

def facts(img, k):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels
    at = lambda x, y: img.getpixel((ox + x, oy + y))[:3]
    row = lambda y, x0, x1, c: any(close(at(x, y), c) for x in range(x0, x1 + 1))
    col = lambda x, y0, y1, c: any(close(at(x, y), c) for y in range(y0, y1 + 1))
    # shape.png: a 16 px box at 8..23 with a translucent step at x 24..25. small.png: a box at 2..9 of a 16 px icon, so
    # an overlay at pixel_x 14 covers x 16..23, y 18..25 (two rows below the box), and an underlay at (-10, -10) covers
    # x -8..-1, y 28..35.
    return {
        'ringAroundBox': row(7, 8, 23, RED) or row(7, 8, 23, GREEN),        # the row above the box
        'ringBelowOverlay': row(26, 16, 23, RED),                            # under the overlay's protruding rows
        'ownRingBelowOverlay': row(26, 16, 23, GREEN),
        'ringBesideUnderlay': col(-9, 28, 35, RED),
        'ringUnderMaptext': row(32, 0, 12, RED) or col(-1, 22, 31, RED),
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
            s = facts(stock, k)
            row = dict(case=n, **s, fugueDiffOver8=sum(v > 8 for v in diffs), fugueMaxDiff=max(diffs))
            print('%s %-15s %s' % (st, n, json.dumps(row)))
            out[st].append(row)
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
