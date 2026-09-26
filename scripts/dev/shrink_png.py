# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Makes OpenShape's screenshots smaller for the documentation, losslessly.

    python scripts/dev/shrink_png.py in.png out.png [--colors 256]

Qt writes screenshots as 8-bit RGBA PNGs at zlib's default level. This
drops an alpha channel that is opaque everywhere, chooses a filter per row
(the smallest sum of absolute differences, as libpng does), and compresses
at level 9. With --colors N the image becomes a palette image when it has at
most N distinct colors (it is left as is otherwise: no lossy quantization).

Standard library only (no Pillow), so it runs with any Python 3, including
MSYS2's. Reads 8-bit, non-interlaced RGB/RGBA PNGs (what Qt writes).
"""

import argparse
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"


def read_chunks(data):
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")
    pos = len(PNG_SIGNATURE)
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        yield kind, data[pos + 8:pos + 8 + length]
        pos += 12 + length


def paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def unfilter(raw, width, height, bpp):
    stride = width * bpp
    rows = []
    prev = bytearray(stride)
    pos = 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        if kind == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif kind == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 0xFF
        elif kind == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif kind == 4:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                up_left = prev[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + paeth(left, prev[i], up_left)) & 0xFF
        elif kind != 0:
            raise ValueError("unknown PNG filter %d" % kind)
        rows.append(line)
        prev = line
    return rows


def filter_rows(rows, bpp):
    """Each row with the filter whose output has the smallest sum of
    absolute (signed) values: libpng's heuristic."""
    out = bytearray()
    prev = bytearray(len(rows[0]))
    for line in rows:
        n = len(line)
        candidates = [bytes(line)]
        candidates.append(bytes((line[i] - (line[i - bpp] if i >= bpp else 0)) & 0xFF for i in range(n)))
        candidates.append(bytes((line[i] - prev[i]) & 0xFF for i in range(n)))
        candidates.append(bytes((line[i] - (((line[i - bpp] if i >= bpp else 0) + prev[i]) >> 1)) & 0xFF
                                for i in range(n)))
        candidates.append(bytes((line[i] - paeth(line[i - bpp] if i >= bpp else 0, prev[i],
                                                 prev[i - bpp] if i >= bpp else 0)) & 0xFF for i in range(n)))
        best = min(range(5), key=lambda k: sum(b if b < 128 else 256 - b for b in candidates[k]))
        out.append(best)
        out += candidates[best]
        prev = line
    return bytes(out)


def chunk(kind, payload):
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)


def shrink(data, max_colors=0):
    header = None
    idat = bytearray()
    for kind, payload in read_chunks(data):
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            idat += payload
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace != 0:
        raise ValueError("only 8-bit non-interlaced RGB/RGBA PNGs are supported")
    bpp = 4 if color == 6 else 3
    rows = unfilter(zlib.decompress(bytes(idat)), width, height, bpp)

    if bpp == 4 and all(all(v == 255 for v in line[3::4]) for line in rows):
        rows = [bytearray(b for i, b in enumerate(line) if i % 4 != 3) for line in rows]
        bpp, color = 3, 2

    palette_chunks = b""
    if max_colors and bpp == 3:
        colors = {}
        for line in rows:
            for i in range(0, len(line), 3):
                colors.setdefault(bytes(line[i:i + 3]), len(colors))
                if len(colors) > max_colors:
                    break
            if len(colors) > max_colors:
                break
        if len(colors) <= max_colors:
            rows = [bytearray(colors[bytes(line[i:i + 3])] for i in range(0, len(line), 3)) for line in rows]
            palette_chunks = chunk(b"PLTE", b"".join(colors))
            bpp, color = 1, 3

    raw = filter_rows(rows, bpp) if bpp > 1 else b"".join(b"\x00" + bytes(line) for line in rows)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, color, 0, 0, 0)
    return (PNG_SIGNATURE + chunk(b"IHDR", ihdr) + palette_chunks
            + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input")
    parser.add_argument("output")
    parser.add_argument("--colors", type=int, default=0,
                        help="make a palette image when there are at most this many colors (max 256)")
    args = parser.parse_args()
    with open(args.input, "rb") as f:
        data = f.read()
    smaller = shrink(data, min(args.colors, 256))
    with open(args.output, "wb") as f:
        f.write(smaller)
    print("%s: %d -> %d bytes (%.0f%%)" % (args.output, len(data), len(smaller), 100.0 * len(smaller) / len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
