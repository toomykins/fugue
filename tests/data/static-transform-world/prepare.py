"""Generate the static transform fixture in a separate runtime directory (requires Pillow)."""
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
Image.new('RGBA', (32,32), (16,16,16,255)).save(t/'floor.png')
im = Image.new('RGBA', (32,32), (0,0,0,0)); d = ImageDraw.Draw(im)
d.rectangle((4,4,15,15), fill=(230,40,40,255)); d.rectangle((16,4,27,15), fill=(40,200,60,255))
d.rectangle((4,16,15,27), fill=(40,80,230,255)); d.rectangle((16,16,27,27), fill=(240,220,40,255))
for x in range(4,28,2): d.point((x,2), fill=(255,255,255,255))
im.save(t/'quad.png')
im = Image.new('RGBA', (16,16), (0,0,0,0)); d = ImageDraw.Draw(im)
d.rectangle((2,2,13,13), fill=(240,120,0,255)); d.rectangle((2,2,6,6), fill=(255,255,255,255)); im.save(t/'small.png')
im = Image.new('RGBA', (48,48), (0,0,0,0)); d = ImageDraw.Draw(im)
d.rectangle((2,2,45,45), outline=(0,230,230,255), width=2); d.rectangle((2,2,12,12), fill=(255,0,255,255)); im.save(t/'big.png')
