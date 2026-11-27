# The splash for the splash-gif world: a 480x480 animated GIF of 80 full-screen frames, frame i a solid colour
# (8 + 3i, 240 - 3i, 96), 30 cs each, looping forever. Its frames composite to 480*480*4*80 bytes (70.3 MiB) and decode
# 18.4 million frame pixels: more than Fugue's old GIF limits (64 MiB, 16 million) and less than a 64-million-pixel
# icon sheet. Written by hand, so every frame is kept whole.
import struct
import sys

W = H = 480
FRAMES = 80


def colour(i):
    return (8 + 3 * i, 240 - 3 * i, 96)


def lzw(pixels, lit=2):
    """GIF LZW (LSB-first, no early change) of palette indices. The decoder defines an entry from each code after the
    first, so the code width is that of the decoder's next free entry."""
    clear, eoi = 1 << lit, (1 << lit) + 1
    table = {(i,): i for i in range(clear)}
    codes, current = [clear], ()
    for p in pixels:
        if current + (p,) in table:
            current += (p,)
            continue
        codes.append(table[current])
        if len(table) + 2 < 4096:
            table[current + (p,)] = len(table) + 2
        current = (p,)
    codes.append(table[current])
    codes.append(eoi)
    out, acc, nb = bytearray(), 0, 0
    for j, c in enumerate(codes):
        # codes[0] is the clear; codes[j] for j >= 1 is the j-th code after it.
        width = 1 + lit if j == 0 else min(12, (eoi + 1 + max(0, j - 2)).bit_length())
        acc |= c << nb
        nb += width
        while nb >= 8:
            out.append(acc & 255)
            acc >>= 8
            nb -= 8
    if nb:
        out.append(acc & 255)
    return bytes(out)


def blocks(data):
    out = bytearray()
    for i in range(0, len(data), 255):
        chunk = data[i:i + 255]
        out += bytes([len(chunk)]) + chunk
    return bytes(out + b'\0')


def main(path):
    out = bytearray(b'GIF89a' + struct.pack('<HHBBB', W, H, 0, 0, 0))
    out += b'\x21\xff\x0bNETSCAPE2.0\x03\x01\x00\x00\x00'
    image = blocks(lzw([0] * (W * H)))
    for i in range(FRAMES):
        out += b'\x21\xf9\x04\x00' + struct.pack('<H', 30) + b'\x00\x00'
        # A local table of two colours: index 0 is the frame's colour.
        out += b'\x2c' + struct.pack('<HHHHB', 0, 0, W, H, 0x80) + bytes(colour(i)) + b'\0\0\0'
        out += b'\x02' + image
    out += b'\x3b'
    with open(path, 'wb') as f:
        f.write(out)


if __name__ == '__main__':
    main(sys.argv[1])
