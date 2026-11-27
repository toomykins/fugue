"""Generate the focused object-image fixture in a separate runtime (requires Pillow)."""
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
        size = 480x480
        is-default = true
    elem "map"
        type = MAP
        pos = 0,0
        size = 480x480
        anchor1 = 0,0
        anchor2 = 100,100
        is-default = true
''')
for name in ('base', 'overlay'):
    im = Image.new('RGBA', (32,32))
    d = ImageDraw.Draw(im)
    if name == 'base':
        d.rectangle((4,4,9,27), fill=(240,40,40,255))
        d.rectangle((10,21,24,27), fill=(40,190,240,255))
    else:
        d.rectangle((23,3,28,8), fill=(255,220,0,255))
        d.rectangle((13,14,16,17), fill=(40,230,70,255))
    im.save(target / (name + '.png'))
print(target)
