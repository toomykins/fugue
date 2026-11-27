"""Stage a small/large-procedure-table fixture; no generated padding in source control."""
from pathlib import Path
import shutil
import sys
out = Path(sys.argv[1])
count = int(sys.argv[2]) if len(sys.argv) > 2 else 0
out.mkdir(parents=True, exist_ok=True)
source = Path(__file__).parent
for name in ['skin.dmf', 'world.dm']:
    shutil.copyfile(source / name, out / name)
with (out / 'padding.dm').open('w') as f:
    for i in range(count):
        f.write(f'/datum/padding_group_{i // 1000}/proc/padding_{i}()\n    return\n')
order = ['world.dm', 'padding.dm'] if len(sys.argv) > 3 and sys.argv[3] == 'after' else ['padding.dm', 'world.dm']
(out / 'world.dme').write_text('#include "skin.dmf"\n' + ''.join(f'#include "{name}"\n' for name in order))
