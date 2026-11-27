"""Generate an isolated built-in lighting fixture; no external assets required."""
from pathlib import Path
import argparse
import shutil
import struct
import zlib

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('target', type=Path)
parser.add_argument('--size', type=int, choices=(9, 17), default=9)
args = parser.parse_args()
source = Path(__file__).resolve().parent
target = args.target.resolve()
if target == source:
    raise SystemExit('Use a separate runtime directory')
target.mkdir(parents=True, exist_ok=True)
for name in ('world.dm', 'world.dmf'):
    shutil.copy2(source / name, target / name)
(target / 'world.dme').write_text(f'#define DARKNESS_WORLD_SIZE {args.size}\n#include "world.dmf"\n#include "world.dm"\n')
if not (target / 'phase.txt').exists():
    (target / 'phase.txt').write_text('ambient=1&see=0&sight=normal')


def png(name, color, inset, width=32):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = bytearray()
    for y in range(32):
        rows.append(0)
        for x in range(width):
            rows.extend(color if inset <= x < width-inset and inset <= y < 32-inset else (0, 0, 0, 0))
    (target / name).write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, 32, 8, 6, 0, 0, 0))
                               + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


png('floor.png', (60, 80, 100, 255), 1)
png('wall.png', (200, 200, 200, 255), 1)
png('target.png', (240, 50, 40, 255), 8)
png('lamp.png', (50, 200, 240, 255), 8)
png('observer.png', (40, 230, 70, 255), 8)
png('hud.png', (230, 210, 30, 255), 4)
png('wide.png', (240, 50, 40, 255), 8, width=64)
png('backdrop.png', (80, 40, 120, 255), 0)
print(target)
