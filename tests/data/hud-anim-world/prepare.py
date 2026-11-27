"""Generate the HUD animation fixture in a separate runtime directory (requires Pillow)."""
from pathlib import Path
import shutil
import subprocess
import sys
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
# Same arm, clock markers and skin as transform-tween-world.
subprocess.run([sys.executable, str(source.parent / 'transform-tween-world' / 'prepare.py'), str(target)], check=True)
shutil.copy2(source / 'world.dm', target / 'world.dm')
print(target)
