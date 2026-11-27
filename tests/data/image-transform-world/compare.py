"""Compare native-resolution owned stock/native MAP crops; no whole-window claim.

Usage: compare.py stock.png native.png [stock-map-y=31] [native-map-y=49]
Both probe windows have a 480x480 MAP at x=8. Resize only the owned native
window to 496x555 (its menu occupies 18 extra pixels). Preserve original captures.
"""
import json
import sys
from PIL import Image
stock_y = int(sys.argv[3]) if len(sys.argv) > 3 else 31
native_y = int(sys.argv[4]) if len(sys.argv) > 4 else 49
stock = Image.open(sys.argv[1]).convert('RGB').crop((8, stock_y, 488, stock_y+480))
native = Image.open(sys.argv[2]).convert('RGB').crop((8, native_y, 488, native_y+480))
results = {}
for name, x, y in [('absent',3,12), ('changing pose',8,12), ('rotation',13,12),
                    ('half size',3,8), ('translation offsets',8,8),
                    ('rotation offsets',13,8), ('half size offsets',3,4)]:
    box = ((x-1)*32-16, (15-y)*32-48, min(480,(x-1)*32+96), (15-y)*32+48)
    a, b = stock.crop(box), native.crop(box)
    errors = [max(abs(u-v) for u,v in zip(a.getpixel((px,py)),b.getpixel((px,py))))
              for py in range(a.height) for px in range(a.width)]
    results[name] = {'different_pixels': sum(e != 0 for e in errors),
                     'over_one_lsb': sum(e > 1 for e in errors), 'max_channel_error': max(errors)}
print(json.dumps(results, indent=2))
raise SystemExit(any(r['over_one_lsb'] for r in results.values()))
