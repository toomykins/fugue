"""Generate the master transform fixture in a separate runtime directory (requires Pillow)."""
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
skin = '\n'.join(['window "main"', '    elem "main"', '        type = MAIN', '        size = 224x224',
                  '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
                  '        size = 224x224', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 32',
                  '        is-default = true', ''])
(t / 'world.dmf').write_bytes(skin.encode())
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')
def box(fill, lo=8, hi=23):
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    ImageDraw.Draw(im).rectangle((lo, lo, hi, hi), fill=fill)
    return im


box((200, 60, 60, 255)).save(t / 'box.png')        # 16x16 at 8..23
box((40, 80, 230, 255)).save(t / 'blue.png')
Image.new('RGBA', (32, 32), (40, 80, 230, 255)).save(t / 'full.png')  # a whole tile, for the fx-*edge* cases
mask = Image.new('RGBA', (32, 32), (0, 0, 0, 0))                       # left half opaque, for the fx-*mask cases
ImageDraw.Draw(mask).rectangle((0, 0, 15, 31), fill=(255, 255, 255, 255))
mask.save(t / 'mask.png')
# The whole view, for the fx-grid cases: red is the pixel's column and green its row, so every pixel names the source
# pixel it came from; blue is a one-pixel checker, which bilinear sampling greys and nearest sampling keeps.
grid = Image.new('RGBA', (224, 224))
grid.putdata([(x, y, 255 if (x + y) % 2 == 0 else 0, 255) for y in range(224) for x in range(224)])
grid.save(t / 'grid.png')
Image.new('RGBA', (224, 224), (255, 255, 255, 255)).save(t / 'cover.png')  # an alpha mask that hides nothing (cover)
