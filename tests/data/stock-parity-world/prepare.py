"""Generate the stock parity fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw, PngImagePlugin
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


def icon(name, size=32, boxes=()):
    im = Image.new('RGBA', (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for box, fill in boxes:
        d.rectangle(box, fill=fill)
    im.save(t / name)


Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')
Image.new('RGBA', (32, 32), (255, 255, 255, 255)).save(t / 'white.png')
# A 16 px yellow box with a softer step on its right and a white corner.
icon('shape.png', boxes=[((8, 8, 23, 23), (240, 220, 40, 255)), ((24, 12, 25, 19), (240, 220, 40, 128)),
                         ((10, 10, 13, 13), (255, 255, 255, 255))])
icon('small.png', 16, [((2, 2, 9, 9), (40, 80, 230, 255))])
icon('big.png', boxes=[((4, 4, 27, 27), (40, 200, 230, 255))])
icon('cover.png', boxes=[((8, 0, 15, 31), (230, 40, 40, 255))])    # left of the yellow box
icon('top.png', boxes=[((0, 8, 31, 15), (40, 200, 60, 255))])      # top of the yellow box
icon('adda.png', boxes=[((8, 8, 19, 19), (200, 0, 0, 255))])
icon('addb.png', boxes=[((14, 14, 25, 25), (0, 0, 200, 255))])
# The mask: opaque on the left half, half alpha in a band on the right.
icon('mask.png', boxes=[((0, 0, 15, 31), (255, 255, 255, 255)), ((16, 8, 31, 15), (255, 255, 255, 128))])
# A 96 px mask: opaque left half, a half-alpha band on the right.
icon('mask96.png', 96, [((0, 0, 47, 95), (255, 255, 255, 255)), ((48, 24, 95, 47), (255, 255, 255, 128))])
icon('big64.png', 64, [((4, 4, 59, 59), (200, 120, 40, 255)), ((8, 8, 23, 23), (255, 255, 255, 255))])
# Icons that tell where a turned surface ends (surface-bounds): a full tile, an L, a box with two alpha-1 pixels in the
# icon's corners, single pixels, a box with a half-alpha rim, and a 23 px box off the icon's centre.
Image.new('RGBA', (32, 32), (200, 120, 40, 255)).save(t / 'full.png')
icon('ell.png', boxes=[((4, 4, 27, 15), (40, 200, 230, 255)), ((4, 16, 15, 27), (40, 200, 230, 255))])
icon('faint.png', boxes=[((8, 8, 23, 23), (40, 200, 230, 255)), ((0, 0, 0, 0), (255, 255, 255, 1)),
                         ((31, 31, 31, 31), (255, 255, 255, 1))])
icon('dot.png', boxes=[((16, 16, 16, 16), (255, 255, 255, 255))])
icon('dot2.png', boxes=[((18, 16, 18, 16), (255, 60, 60, 255)), ((16, 19, 16, 19), (60, 255, 60, 255))])
icon('rim.png', boxes=[((4, 4, 27, 27), (40, 200, 230, 128)), ((5, 5, 26, 26), (40, 200, 230, 255))])
icon('odd.png', boxes=[((5, 4, 27, 26), (40, 200, 230, 255))])

# Icon files with directions, frames and movement states (directions, icon-states). A state's body colour names the state, a
# white-edged marker names the direction it was drawn for, and a centre dot the frame.
MARK = [((13, 24), (255, 255, 255)), ((13, 2), (255, 60, 60)), ((24, 13), (60, 255, 60)), ((2, 13), (60, 60, 255)),
        ((24, 24), (255, 255, 60)), ((2, 24), (255, 60, 255)), ((24, 2), (60, 255, 255)), ((2, 2), (160, 160, 160))]
DOT = [None, (0, 0, 0), (255, 255, 255)]


def dmi(name, states, size=32):
    # states: (name, body colour, dirs, frames, extra lines)
    k = size // 32
    cells = sum(s[2] * s[3] for s in states)
    sheet = Image.new('RGBA', (size * cells, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(sheet)
    text = '# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n' % (size, size)
    x = 0
    for state, body, dirs, frames, extra in states:
        text += 'state = "%s"\n\tdirs = %d\n\tframes = %d\n' % (state, dirs, frames) + ''.join('\t%s\n' % e for e in extra)
        for f in range(frames):
            for i in range(dirs):
                d.rectangle((x + 10 * k, 10 * k, x + 22 * k - 1, 22 * k - 1), fill=body + (255,))
                if dirs > 1:
                    (mx, my), c = MARK[i]
                    d.rectangle((x + mx * k, my * k, x + (mx + 6) * k - 1, (my + 6) * k - 1), fill=c + (255,))
                if DOT[f]:
                    d.rectangle((x + 14 * k, 14 * k, x + 18 * k - 1, 18 * k - 1), fill=DOT[f] + (255,))
                x += size
    info = PngImagePlugin.PngInfo()
    info.add_text('Description', text + '# END DMI\n', zip=True)
    sheet.save(t / name, 'PNG', pnginfo=info)


STATES = [('', (120, 120, 120), 1, 1, []), ('d1', (220, 40, 40), 1, 1, []), ('d4', (40, 180, 60), 4, 1, []),
          ('d8', (40, 80, 230), 8, 1, []), ('anim4', (230, 200, 40), 4, 2, ['delay = 3000,3000']),
          ('once', (220, 40, 220), 1, 3, ['delay = 1,1,1', 'loop = 1']), ('walk', (40, 200, 220), 4, 1, []),
          ('walk', (240, 140, 30), 4, 1, ['movement = 1']), ('run', (240, 140, 30), 4, 1, ['movement = 1']),
          ('slow', (230, 230, 230), 1, 2, ['delay = 80,80'])]
dmi('dirs.dmi', STATES)
dmi('dirs64.dmi', [s for s in STATES if s[0] in ('d4', 'anim4')], 64)
dmi('nodefault.dmi', [('x', (220, 40, 40), 1, 1, [])])
