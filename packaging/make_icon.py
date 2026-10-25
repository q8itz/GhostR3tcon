#!/usr/bin/env python3
"""Draw the launcher icon: a reticle on a dark field. No game artwork is used."""
import math
import struct
import sys
import zlib

SIZE = 256
BACKGROUND = (0x18, 0x1E, 0x16)
EDGE = (0x3C, 0x4A, 0x30)
RETICLE = (0xE8, 0xB4, 0x2C)


def pixel(x, y):
    cx = cy = (SIZE - 1) / 2
    dx, dy = x - cx, y - cy
    r = math.hypot(dx, dy)
    corner = 40
    # Rounded square.
    qx = max(abs(dx) - (cx - corner), 0)
    qy = max(abs(dy) - (cy - corner), 0)
    if math.hypot(qx, qy) > corner:
        return (0, 0, 0, 0)
    if math.hypot(qx, qy) > corner - 6:
        return EDGE + (255,)
    # Ring with gaps on the axes, plus ticks pointing at the centre.
    on_axis = min(abs(dx), abs(dy)) < 7
    if 74 <= r <= 84 and not on_axis:
        return RETICLE + (255,)
    if on_axis and 34 <= r <= 64 and min(abs(dx), abs(dy)) < 4:
        return RETICLE + (255,)
    if r <= 6:
        return RETICLE + (255,)
    return BACKGROUND + (255,)


def main():
    rows = b"".join(
        b"\0" + b"".join(bytes(pixel(x, y)) for x in range(SIZE)) for y in range(SIZE))

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = (b"\x89PNG\r\n\x1a\n" +
           chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0)) +
           chunk(b"IDAT", zlib.compress(rows, 9)) + chunk(b"IEND", b""))
    with open(sys.argv[1], "wb") as f:
        f.write(png)


if __name__ == "__main__":
    main()
