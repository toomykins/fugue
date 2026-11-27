"""Stage the nested CHILD fixture without writing binaries into the source tree.

Usage: prepare.py OUTPUT [ROW_DESIGN_HEIGHT] [ROW_MAIN_PROPERTY]
"""
from pathlib import Path
import shutil
import sys

source = Path(__file__).parent
out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
height = int(sys.argv[2]) if len(sys.argv) > 2 else 30
extra = sys.argv[3] if len(sys.argv) > 3 else ''
skin = (source / 'skin.dmf').read_text().replace('size = 676x30', f'size = 676x{height}')
skin = skin.replace('size = 676x' + str(height) + '\n        is-pane = true',
                    'size = 676x' + str(height) + '\n        is-pane = true\n        ' + extra)
(out / 'skin.dmf').write_text(skin)
(out / 'world.dme').write_text('#include "skin.dmf"\n#include "world.dm"\n')
shutil.copyfile(source / 'world.dm', out / 'world.dm')
