"""The runtime directory of the hover probe: prepare.py <dir> [CLIENT_ENTER] [CLIENT_MOVE] [EXTRA] (requires Pillow).

Copies world.dm, writes world.dme (with a #define for each switch named, or each named in HOVER_SWITCHES, comma
separated, which is how stockprobe.py record gets them), a 288x224 skin (a 7x7 view of 32 px tiles between 32 px of
letterbox left and right) and solid icons: one colour per probe atom over the whole tile, and big.png, 64x64. EXTRA
widens the window to 610x336 and adds, beside and below the map, a second map "map2" (96x32 at 300,0), the stat panel
"info" (300x184 at 300,40) and a grid "grid" (288x96 at 0,232). PANEL (with EXTRA) makes the stat panel 300x290 and
adds the macros F2, F3 and F4 (the verbs flip, swap and tick).
"""
import os
from pathlib import Path
import shutil
import sys
from PIL import Image

source = Path(__file__).resolve().parent
if len(sys.argv) < 2: raise SystemExit(__doc__)
t = Path(sys.argv[1]).resolve()
if t == source or source in t.parents: raise SystemExit('Use a separate runtime directory')
switches = sys.argv[2:] or [s for s in os.environ.get('HOVER_SWITCHES', '').split(',') if s]
if any(s not in ('CLIENT_ENTER', 'CLIENT_MOVE', 'EXTRA', 'PANEL') for s in switches): raise SystemExit(__doc__)
if 'PANEL' in switches and 'EXTRA' not in switches: raise SystemExit('PANEL needs EXTRA')
t.mkdir(parents=True, exist_ok=True)
shutil.copy2(source / 'world.dm', t / 'world.dm')
(t / 'world.dme').write_bytes(''.join(f'#define {s}\n' for s in switches).encode() +
                              b'#include "world.dmf"\n#include "world.dm"\n')
extra = 'EXTRA' in switches
skin = ['window "main"', '    elem "main"', '        type = MAIN', '        size = ' + ('610x336' if extra else '288x224'),
        '        is-default = true', '    elem "map"', '        type = MAP', '        pos = 0,0',
        '        size = 288x224', '        anchor1 = 0,0', '        anchor2 = ' + ('none' if extra else '100,100'),
        '        icon-size = 32', '        is-default = true']
panel = 'PANEL' in switches
if panel: skin.insert(4, '        macro = "macro"')
if extra:
    skin += ['    elem "map2"', '        type = MAP', '        pos = 300,0', '        size = 96x32', '        icon-size = 32',
             '    elem "info"', '        type = INFO', '        pos = 300,40', '        size = ' + ('300x290' if panel else '300x184'),
             '        is-default = true', '    elem "grid"', '        type = GRID', '        pos = 0,232',
             '        size = 288x96', '        cells = 1x2', '        show-names = true']
if panel:
    skin = ['macro "macro"'] + [line for key, verb in (('F2', 'flip'), ('F3', 'swap'), ('F4', 'tick'))
                                for line in ('    elem', f'        name = "{key}"', f'        command = "{verb}"')] + skin
skin = '\n'.join(skin + [''])
(t / 'world.dmf').write_bytes(skin.encode())
for name, fill in {'floor': (16, 16, 16), 'red': (200, 60, 60), 'orange': (230, 140, 40), 'green': (60, 190, 60),
                   'yellow': (230, 220, 50), 'grey': (128, 128, 128), 'magenta': (200, 60, 200), 'blue': (50, 80, 230),
                   'cyan': (50, 200, 210), 'white': (235, 235, 235)}.items():
    Image.new('RGBA', (32, 32), fill + (255,)).save(t / f'{name}.png')
Image.new('RGBA', (64, 64), (90, 110, 150, 255)).save(t / 'big.png')
