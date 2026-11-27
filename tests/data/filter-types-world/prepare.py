"""Generate the filter types fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw
source = Path(__file__).resolve().parent
t = Path(sys.argv[1]).resolve()
if t == source:
    raise SystemExit('Use a separate runtime directory')
t.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', t / 'world.dm')
(t / 'world.dme').write_bytes(b'#include "world.dmf"\n#include "world.dm"\n')
skin = '\n'.join(['window "main"', '    elem "main"', '        type = MAIN', '        size = 576x384',
                  '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
                  '        size = 576x384', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 32',
                  '        is-default = true', ''])
(t / 'world.dmf').write_bytes(skin.encode())
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')
# grid.png: a 2 px yellow/blue checker over x,y 6..25 with a white 4 px block, so displacement shows everywhere.
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
for y in range(6, 26):
    for x in range(6, 26):
        im.putpixel((x, y), (240, 220, 40, 255) if (x // 2 + y // 2) % 2 else (40, 80, 230, 255))
ImageDraw.Draw(im).rectangle((8, 8, 11, 11), fill=(255, 255, 255, 255))
im.save(t / 'grid.png')
# shape.png: the filter fixtures' 16x16 box with a translucent step and a white corner.
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle((8, 8, 23, 23), fill=(240, 220, 40, 255))
d.rectangle((24, 12, 25, 19), fill=(240, 220, 40, 128))
d.rectangle((10, 10, 13, 13), fill=(255, 255, 255, 255))
im.save(t / 'shape.png')
