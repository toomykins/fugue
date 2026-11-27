"""Generate the vis_contents fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw
from PIL.PngImagePlugin import PngInfo
source = Path(__file__).resolve().parent
t = Path(sys.argv[1]).resolve()
if t == source:
    raise SystemExit('Use a separate runtime directory')
t.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', t / 'world.dm')
(t / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
(t / 'world.dmf').write_text('''window "main"
    elem "main"
        type = MAIN
        size = 576x384
        is-default = true
    elem "map"
        type = MAP
        pos = 0,0
        size = 576x384
        anchor1 = 0,0
        anchor2 = 100,100
        icon-size = 32
        is-default = true
''')

def dmi(path, size, states):
    """states: list of (name, [frame per dir]) with 4 dirs (S, N, E, W) each drawn by a callable."""
    cells = [f for _, frames in states for f in frames]
    sheet = Image.new('RGBA', (size * len(cells), size), (0, 0, 0, 0))
    for i, draw in enumerate(cells):
        im = Image.new('RGBA', (size, size), (0, 0, 0, 0)); draw(ImageDraw.Draw(im)); sheet.paste(im, (i * size, 0))
    desc = '# BEGIN DMI\nversion = 4.0\n\twidth = %d\n\theight = %d\n' % (size, size)
    for name, frames in states:
        desc += 'state = "%s"\n\tdirs = %d\n\tframes = 1\n' % (name, len(frames))
    desc += '# END DMI\n'
    info = PngInfo(); info.add_text('Description', desc, zip=True)
    sheet.save(path, 'PNG', pnginfo=info)

# Each state: a body colour, and a white notch on the facing edge (S, N, E, W).
def body(colour, box, notch):
    def draw(d):
        d.rectangle(box, fill=colour)
        x0, y0, x1, y1 = box; mx, my = (x0 + x1) // 2, (y0 + y1) // 2
        d.rectangle({'S': (mx - 1, y1 - 2, mx + 1, y1), 'N': (mx - 1, y0, mx + 1, y0 + 2),
                     'E': (x1 - 2, my - 1, x1, my + 1), 'W': (x0, my - 1, x0 + 2, my + 1)}[notch], fill=(255, 255, 255, 255))
    return draw
def four(colour, box):
    return [body(colour, box, n) for n in 'SNEW']
hbox, kbox = (4, 4, 27, 27), (8, 8, 23, 23)
dmi(t / 'holder.dmi', 32, [('a', four((40, 80, 230, 255), hbox)), ('b', four((40, 200, 230, 255), hbox))])
dmi(t / 'kid.dmi', 32, [('a', four((240, 120, 0, 255), kbox)), ('b', four((230, 40, 40, 255), kbox))])
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')
im = Image.new('RGBA', (32, 32), (60, 20, 60, 255)); d = ImageDraw.Draw(im)
d.rectangle((0, 0, 31, 31), outline=(200, 0, 200, 255)); d.rectangle((1, 1, 6, 6), fill=(255, 255, 255, 255)); im.save(t / 'remote.png')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); ImageDraw.Draw(im).rectangle((26, 0, 31, 31), fill=(120, 120, 120, 255)); im.save(t / 'wall.png')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); ImageDraw.Draw(im).ellipse((16, 16, 29, 29), fill=(40, 200, 60, 255)); im.save(t / 'thing.png')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
d.rectangle((2, 12, 11, 29), fill=(240, 220, 40, 255)); d.rectangle((4, 14, 6, 16), fill=(0, 0, 0, 255)); im.save(t / 'npc.png')
