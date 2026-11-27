"""Compare 1:1 grabs of the filter types fixture and write stock.json (see README.md).

usage: compare.py <dir> <set> [<set> ...]
Each <dir>/<set>-events.log names the cases; <dir>/<set>-stock/f0000.png and <dir>/<set>-fugue/f0000.png are the
grabs (tools/stockprobe/stockprobe.py pair). For each case it keeps up to 16 stock pixels the filter changed (against the
unfiltered icon drawn by the fixture's prepare.py assets), Fugue's difference from stock over the 96 px crop, and the
case's wire records as Fugue received them (captured with a temporary decoder dump). Writes <dir>/stock.json.
"""
import json
import sys
from pathlib import Path
from PIL import Image

# 516.1688 wire records (type, record flags, hex params), in filter order.
RECORDS = {
    'wave': [(5, 1, '000000400000004000000000000000000000')],
    'wave-y': [(5, 1, '0000404000000000000080400000003f0000')],
    'wave-diag': [(5, 1, '0000c03f00000041000000410000803e0000')],
    'wave-side': [(5, 1, '000000400000a04000000000000000000100')],
    'wave-bound': [(5, 1, '000040400000c0c000000040000000000200')],
    'wave-flags': [(5, 1, '0000803f000040400000803f000000000300')],
    'wave-two': [(5, 1, '0000803f0000804000000000000000000000'), (5, 2, '00000040000000000000c0409a99993e0000')],
    'wave-split': [(5, 1, '0000803f0000804000000000000000000000'), (5, 2, '00000040000000000000c040000000000200')],
    'wave-big': [(5, 1, '000020420000204100000000000000000000')],
    'ripple': [(6, 1, '000040400000000000000000000000410000000000400000803f')],
    'ripple-xy': [(6, 1, '0000004000008040000000c00000404100000000803f0000003f')],
    'ripple-bound': [(6, 1, '000080400000000000000000000020410200000040400000803f')],
    'ripple-two': [(6, 1, '000000400000c0c0000000000000c0400000000000400000803f'),
                   (6, 2, '000040400000c04000008040000010410000000040400000803f')],
    'ripple-sign': [(6, 1, '9a99d93f00000000000000009a99193f00006666a63f00000040')],
    'bloom': [(14, 1, '00004040808080ff0000803f')],
    'bloom-soft': [(14, 1, '000000400000008000000000')],
    'bloom-orb': [(14, 1, '00000040ddddddff00000040')],
    'bloom-spread': [(14, 1, '00000000000000ff00000040')],
    'bloom-wide': [(14, 1, '00008040000000ff00000000')],
    'bloom-grid': [(14, 1, '0000803f646464c80000803f')],
    # The record byte numbers the filter: the bar sign's seven waves arrive as 1..7.
    'sign-static': [(5, 1, '0000c03f00004041000010c1cdcc4c3e0000'), (5, 2, '00000040000070c1000080409a99193f0000'),
                    (5, 3, 'cdcc4c3f0000a04100009041cdcccc3d0000'), (5, 4, '00002040000030c1000060c16666663f0000'),
                    (5, 5, '0000803f000010410000c841cdcccc3e0000'), (5, 6, '6666e63f0000b0c1000010419a99993e0000'),
                    (5, 7, '9a99193f00006041000040c13333333f0000')],
}
MASKS = {5: 0x3d, 6: 0x87d, 14: 0x13}


def cases(d, st):
    return list(dict.fromkeys(l.split(None, 1)[1].strip() for l in open(d / f'{st}-events.log') if l.startswith('CASE')))


def origin(k):
    return (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32   # the case tile's top-left in MAP pixels


if __name__ == '__main__':
    d = Path(sys.argv[1]); here = Path(__file__).resolve().parent
    out = {'schema': 1, 'stock': 'DreamSeeker/DreamDaemon 516.1688', 'date': '2026-11-14',
           'method': 'native 1:1 grabs of stock and Fugue on one server (pair.sh); samples are stock pixels (x, y '
                     'relative to the case tile, RGB) where the filter changed the unfiltered icon over the floor; '
                     'fugueDiff* compare the 96 px crops after the change (bottom-row crops stop at the map edge)',
           'sets': {}}
    floor = (16, 16, 16)
    for st in sys.argv[2:]:
        stock = Image.open(d / f'{st}-stock/f0000.png').convert('RGB')
        fugue = Image.open(d / f'{st}-fugue/f0000.png').convert('RGB')
        rows = []
        for k, n in enumerate(cases(d, st)):
            ox, oy = origin(k)
            icon = Image.open(d / ('shape.png' if n.startswith('bloom') and n != 'bloom-grid' else 'grid.png')).convert('RGBA')
            def plain(x, y):
                if not (0 <= x < 32 and 0 <= y < 32): return floor
                r, g, b, a = icon.getpixel((x, y))
                return tuple(round(c * a / 255 + f * (1 - a / 255)) for c, f in zip((r, g, b), floor))
            area = [(x, y) for y in range(-24, 56) for x in range(-24, 56) if 0 <= oy + y < 384]
            changed = [(x, y) for x, y in area
                       if max(abs(a - b) for a, b in zip(stock.getpixel((ox + x, oy + y)), plain(x, y))) > 24]
            step = max(1, len(changed) // 16)
            samples = [[x, y, *stock.getpixel((ox + x, oy + y))] for x, y in changed[::step][:16]]
            diffs = [max(abs(a - b) for a, b in zip(stock.getpixel((x, y)), fugue.getpixel((x, y))))
                     for y in range(max(0, oy - 32), min(384, oy + 64)) for x in range(ox - 32, ox + 64)]
            row = {'case': n, 'changed': len(changed), 'samples': samples,
                   'fugueDiffOver8': sum(v > 8 for v in diffs), 'fugueMaxDiff': max(diffs)}
            if n in RECORDS:
                row['records'] = [{'type': t, 'paramMask': MASKS[t], 'flags': f, 'params': p} for t, f, p in RECORDS[n]]
            print('%s %-16s changed %4d fugueMax %3d' % (st, n, len(changed), row['fugueMaxDiff']))
            rows.append(row)
        out['sets'][st] = rows
    (d / 'stock.json').write_text(json.dumps(out, indent=1) + '\n', newline='\n')
