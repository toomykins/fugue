"""Generate independent RGBA basis tiles; stage only below build/."""
from pathlib import Path
import shutil
import sys
from PIL import Image
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target.exists() or 'build' not in target.parts:
    raise SystemExit('Use a new build/ runtime')
target.mkdir(parents=True)
for name in ('world.dm', 'world.dme', 'skin.dmf'):
    shutil.copy2(source/name, target/name)
im = Image.new('RGBA', (32,32))
# Distinguish all four input channels, including RGB at zero alpha.
values = [(255,0,0,0),(0,255,0,64),(0,0,255,128),(0,0,0,255),
          (63,67,59,64),(128,64,32,128),(0,0,0,0),(255,255,255,255)]
for y in range(32):
    for x in range(32):
        im.putpixel((x,y), values[(y//16)*4+x//8])
im.save(target/'basis.png')
Image.new('RGBA',(32,32),(160,120,80,255)).save(target/'floor.png')
print(target)
