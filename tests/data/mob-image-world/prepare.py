"""Generate supplemental mob-image probes in a disposable runtime (Pillow)."""
from pathlib import Path
import runpy
import shutil
import sys
source = Path(__file__).resolve().parent
runtime = Path(sys.argv[1]).resolve()
if runtime == source:
    raise SystemExit('Use a separate runtime directory')
runpy.run_path(str(source.parent / 'image-transform-world' / 'prepare.py'))
shutil.copy2(source / 'world.dm', runtime / 'world.dm')
