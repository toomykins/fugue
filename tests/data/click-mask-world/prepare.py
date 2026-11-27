"""Generate the click fixture for masks, groups and planes in a separate runtime directory (requires Pillow)."""
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
icon('big.png', 32, [((4, 4, 27, 27), (40, 200, 230, 255))])
icon('shape.png', 32, [((8, 8, 23, 23), (240, 220, 40, 255)), ((24, 12, 25, 19), (240, 220, 40, 128))])
icon('sq.png', 8, [], white)
icon('mask.png', 32, [((0, 0, 15, 31), white), ((16, 8, 31, 15), (255, 255, 255, 128))])
icon('mask96.png', 96, [((0, 0, 47, 95), white), ((48, 24, 95, 47), (255, 255, 255, 128))])

# The points to click, in map pixels from the top-left, and their labels (case@x,y inside the case's tile).
points = {'img-kt': [(20, 20), (6, 6), (30, 16)], 'mask-icon': [(8, 20), (22, 22), (22, 11)],
          'mask-inverse': [(8, 20), (22, 22)], 'mask-swap': [(8, 20), (22, 22)], 'particle': [(16, 1), (16, 4), (16, 16)],
          'kt-anim-turn': [(16, 16), (5, 5), (16, 2)], 'kt-turn-opaque2': [(16, 16), (5, 5), (16, 2), (1, 1)],
          'flat-turn-opaque2': [(16, 16), (5, 5), (16, 2), (1, 1)], 'plane-shift': [(30, 16), (6, 16), (16, 16)],
          'plane-mask': [(16, 16), (6, 6)], 'two-outlines': [(2, 16), (16, 16)], 'plane-blur': [(16, 16), (3, 16)],
          'plane-outline': [(16, 16), (3, 16)], 'plane-color': [(16, 16)], 'plane-mask-hide': [(16, 16)],
          'plane-plain': [(16, 16)], 'plane-alpha0': [(16, 16)], 'plain': [(16, 16), (1, 1)]}
out, labels = [(300, 370)], ['throwaway']
for k, (name, where) in enumerate(points.items()):
    ox, oy = (1 + (k % 6) * 3) * 32, (12 - (10 - (k // 6) * 3)) * 32
    for x, y in where:
        out.append((ox + x, oy + y))
        labels.append('%s@%d,%d' % (name, x, y))
(t / 'points.txt').write_text(';'.join('%d,%d' % p for p in out))
(t / 'labels.txt').write_text('\n'.join(labels))
