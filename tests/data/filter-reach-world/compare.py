"""Compare 1:1 grabs of the filter reach fixture (see README.md).

usage: compare.py <dir> <set> [<set> ...]
Each <dir>/<set>-events.log names the cases; <dir>/<set>-stock/f0000.png and <dir>/<set>-fugue/f0000.png are the
grabs (pair.sh). Prints per case the stock facts and Fugue's difference from stock, and writes <dir>/facts.json.
Set b animates for 20 s and the two clients are grabbed about a second apart, so its differences include that phase.
"""
import json
import sys
from pathlib import Path
from PIL import Image


def cases(d, st):
    return list(dict.fromkeys(l.split(None, 1)[1].strip() for l in open(d / f'{st}-events.log') if l.startswith('CASE')))


def close(rgb, ref, tol=90):
    return sum(abs(a - b) for a, b in zip(rgb, ref)) < tol


RED, MAGENTA = (255, 0, 0), (255, 0, 255)


def facts(img, k):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels
    at = lambda x, y: img.getpixel((ox + x, oy + y))[:3]
    area = [(x, y) for x in range(-16, 48) for y in range(-16, 48)]
    text = [(x, y) for x in range(-2, 16) for y in range(22, 36)]        # around the maptext "Ab" at the tile's foot
    return {
        'red': sum(close(at(x, y), RED) for x, y in area),
        'magenta': sum(close(at(x, y), MAGENTA) for x, y in area),
        'redAtText': sum(close(at(x, y), RED) for x, y in text),
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
            row = dict(case=n, **facts(stock, k), fugueRed=facts(fugue, k)['red'],
                       fugueDiffOver8=sum(v > 8 for v in diffs), fugueMaxDiff=max(diffs))
            print('%s %-22s %s' % (st, n, json.dumps(row)))
            out[st].append(row)
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
