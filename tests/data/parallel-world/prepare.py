"""Generate the parallel/follower fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
# Same arm, clock markers and skin as transform-tween-world.
subprocess.run([sys.executable, str(source.parent / 'transform-tween-world' / 'prepare.py'), str(target)], check=True)
shutil.copy2(source / 'world.dm', target / 'world.dm')
im = Image.new('RGBA', (32, 32), (0, 0, 0, 0))
im.putpixel((0, 0), (60, 60, 60, 255))
im.save(target / 'dot.png')
print(target)
