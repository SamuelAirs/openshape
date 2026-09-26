# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Builds resources/icons/openshape.ico from resources/icons/openshape.svg.

Each size is rendered by rsvg-convert (MSYS2: pacman -S
mingw-w64-ucrt-x86_64-librsvg) and packed into one Windows icon: 32-bit
BMP entries up to 128 px (every Windows version and tool reads them) and a
PNG-compressed 256 px entry (Windows Vista and later). Python standard
library only. The .ico is committed; run this again after changing the SVG:

    python scripts/windows/make-icon.py [svg] [ico]
"""

import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

SIZES = [16, 20, 24, 32, 40, 48, 64, 128, 256]
PNG_MIN_SIZE = 256  # entries this large are stored as PNG, smaller ones as BMP


def render_png(rsvg, svg, size, directory):
    out = os.path.join(directory, f"icon-{size}.png")
    subprocess.run([rsvg, "-w", str(size), "-h", str(size), "-f", "png", "-o", out, svg], check=True)
    with open(out, "rb") as f:
        return f.read()


def decode_rgba(png):
    """Decodes an 8-bit RGBA, non-interlaced PNG (what rsvg-convert writes)."""
    if png[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, header = 8, b"", None
    while pos < len(png):
        length, kind = struct.unpack(">I4s", png[pos:pos + 8])
        data = png[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", data)
        elif kind == b"IDAT":
            idat += data
        elif kind == b"IEND":
            break
    width, height, depth, color, _, _, interlace = header
    if depth != 8 or color != 6 or interlace != 0:
        raise ValueError(f"unsupported PNG layout (depth {depth}, color type {color}, interlace {interlace})")
    raw = zlib.decompress(idat)
    stride = width * 4
    rows, prev, pos = [], bytearray(stride), 0
    for _ in range(height):
        kind = raw[pos]
        line = bytearray(raw[pos + 1:pos + 1 + stride])
        pos += 1 + stride
        for i in range(stride):
            a = line[i - 4] if i >= 4 else 0
            b = prev[i]
            c = prev[i - 4] if i >= 4 else 0
            if kind == 1:
                line[i] = (line[i] + a) & 0xFF
            elif kind == 2:
                line[i] = (line[i] + b) & 0xFF
            elif kind == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif kind == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xFF
            elif kind != 0:
                raise ValueError(f"bad PNG filter {kind}")
        rows.append(bytes(line))
        prev = line
    return width, height, rows


def bmp_entry(width, height, rows):
    """A 32-bit icon bitmap: BITMAPINFOHEADER, BGRA rows bottom-up, AND mask."""
    header = struct.pack("<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = bytearray()
    for row in reversed(rows):
        for x in range(width):
            r, g, b, a = row[4 * x:4 * x + 4]
            pixels += bytes((b, g, r, a))
    mask_stride = ((width + 31) // 32) * 4
    mask = bytearray()
    for row in reversed(rows):
        line = bytearray(mask_stride)
        for x in range(width):
            if row[4 * x + 3] == 0:  # fully transparent: masked out
                line[x // 8] |= 0x80 >> (x % 8)
        mask += line
    return header + bytes(pixels) + bytes(mask)


def main():
    root = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    svg = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "resources", "icons", "openshape.svg")
    ico = sys.argv[2] if len(sys.argv) > 2 else os.path.join(root, "resources", "icons", "openshape.ico")
    rsvg = shutil.which("rsvg-convert")
    if not rsvg:
        sys.exit("rsvg-convert not found (pacman -S mingw-w64-ucrt-x86_64-librsvg; MSYS2's ucrt64/bin first in PATH)")

    images = []
    with tempfile.TemporaryDirectory() as directory:
        for size in SIZES:
            png = render_png(rsvg, svg, size, directory)
            width, height, rows = decode_rgba(png)
            if (width, height) != (size, size):
                raise ValueError(f"rendered {width}x{height}, wanted {size}x{size}")
            if not any(row[4 * x + 3] for row in rows for x in range(width)):
                raise ValueError(f"{size} px rendering is empty")
            images.append((size, png if size >= PNG_MIN_SIZE else bmp_entry(width, height, rows)))

    offset = 6 + 16 * len(images)
    directory_entries, data = b"", b""
    for size, blob in images:
        dim = 0 if size >= 256 else size  # 0 means 256 in an ICONDIRENTRY
        directory_entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    with open(ico, "wb") as f:
        f.write(struct.pack("<HHH", 0, 1, len(images)) + directory_entries + data)
    print(f"wrote {ico}: {len(images)} images ({', '.join(str(s) for s, _ in images)} px), {offset + len(data)} bytes")


if __name__ == "__main__":
    main()
