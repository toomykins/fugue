"""Prepare the direct-set observation world in a separate directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, PngImagePlugin

source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
target.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', target / 'world.dm')
(target / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
(target / 'world.dmf').write_text('''window "main"
    elem "main"
        type = MAIN
        size = 352x352
        is-default = true
    elem "map"
        type = MAP
        pos = 0,0
        size = 352x352
        anchor1 = 0,0
        anchor2 = 100,100
        icon-size = 32
        is-default = true
''')

# One resource keeps phase cues available before their state changes arrive.
states = [
    ('floor', (96, 96, 96, 255)),
    ('player', (200, 40, 40, 255)),
    ('clock', (255, 255, 255, 255)),
    ('idle', (20, 180, 220, 255)),
    ('running', (220, 180, 20, 255)),
    ('assigned', (180, 20, 220, 255)),
    ('settled', (20, 220, 80, 255)),
]
image = Image.new('RGBA', (32 * len(states), 32), (0, 0, 0, 0))
description = '# BEGIN DMI\nversion = 4.0\n\twidth = 32\n\theight = 32\n'
for index, (name, colour) in enumerate(states):
    description += f'state = "{name}"\n\tdirs = 1\n\tframes = 1\n'
    x = index * 32
    image.paste(colour, (x, 0, x + (2 if name == 'clock' else 32), 6 if name == 'clock' else 32))
description += '# END DMI\n'
info = PngImagePlugin.PngInfo()
info.add_text('Description', description, zip=True)
image.save(target / 'probe.dmi', format='PNG', pnginfo=info)
print(target)
