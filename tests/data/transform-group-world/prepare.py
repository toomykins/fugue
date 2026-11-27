"""Generate the transform group fixture in a separate runtime directory (requires Pillow)."""
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
(t / 'world.dmf').write_bytes(b'''window "main"
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
Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(t / 'floor.png')
# A 16x16 box with a softer step on one side, so filters meet both hard and translucent edges.
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0)); d = ImageDraw.Draw(im)
d.rectangle((8, 8, 23, 23), fill=(240, 220, 40, 255)); d.rectangle((24, 12, 25, 19), fill=(240, 220, 40, 128))
d.rectangle((10, 10, 13, 13), fill=(255, 255, 255, 255)); im.save(t / 'shape.png')
im = Image.new('RGBA', (16, 16), (0, 0, 0, 0)); ImageDraw.Draw(im).rectangle((2, 2, 9, 9), fill=(40, 80, 230, 255)); im.save(t / 'small.png')
