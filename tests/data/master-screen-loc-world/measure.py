"""Measure stock captures for stock-observations.json.

  python measure.py <out> [--copy]   entries for the phases of a collect.py run (<out>/record.json), printed as JSON
  python measure.py --add-copies     adds "copies" to the entries of stock-observations.json that lack it

An entry made here counts the whole map (crop 0,0,224,224): the pixels of each marker colour (outline white, part red,
shadow green, overlay cyan) and of black, each colour's bounds (left, top, right, bottom), and "copies": the left, top
and pixel count of every separate cyan square, from the top. With --copy the capture is copied next to this script as
stock-<phase>.png, in the spelling the older captures use (1,1 to 3:8,3|ov is stock-1-1-to-3-8-3-ov.png)."""
import json, shutil, sys
from pathlib import Path
from PIL import Image
here = Path(__file__).resolve().parent
colours = {'outline': (255, 255, 255), 'part': (240, 30, 30), 'shadow': (0, 255, 0), 'overlay': (60, 210, 210),
           'black': (0, 0, 0)}

def name_of(phase):
    s = phase.replace('|', '-').replace(' to ', '-to-').replace(',', '-').replace(':', '-').replace('%', 'pc')
    return 'stock-' + s.replace(' ', '-').replace('+', 'p').lower() + '.png'

def copies(px):
    seen = set(); found = []
    for y in range(224):
        for x in range(224):
            if px[x, y] == colours['overlay'] and (x, y) not in seen:
                todo = [(x, y)]; seen.add((x, y)); xs = []; ys = []
                while todo:
                    a, b = todo.pop(); xs.append(a); ys.append(b)
                    for c, d in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                        if 0 <= c < 224 and 0 <= d < 224 and (c, d) not in seen and px[c, d] == colours['overlay']:
                            seen.add((c, d)); todo.append((c, d))
                found.append([min(xs), min(ys), len(xs)])
    return sorted(found, key=lambda c: (c[1], c[0]))

def entry(phase, path, stable):
    px = Image.open(path).convert('RGB').crop((0, 0, 224, 224)).load()
    counts = {k: 0 for k in colours}; bounds = {}
    for y in range(224):
        for x in range(224):
            for k, c in colours.items():
                if px[x, y] == c:
                    counts[k] += 1
                    if k != 'black':
                        b = bounds.get(k, [x, y, x, y]); bounds[k] = [min(b[0], x), min(b[1], y), max(b[2], x), max(b[3], y)]
    return {'phase': phase, 'image': name_of(phase), 'stable': stable, 'crop': [0, 0, 224, 224],
            'counts': counts, 'bounds': bounds, 'copies': copies(px)}

if sys.argv[1:2] == ['--add-copies']:
    path = here / 'stock-observations.json'; doc = json.loads(path.read_text())
    for p in doc['phases']:
        if 'copies' not in p:
            p['copies'] = copies(Image.open(here / p['image']).convert('RGB').crop((0, 0, 224, 224)).load())
    path.write_text(json.dumps(doc, indent=1) + '\n')
else:
    out = Path(sys.argv[1]); record = json.loads((out / 'record.json').read_text())
    entries = [entry(p['phase'], out / p['image'], p['stable']) for p in record['phases']]
    if '--copy' in sys.argv:
        for p, e in zip(record['phases'], entries): shutil.copy2(out / p['image'], here / e['image'])
    print(json.dumps(entries, indent=1))
