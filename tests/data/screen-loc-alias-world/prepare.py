"""Stage independent stock-client alias probes; no game assets needed."""
import shutil
import struct
import sys
import zlib
from pathlib import Path


def png(color):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = (b'\0' + bytes(color) * 32) * 32
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    dest = Path(sys.argv[1])
    dest.mkdir(parents=True, exist_ok=True)
    for name in ['world.dme', 'world.dm', 'skin.dmf']:
        shutil.copy2(Path(__file__).with_name(name), dest / name)
    pairs = sys.argv[2] if len(sys.argv) > 2 else 'pairs.json'
    shutil.copy2(Path(__file__).with_name(pairs), dest / 'pairs.json')
    for name, color in [('green', (0, 255, 0, 255)), ('black', (0, 0, 0, 255))]:
        (dest / (name + '.png')).write_bytes(png(color))
