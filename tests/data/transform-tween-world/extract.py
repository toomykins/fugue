"""Recover each case's affine matrix per frame from the arm's three colour features (mats.json).

Usage: extract.py <run dir> [scale]. <run dir> holds events.log and cap/ with fNNNN.png grabs of the
576x384 MAP's client area (top-left aligned) and times.txt ("index start_ms end_ms"). scale is the
client's DPI scale (1 for stock DreamSeeker, 1.5 for Fugue at 144 DPI). Matrices use DM's convention
(x' = a x + b y + c, y' = d x + e y + f, y up, pixels from the icon centre).
"""
import json
import sys
from pathlib import Path
from PIL import Image
run = Path(sys.argv[1]); cap = run / 'cap'; K = float(sys.argv[2]) if len(sys.argv) > 2 else 1.0
# events.log is appended to by every run; each run logs the same cases in the same order.
names = list(dict.fromkeys(l.split()[1] for l in (run / 'events.log').read_text().splitlines() if l.startswith('CASE')))
times = {int(l.split()[0]): (float(l.split()[1]) + float(l.split()[2])) / 2
         for l in (cap / 'times.txt').read_text().splitlines() if l.strip()}
def features(px, size, cx, cy, radius):
    acc = {k: [0.0, 0.0, 0.0] for k in 'rgb'}
    for y in range(max(0, int(cy - radius)), min(size[1], int(cy + radius))):
        for x in range(max(0, int(cx - radius)), min(size[0], int(cx + radius))):
            r, g, b = px[x, y][:3]
            for k, w in (('r', r - max(g, b)), ('g', g - max(r, b)), ('b', b - max(r, g))):
                if w > 20:
                    a = acc[k]; a[0] += w; a[1] += (x + 0.5 - cx) / K * w; a[2] += -(y + 0.5 - cy) / K * w
    return {k: (a[1] / a[0], a[2] / a[0]) if a[0] else None for k, a in acc.items()}
out = []
for i in sorted(times):
    im = Image.open(cap / f'f{i:04d}.png').convert('RGB'); px = im.load()
    y = int((11 * 32 + 27.5) * K)
    xs = [x for x in range(min(im.size[0], int(576 * K))) if min(px[x, y]) > 200]
    clock = (sum(xs) / len(xs) + 0.5 - K / 2 - 32 * K) / (320 * K) if xs else None
    row = {'i': i, 't': times[i], 'clock': clock, 'm': {}}
    for k, n in enumerate(names):
        tx, ty = 2 + (k % 6) * 3, 10 - (k // 6) * 3
        f = features(px, im.size, ((tx - 1) * 32 + 16) * K, ((12 - ty) * 32 + 16) * K, 46 * K)
        if None in f.values():
            row['m'][n] = None
            continue
        (rx, ry), (gx, gy), (bx, by) = f['r'], f['g'], f['b']
        a, d = (rx - bx) / 18, (ry - by) / 18
        c, ff = rx - 8 * a, ry - 8 * d
        b, e = (gx - c) / 6, (gy - ff) / 6
        row['m'][n] = [round(v, 4) for v in (a, b, c, d, e, ff)]
    out.append(row)
json.dump(out, open(run / 'mats.json', 'w'))
print(len(out), 'frames')
