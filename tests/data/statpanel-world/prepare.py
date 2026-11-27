"""Write an isolated fixture runtime; no BYOND/game assets or third-party modules."""
from pathlib import Path
import shutil
import struct
import sys
import zlib


def png(path, overlay=False):
    def chunk(kind, payload):
        return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload))
    rows = bytearray()
    for y in range(32):
        rows.append(0)
        for x in range(32):
            visible = (18 <= x < 30 and 2 <= y < 14) if overlay else (5 <= x < 27 and 5 <= y < 27)
            rows.extend((240, 200, 40, 255) if visible and overlay else (40, 170, 230, 255) if visible else (0, 0, 0, 0))
    path.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 6, 0, 0, 0)) +
                     chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    source = Path(__file__).resolve().parent
    target = Path(sys.argv[1]).resolve()
    if source == target:
        raise SystemExit('Choose a separate build runtime directory')
    target.mkdir(parents=True, exist_ok=True)
    for name in ('world.dm', 'world.dmf'):
        shutil.copy2(source / name, target / name)
    (target / 'world.dme').write_text('#include "world.dmf"\n#include "world.dm"\n', encoding='utf-8')
    png(target / 'base.png')
    png(target / 'overlay.png', True)
    print(target)
