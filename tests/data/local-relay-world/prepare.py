"""Stage the retained stock fixture, generating its described asymmetric icon states."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw, PngImagePlugin
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
target.mkdir(parents=True, exist_ok=True)
group = '--group' in sys.argv[2:]
shutil.copy2(source / ('group-world.dm' if group else 'world.dm'), target / 'world.dm')
if group:
    (target / 'group.txt').write_text('group\n')
(target / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
(target / 'world.dmf').write_text('''window "main"
    elem "main"
        type = MAIN
        size = 448x448
        is-default = true
    elem "map"
        type = MAP
        pos = 0,0
        size = 448x448
        anchor1 = 0,0
        anchor2 = 100,100
        is-default = true
''')
states = ['floor','mob','rock','st','nst','img']
im = Image.new('RGBA', (32*len(states),32))
d = ImageDraw.Draw(im)
colors = [(35,35,35,255),(80,180,80,255),(90,90,180,255),(240,30,30,255),(230,210,30,255),(60,210,210,255)]
for i, color in enumerate(colors):
    box = (i*32,0,i*32+(15 if i>=3 else 31),15 if i>=3 else 31)
    d.rectangle(box, fill=color)
info = PngImagePlugin.PngInfo()
info.add_text('Description','# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n' + ''.join(f'state = "{s}"\n\tdirs = 1\n\tframes = 1\n' for s in states) + '# END DMI\n')
im.save(target/'z.dmi',format='PNG',pnginfo=info)
print(target)
