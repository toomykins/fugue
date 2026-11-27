"""Generate the particle holder fixture in a separate runtime directory (requires Pillow)."""
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


def icon(name, size, boxes, fill=(0, 0, 0, 0)):
    im = Image.new('RGBA', (size, size), fill)
    d = ImageDraw.Draw(im)
    for box, colour in boxes:
        d.rectangle(box, fill=colour)
    im.save(t / name)


white = (255, 255, 255, 255)
icon('floor.png', 32, [], (16, 16, 16, 255))
icon('small.png', 16, [((2, 2, 9, 9), (40, 80, 230, 255))])
icon('big.png', 32, [((4, 4, 27, 27), (40, 200, 230, 255))])
icon('sq.png', 8, [], white)
icon('ell.png', 16, [((0, 0, 15, 7), white), ((0, 8, 7, 15), white)])   # an L, so turns and flips show
