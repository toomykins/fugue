"""Compare 1:1 grabs of the stock parity fixture (see README.md).

usage: compare.py <dir> <set> [<set> ...]
Each <dir>/<set>-events.log names the cases; <dir>/<set>-stock/f0000.png and <dir>/<set>-fugue/f0000.png are the
grabs (`stockprobe.py pair`). Prints per case stock's pixels at the sample points and Fugue's difference from stock, and writes
<dir>/facts.json.
"""
import json
import sys
from pathlib import Path
from PIL import Image

# Tile-relative points (x from the left, y from the top) read from the stock grab.
POINTS = {
    'images-groups': {'left': (11, 20), 'top': (20, 11), 'box': (20, 20), 'corner': (11, 11), 'parent': (6, 6), 'small': (5, 26),
          'overlap': (16, 16), 'right': (26, 16), 'next': (52, 20)},
    'images-groups-more': {'left': (11, 20), 'top': (20, 11), 'box': (20, 20), 'corner': (11, 11), 'parent': (6, 6), 'right': (26, 16),
          'low': (16, 26)},
    'color-matrix-alpha': {'parent': (6, 6), 'box': (20, 20)},
    'animated-groups': {'parent': (6, 6), 'box': (20, 20)},
    'alpha-masks': {'left': (8, 20), 'band': (22, 11), 'right': (22, 22), 'edge': (16, 20), 'outside': (2, 16), 'far': (30, 28)},
    'repeated-filters': {'parent': (6, 6), 'edge': (3, 16), 'corner': (3, 3), 'outside': (1, 1), 'right': (26, 16), 'low': (16, 29)},
    'relay-hud': {'box': (20, 20), 'centre': (16, 16)},
    'relay-hud-turn': {'box': (20, 20), 'centre': (16, 16)},
    'relay-hud-color': {'box': (20, 20), 'centre': (16, 16)},
    'relay-hud-outline': {'box': (20, 20), 'centre': (16, 16)},
    'relay-obj': {'box': (20, 20), 'centre': (16, 16)},
    'relay-hud-noicon': {'box': (20, 20), 'centre': (16, 16)},
    'relay-obj-turn': {'box': (20, 20), 'centre': (16, 16)},
    'relay-obj-color': {'box': (20, 20), 'centre': (16, 16)},
    'relay-obj-outline': {'box': (20, 20), 'centre': (16, 16)},
    'blend-reset-layers': {'parent': (6, 6), 'box': (20, 20), 'kid': (22, 24), 'edge': (3, 16), 'centre': (16, 16)},
    'directions': {'parent': (6, 6), 'box': (20, 20), 'kid': (22, 24), 'edge': (3, 16), 'centre': (16, 16)},
    'icon-states': {'parent': (6, 6), 'box': (20, 20), 'kid': (22, 24), 'edge': (3, 16), 'centre': (16, 16)},
    'layers-planes': {'parent': (6, 6), 'box': (20, 20), 'kid': (22, 24), 'edge': (3, 16), 'centre': (16, 16)},
    'render-target-relays': {'box': (20, 20), 'centre': (16, 16)},
    'overlay-relays': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'edge': (28, 16)},
    'tgstation-refusals': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'left': (9, 16)},
    'animated-render-target': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'left': (9, 16)},
    'turned-mob-images': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'left': (9, 16)},
    'relay-overlay-matrix': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'left': (9, 16)},
    'shared-render-target': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5)},
    'surface-bounds': {'box': (20, 20), 'centre': (16, 16), 'corner': (5, 5), 'left': (9, 16)},
    'sweep': {'parent': (6, 6), 'box': (20, 20), 'kid': (22, 24), 'edge': (3, 16), 'centre': (16, 16)},
    'group-blend-add': {'parent': (6, 6), 'red': (10, 10), 'overlap': (16, 16), 'blue': (24, 24), 'outside': (30, 2)},
    'plane-mask-filters': {'left': (8, 20), 'band': (22, 11), 'right': (22, 22), 'edge': (16, 20), 'outside': (2, 16), 'far': (30, 28)},
}


def cases(d, st):
    return list(dict.fromkeys(l.split(None, 1)[1].strip() for l in open(d / f'{st}-events.log') if l.startswith('CASE')))


def origin(k):
    return (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels


if __name__ == '__main__':
    d = Path(sys.argv[1]); out = {}
    for st in sys.argv[2:]:
        names = cases(d, st)
        stock = Image.open(d / f'{st}-stock/f0000.png').convert('RGB')
        fugue = Image.open(d / f'{st}-fugue/f0000.png').convert('RGB')
        out[st] = []
        for k, n in enumerate(names):
            ox, oy = origin(k)
            diffs = [max(abs(a - b) for a, b in zip(stock.getpixel((x, y)), fugue.getpixel((x, y))))
                     for y in range(max(0, oy - 32), min(384, oy + 64)) for x in range(max(0, ox - 32), ox + 64)]
            row = dict(case=n, stock={p: list(stock.getpixel((ox + x, oy + y))) for p, (x, y) in POINTS[st].items()},
                       fugueDiffOver8=sum(v > 8 for v in diffs), fugueMaxDiff=max(diffs))
            print('%s %-22s over8=%-5d max=%-3d %s' % (st, n, row['fugueDiffOver8'], row['fugueMaxDiff'],
                                                     json.dumps(row['stock'])))
            out[st].append(row)
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
