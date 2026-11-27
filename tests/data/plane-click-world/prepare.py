"""Generate the plane click fixture in a separate runtime directory (requires Pillow)."""
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


box((200, 60, 60, 255)).save(t / 'box.png')        # 16x16 at 8..23: the world object
box((40, 80, 230, 255)).save(t / 'blue.png')       # the vis_contents member and the master's overlay
box((60, 200, 60, 255)).save(t / 'green.png')      # the client image
mask = Image.new('RGBA', (32, 32), (0, 0, 0, 0))   # left half opaque, for the alpha masks
ImageDraw.Draw(mask).rectangle((0, 0, 15, 31), fill=(255, 255, 255, 255))
mask.save(t / 'mask.png')
