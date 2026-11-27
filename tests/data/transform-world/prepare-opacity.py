"""Generate a small RGBA/DMI oracle fixture in a disposable BYOND runtime."""
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
shutil.copy2(source / 'opacity-world.dm', target / 'world.dm')
shutil.copy2(source / 'world.dmf', target / 'world.dmf')
(target / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n')

def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))

# Default, four static directions, and two four-direction animation frames.
rows = bytearray()
for y in range(32):
    rows.append(0)
    for cell in range(13):
        for x in range(32):
            alpha = (0, 1, 63, 64, 127, 128, 254, 255)[x // 4]
            color = (255, 255, 255, alpha) if 4 <= y < 20 else (0, 0, 0, 0)
            if 22 <= y < 28 and 4 <= x < 12 + (cell % 4)*4:
                color = ((240,40,40,255) if cell < 9 else (40,190,240,255))
            rows.extend(color)
metadata = ('# BEGIN DMI\nversion = 4.0\nwidth = 32\nheight = 32\n'
            'state = ""\ndirs = 1\nframes = 1\n'
            'state = "mark"\ndirs = 4\nframes = 1\n'
            'state = "pulse"\ndirs = 4\nframes = 2\ndelay = 5,5\n# END DMI\n')
(target / 'source.dmi').write_bytes(b'\x89PNG\r\n\x1a\n'
    + chunk(b'IHDR', struct.pack('>IIBBBBB',32*13,32,8,6,0,0,0))
    + chunk(b'tEXt', b'Description\0' + metadata.encode())
    + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))
print(target)
