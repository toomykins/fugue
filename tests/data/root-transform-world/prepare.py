"""Generate ordinary appearance-tree probes in a separate runtime (Pillow)."""
from pathlib import Path
import shutil
import sys
from PIL import Image, ImageDraw
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime')
target.mkdir(parents=True, exist_ok=True)
shutil.copy2(source/'world.dm', target/'world.dm')
(target/'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
(target/'world.dmf').write_text('''window "main"
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
for name, size, color in [('base',(32,32),(240,40,40,255)),
                          ('wide',(48,16),(40,180,240,255)),
                          ('tall',(16,48),(240,40,220,255)),
                          ('leaf',(16,16),(40,230,70,255)),
                          ('square',(32,32),(255,220,0,255))]:
    im=Image.new('RGBA',size);d=ImageDraw.Draw(im)
    d.rectangle((2,3,5,size[1]-3),fill=color)
    d.rectangle((6,size[1]-6,size[0]-3,size[1]-3),fill=color)
    im.save(target/(name+'.png'))
print(target)
