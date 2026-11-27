"""Stage the independent master-material fixture in a new build/ directory."""
from pathlib import Path
import shutil
import subprocess
import sys
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target.exists() or 'build' not in target.parts:
    raise SystemExit('Use a new build/ runtime')
subprocess.run([sys.executable, '-B', str(source.parent/'local-relay-world'/'prepare.py'), str(target)], check=True)
shutil.copy2(source/'world.dm', target/'world.dm')
(target/'phase.txt').write_text('control')
