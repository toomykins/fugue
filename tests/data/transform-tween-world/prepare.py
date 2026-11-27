"""Generate the transform tween fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw
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
Image.new('RGBA', (32, 32), (0, 0, 0, 255)).save(target / 'floor.png')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
d = ImageDraw.Draw(im)
d.rectangle((16, 15, 31, 16), fill=(255, 0, 0, 255))   # east arm: centroid (8, 0) from the centre
d.rectangle((15, 4, 16, 15), fill=(0, 255, 0, 255))    # north arm: centroid (0, 6)
d.rectangle((4, 15, 7, 16), fill=(0, 0, 255, 255))     # west dot: centroid (-10, 0)
im.save(target / 'arm.png')
for name, colour in (('mark', (255, 255, 255, 255)), ('fixed', (255, 0, 0, 255))):
    im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    ImageDraw.Draw(im).rectangle((0, 26, 1, 29), fill=colour)
    im.save(target / (name + '.png'))
print(target)
