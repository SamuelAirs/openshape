# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Summarizes a screenshot as numbers, so CI can show whether the view drew
anything without anyone opening the image: size, the most common colors and
the share of pixels that differ from the background.

Usage: python3 scripts/ci/png_stats.py <file.png> [--github]
With --github the summary is printed as a GitHub notice annotation.
Reads 8-bit RGB/RGBA, non-interlaced PNGs (what Qt writes); standard library only.
"""

import collections
import struct
import sys
import zlib


def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, header = 8, b"", None
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            idat += chunk
        pos += 12 + length
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color not in (2, 6) or interlace:
        raise ValueError(f"unsupported PNG (depth {depth}, color type {color}, interlace {interlace})")
    bpp = 3 if color == 2 else 4
    raw = zlib.decompress(idat)
    stride = width * bpp
    rows, prev = [], bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        kind, line = raw[start], bytearray(raw[start + 1:start + 1 + stride])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if kind == 1:
                line[i] = (line[i] + a) & 255
            elif kind == 2:
                line[i] = (line[i] + b) & 255
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        rows.append(line)
        prev = line
    return width, height, bpp, rows


def summarize(path):
    width, height, bpp, rows = read_png(path)
    counts = collections.Counter()
    for line in rows:
        for i in range(0, len(line), bpp * 2):  # every second pixel is plenty
            counts[bytes(line[i:i + 3])] += 1
    total = sum(counts.values())
    top = counts.most_common(5)
    background = top[0][1] / total
    colors = ", ".join(f"#{c.hex()} {n / total:.1%}" for c, n in top)
    return (f"{width}x{height}; not background: {1 - background:.1%}; "
            f"distinct colors: {len(counts)}; most common: {colors}")


if __name__ == "__main__":
    summary = summarize(sys.argv[1])
    if "--github" in sys.argv:
        print(f"::notice title=Screenshot {sys.argv[1].split('/')[-1]}::{summary}")
    else:
        print(summary)
