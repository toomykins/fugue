"""Stage synthetic map-isolation fixture; generated assets are not retained."""
import shutil
import struct
import sys
import zlib
from pathlib import Path


def png(color, stress=False):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    size = 1184 if stress else 32
    row = bytes(color) * size
    pixels = (b"\0" + row) * size
    text = b""
    if stress:
        # 341 states / 1366 cells, deliberately mostly unused by the two selections.
        description = '# BEGIN DMI\nversion = 4.0\nwidth = 32\nheight = 32\n'
        for i in range(341):
            description += f'state = "s{i}"\ndirs = {4 if i < 339 else 1}\nframes = {3 if i == 338 else 1}\n'
        description += '# END DMI\n'
        text = chunk(b"zTXt", b"Description\0\0" + zlib.compress(description.encode()))
        top = row[:128*4] + bytes((0,255,0,255))*128 + bytes((0,0,255,255))*128 + row[384*4:]
        pixels = (b"\0" + top)*32 + (b"\0" + row)*(size-32)
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0))
            + text + chunk(b"IDAT", zlib.compress(pixels))
            + chunk(b"IEND", b""))


if __name__ == "__main__":
    dest = Path(sys.argv[1])
    dest.mkdir(parents=True, exist_ok=True)
    for name in ("world.dme", "world.dm", "skin.dmf"):
        shutil.copy2(Path(__file__).with_name(name), dest / name)
    (dest / "stress.dmi").write_bytes(png((255,0,0,255), stress=True))
    for name, color in (("red", (255, 0, 0, 255)), ("green", (0, 255, 0, 255)), ("blue", (0, 0, 255, 255)), ("black", (0, 0, 0, 255))):
        (dest / (name + ".png")).write_bytes(png(color))
