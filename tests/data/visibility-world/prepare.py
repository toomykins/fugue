"""Generate disposable synthetic visibility assets; no game checkout required."""
from pathlib import Path
import shutil
import struct
import sys
import zlib

source = Path(__file__).resolve().parent
target = Path(sys.argv[1]).resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
target.mkdir(parents=True, exist_ok=True)
for name in ('world.dm', 'world.dmf'):
    shutil.copy2(source / name, target / name)
(target / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')
if not (target / 'phase.txt').exists():
    (target / 'phase.txt').write_text('x=5&view=4&shape=wall')
def png(name, color, size=32, inset=0):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(size):
        rows.append(0)
        for x in range(size):
            rows.extend(color if inset <= x < size-inset and inset <= y < size-inset else (0,0,0,0))
    (target / name).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',size,size,8,6,0,0,0)) + chunk(b'IDAT',zlib.compress(rows)) + chunk(b'IEND',b''))
png('floor.png', (60,80,100,255), inset=1)
png('wall.png', (200,200,200,255), inset=1)
png('object.png', (240,50,40,255), inset=8)
png('mob.png', (40,230,70,255), inset=8)
png('hud.png', (230,210,30,255), inset=4)
png('large.png', (210,50,210,255), size=96, inset=2)
print(target)
