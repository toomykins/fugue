"""Stage the narrow master fixture, reusing the relay corpus's generated controls."""
from pathlib import Path
import shutil
import subprocess
import sys
source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
subprocess.run([sys.executable, '-B', str(source.parent/'local-relay-world'/'prepare.py'), str(target)], check=True)
focused_input = '--input' in sys.argv[2:]
shutil.copy2(source/('input-world.dm' if focused_input else 'world.dm'), target/'world.dm')
(target/'phase.txt').write_text('plane-rotate' if focused_input else 'overlay')
