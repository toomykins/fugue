"""Stage a handwritten world with a generated bounded case list and independent PNGs."""
import argparse
import shutil
import struct
import zlib
from pathlib import Path


def png(pixel):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + b''.join(bytes(pixel(x,y)) for x in range(32)) for y in range(32))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 32, 32, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('dest', type=Path)
    parser.add_argument('cases', type=Path)
    args = parser.parse_args()
    args.dest.mkdir(parents=True, exist_ok=True)
    for name in ['world.dme', 'world.dm', 'skin.dmf']:
        shutil.copy2(Path(__file__).with_name(name), args.dest / name)
    shutil.copy2(args.cases, args.dest / 'cases.json')
    (args.dest/'blue.png').write_bytes(png(lambda x,y: (0,0,255,255)))
    (args.dest/'marker.png').write_bytes(png(lambda x,y: (0,255,0,255) if 12 <= x < 20 and 12 <= y < 20 else (0,0,0,0)))
    (args.dest/'ack.txt').write_text('0')
