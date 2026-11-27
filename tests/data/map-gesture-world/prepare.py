"""Copy the manual callback oracle and generate tiny PNGs outside the source tree."""
import argparse
from pathlib import Path
import shutil
import struct
import zlib


def png(color):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    pixels = (b"\0" + bytes(color) * 32) * 32
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 32, 32, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("destination", type=Path)
    out = parser.parse_args().destination.resolve()
    here = Path(__file__).resolve().parent
    if out == here or here in out.parents:
        parser.error("Use a separate build directory, not the fixture source directory")
    out.mkdir(parents=True, exist_ok=True)
    for name in ("world.dm", "world.dmf"):
        shutil.copyfile(here / name, out / name)
    (out / "world.dme").write_text('#include "world.dmf"\n#include "world.dm"\n', encoding="utf-8")
    for name, color in {"floor": (51, 51, 51, 255), "source": (255, 0, 0, 255),
                        "destination": (0, 255, 0, 255), "mob": (0, 0, 255, 255)}.items():
        (out / (name + ".png")).write_bytes(png(color))
    print(out)


if __name__ == "__main__":
    main()
