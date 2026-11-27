"""Generate the filter reach fixture in a separate runtime directory (requires Pillow)."""
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
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')


def shape(fill=(240, 220, 40, 255)):
    # A 16x16 box with a softer step on one side and a white corner, so filters meet hard and translucent edges.
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rectangle((8, 8, 23, 23), fill=fill)
    d.rectangle((24, 12, 25, 19), fill=fill[:3] + (128,))
    d.rectangle((10, 10, 13, 13), fill=(255, 255, 255, 255))
    return im


shape().save(t / 'shape.png')
im = Image.new('RGBA', (16, 16), (0, 0, 0, 0))
ImageDraw.Draw(im).rectangle((2, 2, 9, 9), fill=(40, 80, 230, 255))
im.save(t / 'small.png')
# anim.dmi: "still" (the yellow box) and a two-frame "blink" (a cyan box, then the yellow one), 1 s per frame.
sheet = Image.new('RGBA', (96, 32), (0, 0, 0, 0))
sheet.paste(shape(), (0, 0))
sheet.paste(shape((40, 220, 230, 255)), (32, 0))
sheet.paste(shape(), (64, 0))
info = PngImagePlugin.PngInfo()
info.add_text('Description', '# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\nstate = "still"\n\tdirs = 1\n'
              '\tframes = 1\nstate = "blink"\n\tdirs = 1\n\tframes = 2\n\tdelay = 10,10\n# END DMI\n', zip=True)
sheet.save(t / 'anim.dmi', 'PNG', pnginfo=info)
