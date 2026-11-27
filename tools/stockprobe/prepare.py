#!/usr/bin/env python3
"""The runtime directory of a probe world that needs nothing of its own: prepare.py <world> <dir> (requires Pillow).

Copies the world's .dm files (and its world.dmf, if it has one) and writes what they may rely on: world.dme including
them all, a skin whose main window is one 576x384 map with 32 px icons, and solid 32x32 icons:

  floor.png  dark grey, the whole tile         tile.png   green, the whole tile
  box.png    red, 16x16 at 8..23               blue.png   blue, 16x16 at 8..23
  small.png  yellow, 8x8 at 12..19

stockprobe.py uses this for a world directory with no prepare.py of its own. A world that needs other icons, a
generated source file or another skin keeps its own prepare.py <dir>.
"""
import shutil
import sys
from pathlib import Path
from PIL import Image, ImageDraw

SKIN = '\n'.join(['window "main"', '    elem "main"', '        type = MAIN', '        size = 576x384',
                  '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
                  '        size = 576x384', '        anchor1 = 0,0', '        anchor2 = 100,100', '        icon-size = 32',
                  '        is-default = true', ''])


def box(fill, lo, hi):
    image = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
    ImageDraw.Draw(image).rectangle((lo, lo, hi, hi), fill=fill)
    return image


def prepare(world, target):
    world, target = Path(world).resolve(), Path(target).resolve()
    if target == world: raise SystemExit('Use a separate runtime directory')
    target.mkdir(parents=True, exist_ok=True)
    sources = sorted(p.name for p in world.glob('*.dm'))
    for name in sources: shutil.copy2(world / name, target / name)
    if (world / 'world.dmf').exists(): shutil.copy2(world / 'world.dmf', target / 'world.dmf')
    else: (target / 'world.dmf').write_bytes(SKIN.encode())
    (target / 'world.dme').write_bytes(''.join(f'#include "{name}"\n' for name in ['world.dmf'] + sources).encode())
    box((16, 16, 16, 255), 0, 31).save(target / 'floor.png')
    box((40, 140, 60, 255), 0, 31).save(target / 'tile.png')
    box((200, 60, 60, 255), 8, 23).save(target / 'box.png')
    box((40, 80, 230, 255), 8, 23).save(target / 'blue.png')
    box((240, 220, 40, 255), 12, 19).save(target / 'small.png')


if __name__ == '__main__':
    if len(sys.argv) != 3: raise SystemExit(__doc__)
    prepare(sys.argv[1], sys.argv[2])
