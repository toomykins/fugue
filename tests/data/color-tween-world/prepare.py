"""Generate the colour tween fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import subprocess
import sys
from PIL import Image
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
# Same clock markers and skin as transform-tween-world.
subprocess.run([sys.executable, str(source.parent / 'transform-tween-world' / 'prepare.py'), str(target)], check=True)
shutil.copy2(source / 'world.dm', target / 'world.dm')
Image.new('RGBA', (32, 32), (100, 100, 100, 255)).save(target / 'grey.png')
print(target)
