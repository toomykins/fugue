"""Generate asymmetric assets in a separate, disposable BYOND runtime."""
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
def png(name, overlay=False):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(32):
        rows.append(0)
        for x in range(32):
            color = (0,0,0,0)
            if overlay:
                if 23 <= x < 29 and 3 <= y < 9: color = (255,220,0,255)
            else:
                if 4 <= x < 10 and 4 <= y < 28: color = (240,40,40,255)
                if 10 <= x < 25 and 21 <= y < 28: color = (40,190,240,255)
                if 13 <= x < 18 and 7 <= y < 12: color = (40,230,70,255)
            rows.extend(color)
    (target / name).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB',32,32,8,6,0,0,0)) + chunk(b'IDAT',zlib.compress(rows)) + chunk(b'IEND',b''))
png('base.png')
png('overlay.png', True)
print(target)
