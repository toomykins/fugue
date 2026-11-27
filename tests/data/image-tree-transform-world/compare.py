"""Compare 1:1 grabs of the image tree transform fixture (see README.md).

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


YELLOW, BLUE, WHITE = (240, 220, 40), (40, 80, 230), (255, 255, 255)


def box(img, k, colour):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels
    pts = [(x, y) for x in range(-24, 56) for y in range(-24, 56) if close(img.getpixel((ox + x, oy + y))[:3], colour)]
    if not pts:
        return None
    xs, ys = [p[0] for p in pts], [p[1] for p in pts]
    return [min(xs), min(ys), max(xs) - min(xs) + 1, max(ys) - min(ys) + 1]   # tile-relative x, y, w, h


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
            # The image is shape.png's yellow box (its white corner shows the turn); the target and overlays are blue.
            row = dict(case=n, yellow=box(stock, k, YELLOW), white=box(stock, k, WHITE), blue=box(stock, k, BLUE),
                       fugueDiffOver8=sum(v > 8 for v in diffs), fugueMaxDiff=max(diffs))
            print('%s %-24s %s' % (st, n, json.dumps(row)))
            out[st].append(row)
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
