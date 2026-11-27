"""Stage independent generated assets and fixture sources below build/."""
from pathlib import Path
import shutil
import sys
from PIL import Image
src = Path(__file__).resolve().parent
dst = Path(sys.argv[1]).resolve()
if dst.exists() or 'build' not in dst.parts:
    raise SystemExit('Use a new build/ directory')
dst.mkdir(parents=True)
for name in ('world.dm', 'world.dme', 'skin.dmf'):
    shutil.copy2(src/name, dst/name)
for name, color in [('floor',(24,32,48,255)), ('blue',(40,80,200,255)),
                    ('red',(200,40,20,255)), ('green',(20,200,60,255))]:
    Image.new('RGBA',(32,32),color).save(dst/(name+'.png'))
print(dst)
