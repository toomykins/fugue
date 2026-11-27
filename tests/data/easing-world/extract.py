"""Turn timestamped captures of the fixture's MAP into marker fractions (frac.json).

Usage: extract.py <capture dir> [scale]. The directory holds fNNNN.png crops of the
576x384 MAP's client area, top-left aligned, plus times.txt ("index start_ms end_ms").
scale is the client's DPI scale (1 for stock DreamSeeker, 1.5 for Fugue at 144 DPI).
"""
import sys, json
from pathlib import Path
from PIL import Image
d = Path(sys.argv[1]); K = float(sys.argv[2]) if len(sys.argv)>2 else 1.0; X0, X1 = 64*K, 384*K
times = {int(l.split()[0]): (float(l.split()[1])+float(l.split()[2]))/2 for l in (d/'times.txt').read_text().split('\n') if l.strip()}
names = ['0:0'] + [f'{t}:{m}' for t in range(1,9) for m in (0,64,128,192)]
out = []
for i in sorted(times):
    im = Image.open(d/f'f{i:04d}.png').convert('RGB'); px = im.load(); w,h = im.size
    row = {}
    for k,n in enumerate(names):
        y = int(((k//4)*32 + 27.5 - (k%4)*8)*K)
        xs = [x for x in range(w) if min(px[x,y])>200]
        row[n] = (sum(xs)/len(xs) + 0.5 - K/2 - X0)/(X1-X0) if xs else None
    out.append({'i':i,'t':times[i],'f':row})
json.dump(out, open(d/'frac.json','w'), indent=0)
for o in out[:8]: print(round(o['t']), {k:(round(v,3) if v is not None else None) for k,v in list(o['f'].items())[:6]})
