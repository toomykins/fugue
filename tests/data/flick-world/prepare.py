"""Generate the flick probe in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw, PngImagePlugin
source = Path(__file__).resolve().parent
t = Path(sys.argv[1]).resolve()
t.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', t / 'world.dm')
(t / 'world.dme').write_bytes(b'#include "world.dmf"\n#include "world.dm"\n')
skin = '\n'.join(['window "main"', '    elem "main"', '        type = MAIN', '        size = 576x384',
                  '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
                  '        size = 576x384', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 32',
                  '        is-default = true', ''])
(t / 'world.dmf').write_bytes(skin.encode())
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')


def box(fill):
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    ImageDraw.Draw(im).rectangle((8, 8, 23, 23), fill=fill)
    return im


def dmi(name, states):
    # states: (name, colour, frames); every frame lasts 30 s
    n = sum(s[2] for s in states)
    sheet = Image.new('RGBA', (32 * n, 32), (0, 0, 0, 0))
    text = '# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n'
    x = 0
    for state, fill, frames in states:
        text += 'state = "%s"\n\tdirs = 1\n\tframes = %d\n' % (state, frames)
        if frames > 1:
            text += '\tdelay = ' + ','.join(['300'] * frames) + '\n'
        for _ in range(frames):
            sheet.paste(box(fill), (x, 0))
            x += 32
    info = PngImagePlugin.PngInfo()
    info.add_text('Description', text + '# END DMI\n', zip=True)
    sheet.save(t / name, 'PNG', pnginfo=info)


dmi('anim.dmi', [('still', (240, 220, 40, 255), 1), ('flash', (40, 220, 230, 255), 2),
                 ('other', (40, 230, 60, 255), 2)])
dmi('other.dmi', [('still', (230, 40, 230, 255), 2)])
