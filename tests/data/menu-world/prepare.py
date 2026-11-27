"""The runtime directory of the menu probe: prepare.py <dir> (requires Pillow).

Copies world.dm and writes world.dme, the shared 576x384 skin of tools/stockprobe/prepare.py, floor.png (dark grey)
and three icons made to be told apart in a grab:

  z.dmi, 32x32:  "a"     four quadrants: red top left, green top right, blue bottom left, yellow bottom right
                 "d"     four directions, the whole cell: south red, north green, east blue, west magenta
                 "anim"  three frames of 1 s: red, green, blue
                 "half"  orange on the left half only
                 "ov"    white 12x12 in the middle (12..23)
                 "un"    cyan, the whole cell
                 "white" white, the whole cell
                 ""      grey with a black diagonal (what a missing state falls back to)
  big.dmi, 64x64:  ""  quadrants of 32: red, green, blue, yellow as in "a"
  small.dmi, 16x16: "" purple
"""
import shutil
import sys
from pathlib import Path
from PIL import Image, PngImagePlugin

source = Path(__file__).resolve().parent
sys.path.insert(0, str(source.parent.parent.parent / 'tools' / 'stockprobe'))
from prepare import SKIN  # noqa: E402

RED, GREEN, BLUE, YELLOW = (220, 40, 40, 255), (40, 200, 60, 255), (40, 60, 220, 255), (240, 220, 30, 255)


def quadrants(size):
    h = size // 2
    return lambda x, y: (RED if y < h else BLUE) if x < h else (GREEN if y < h else YELLOW)


def solid(colour): return lambda x, y: colour


def dmi(path, size, states):
    """states: [(name, dirs, delays or None, [frame functions in DMI order: each frame's dirs together])]"""
    desc = f'# BEGIN DMI\nversion = 4.0\n\twidth = {size}\n\theight = {size}\n'
    cells = []
    for name, dirs, delays, frames in states:
        desc += f'state = "{name}"\n\tdirs = {dirs}\n\tframes = {len(frames) // dirs}\n'
        if delays: desc += '\tdelay = ' + ','.join(str(d) for d in delays) + '\n'
        cells += frames
    desc += '# END DMI\n'
    image = Image.new('RGBA', (size * len(cells), size), (0, 0, 0, 0))
    for i, f in enumerate(cells):
        for y in range(size):
            for x in range(size):
                c = f(x, y)
                if c: image.putpixel((i * size + x, y), c)
    info = PngImagePlugin.PngInfo(); info.add_text('Description', desc, zip=True)
    image.save(path, format='PNG', pnginfo=info)


def prepare(target):
    target = Path(target).resolve()
    if target == source or source in target.parents: raise SystemExit('Use a separate runtime directory')
    target.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / 'world.dm', target / 'world.dm')
    (target / 'world.dmf').write_bytes(SKIN.encode())
    (target / 'world.dme').write_bytes(b'#include "world.dmf"\n#include "world.dm"\n')
    Image.new('RGBA', (32, 32), (16, 16, 16, 255)).save(target / 'floor.png')
    dmi(target / 'z.dmi', 32, [
        ('a', 1, None, [quadrants(32)]),
        ('d', 4, None, [solid(RED), solid(GREEN), solid(BLUE), solid((200, 40, 200, 255))]),
        ('anim', 1, [10, 10, 10], [solid(RED), solid(GREEN), solid(BLUE)]),
        ('half', 1, None, [lambda x, y: (240, 140, 30, 255) if x < 16 else None]),
        ('ov', 1, None, [lambda x, y: (255, 255, 255, 255) if 12 <= x < 24 and 12 <= y < 24 else None]),
        ('un', 1, None, [solid((40, 210, 220, 255))]),
        ('white', 1, None, [solid((255, 255, 255, 255))]),
        ('', 1, None, [lambda x, y: (0, 0, 0, 255) if abs(x - y) <= 1 else (128, 128, 128, 255)]),
    ])
    dmi(target / 'big.dmi', 64, [('', 1, None, [quadrants(64)])])
    dmi(target / 'small.dmi', 16, [('', 1, None, [solid((150, 50, 200, 255))])])


if __name__ == '__main__':
    if len(sys.argv) != 2: raise SystemExit(__doc__)
    prepare(sys.argv[1])
