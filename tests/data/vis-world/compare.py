"""Read per-case facts from 1:1 grabs of the vis fixture (see README.md).

usage: compare.py <dir> <set> [<set> ...]   (sets a, b, c, d)
Each <dir>/<set>-events.log names the cases; <dir>/<set>-stock/f0000.png and <dir>/<set>-fugue/f0000.png are the
grabs (pair.sh). Prints stock and Fugue facts per case and writes <dir>/facts.json with the stock facts.
"""
import json
import sys
from pathlib import Path
from PIL import Image

COLOURS = {
    'floor': (16, 16, 16), 'holder.a': (40, 80, 230), 'holder.b': (40, 200, 230), 'kid.a': (240, 120, 0),
    'kid.b': (230, 40, 40), 'wall': (120, 120, 120), 'white': (255, 255, 255), 'remote': (60, 20, 60),
    'remote.edge': (200, 0, 200), 'thing': (40, 200, 60), 'npc': (240, 220, 40), 'black': (0, 0, 0),
}

def name(rgb):
    best = min(COLOURS, key=lambda k: sum(abs(a - b) for a, b in zip(COLOURS[k], rgb)))
    return best if sum(abs(a - b) for a, b in zip(COLOURS[best], rgb)) < 40 else 'other'

def shape(img, ox, oy):
    """Set c: where each box lands, its size, which way its notch points and whether its edges are blended."""
    px = {(x, y): name(img.getpixel((ox + x, oy + y))[:3]) for y in range(-32, 64) for x in range(-32, 64)}
    out = {}
    for label, colour in (('kid', 'kid.b'), ('holder', 'holder.a'), ('thing', 'thing')):
        pts = [p for p, n in px.items() if n == colour]
        if not pts:
            continue
        xs, ys = [p[0] for p in pts], [p[1] for p in pts]
        f = {'cx': round(sum(xs) / len(pts), 1), 'cy': round(sum(ys) / len(pts), 1),
             'w': max(xs) - min(xs) + 1, 'h': max(ys) - min(ys) + 1}
        near = [p for p, n in px.items() if n == 'white' and any(
            px.get((p[0] + dx, p[1] + dy)) == colour for dx in (-2, -1, 0, 1, 2) for dy in (-2, -1, 0, 1, 2))]
        if near and label != 'thing':
            import math
            wx, wy = sum(p[0] for p in near) / len(near), sum(p[1] for p in near) / len(near)
            f['notch'] = round(math.degrees(math.atan2(wy - f['cy'], wx - f['cx'])) / 15) * 15 % 360  # 0 east, 90 south
        if label == 'kid':
            box = [(x, y) for x in range(min(xs), max(xs) + 1) for y in range(min(ys), max(ys) + 1)]
            f['blended'] = any(px[p] == 'other' and not any(px.get((p[0] + dx, p[1] + dy)) in ('holder.a', 'white')
                               for dx in (-1, 0, 1) for dy in (-1, 0, 1)) for p in box)
        out[label] = f
    return out

def cases(d, st):
    return list(dict.fromkeys(l.split(None, 1)[1].strip() for l in open(d / f'{st}-events.log') if l.startswith('CASE')))

def facts(img, st, k):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels
    at = lambda x, y: name(img.getpixel((ox + x, oy + y))[:3])
    if st == 'b':
        # Kid box (8..23 in its icon) moved by (12, -12): x 20..35, y 20..35. The holder covers 4..27, the wall bar
        # x 26..31 from the top of the tile to y 31.
        look = at(33, 33)
        f = {'drawn': look != 'floor'}
        if f['drawn']:
            f['look'] = look
            # An inherited holder icon is the holder's 4..27 box, so its notches sit 4 px further out.
            big = look.startswith('holder')
            f['dir'] = 'east' if at(38 if big else 34, 27) == 'white' else 'south' if at(27, 38 if big else 34) == 'white' else '?'
            f['overWall'] = at(28, 30) != 'wall'
            f['overHolder'] = None if look == 'holder.a' else at(22, 22) == look
        return f
    if st == 'c':
        return shape(img, ox, oy)
    # Sets a and d: the remote turf's white corner sits at 1..6 of the copy, its contents' thing at 16..29 (under the
    # holder's 4..27 box unless offset), its npc at 2..11 x 12..29.
    dx, dy = {'obj-turf-px': (6, 4), 'turf-mob': (4, 0)}.get(cases.current[k], (0, 0))
    return {'turf': at(2 + dx, 2 + dy) == 'white', 'thing': at(28 + dx, 22 + dy) == 'thing',
            'npc': at(3 + dx, 20 + dy) == 'npc', 'kid': 'kid.b' in (at(10 + dx, 20 + dy), at(29 + dx, 10 + dy), at(2 + dx, 30 + dy))}

if __name__ == '__main__':
    d = Path(sys.argv[1]); out = {}
    for st in sys.argv[2:]:
        names = cases(d, st); cases.current = names
        stock = Image.open(d / f'{st}-stock/f0000.png').convert('RGB')
        fugue = Image.open(d / f'{st}-fugue/f0000.png').convert('RGB')
        out[st] = []
        for k, n in enumerate(names):
            s, g = facts(stock, st, k), facts(fugue, st, k)
            print('%s %-16s stock %s%s' % (st, n, json.dumps(s), '' if s == g else '  FUGUE ' + json.dumps(g)))
            out[st].append(dict(case=n, **s))
    (d / 'facts.json').write_text(json.dumps(out, indent=1))
