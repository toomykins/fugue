"""Compare 480x480 stock/native MAP crops from owned fixture captures.

Usage: compare.py stock.png native.png
Stock MAP origin is (8,31); native (8,49) after resizing ONLY the owned native
window to 496x555. The reset case is deliberately refused, not compared as parity.
"""
import json
import sys
from PIL import Image
maps=[Image.open(p).convert('RGB').crop((8,y,488,y+480)) for p,y in zip(sys.argv[1:3],(31,49))]
results={}
for name,x,y in [('absent',3,12),('changing',8,12),('rotate',13,12),('reverse',3,8),
                 ('shrink',8,8),('nested mob',13,8),('wide root',3,4),('square children',8,4)]:
    ox=(x-1)*32; oy=(15-y)*32
    a,b=[m.crop((ox-32,oy-40,min(480,ox+88),oy+64)) for m in maps]
    errors=[max(abs(u-v) for u,v in zip(a.getpixel((px,py)),b.getpixel((px,py))))
            for py in range(a.height) for px in range(a.width)]
    results[name]={'different':sum(e!=0 for e in errors),'overOneLSB':sum(e>1 for e in errors),'max':max(errors)}
print(json.dumps(results,indent=2))
raise SystemExit(any(r['overOneLSB'] for r in results.values()))
