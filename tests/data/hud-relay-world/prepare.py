"""Independent unequal HUD anchors and plane PASS_MOUSE fixture; assets stay in build/."""
from pathlib import Path
import shutil
import sys
from PIL import Image


def prepare(dest, wide=False):
    dest.mkdir(parents=True, exist_ok=True)
    here = Path(__file__).resolve().parent
    shutil.copy2(here / 'world.dm', dest / 'world.dm')
    (dest / 'world.dme').write_text('#include "skin.dmf"\n#include "world.dm"\n')
    (dest / 'skin.dmf').write_text('''window "main"
    elem "main"
        type = MAIN
        size = 448x320
        is-default = true
        statusbar = false
    elem "map"
        type = MAP
        size = 448x320
        anchor1 = 0,0
        anchor2 = 100,100
        is-default = true
        zoom-mode = normal
''')
    if wide:
        with (dest / 'padding.dm').open('w') as f:
            for i in range(66000):
                f.write(f'/datum/padding_group_{i//1000}/proc/padding_{i}()\n    return\n')
        with (dest / 'world.dme').open('a') as f:
            f.write('#include "padding.dm"\n')
    for name, size, color in [('small', (45,21), (255,220,20,255)),
                               ('large', (75,53), (255,220,20,255)),
                               ('floor', (32,32), (20,70,90,255)),
                               ('badge', (32,32), (0,255,0,255))]:
        im = Image.new('RGBA', size, color)
        if name in ('small', 'large'):
            for y in range(7,12):
                for x in range(7,12): im.putpixel((x,y), (0,0,0,0))
        im.save(dest / (name+'.png'))


if __name__ == '__main__':
    prepare(Path(sys.argv[1]), '--wide' in sys.argv[2:])
