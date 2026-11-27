"""Generate the wide icon transform fixture (64 px icons) in a separate runtime directory (requires Pillow)."""
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
                  '        size = 576x384', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 64',
                  '        is-default = true', ''])
(t / 'world.dmf').write_bytes(skin.encode())
Image.new('RGBA', (64, 64), (16, 16, 16, 255)).save(t / 'floor.png')
# A 32 px yellow box with a white corner (shows the turn) and a translucent step on its right, in a 64 px icon.
im = Image.new('RGBA', (64, 64), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle((16, 16, 47, 47), fill=(240, 220, 40, 255))
d.rectangle((48, 24, 51, 39), fill=(240, 220, 40, 128))
d.rectangle((20, 20, 27, 27), fill=(255, 255, 255, 255))
im.save(t / 'shape.png')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
ImageDraw.Draw(im).rectangle((4, 4, 19, 19), fill=(40, 80, 230, 255))
im.save(t / 'small.png')
