# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Checks App Store screenshots before they are uploaded.

    python scripts/dev/check_appstore_screenshots.py [folder]

(default: docs/appstore/screenshots). Every PNG named iphone-*.png or
ipad-*.png must be

- exactly a size App Store Connect accepts for the largest displays
  (checked 2026-09-27 against
  https://developer.apple.com/help/app-store-connect/reference/app-information/screenshot-specifications):
  iPhone 6.9" 1260 x 2736, 1290 x 2796 or 1320 x 2868; iPad 13"
  2064 x 2752 or 2048 x 2732; portrait or landscape;
- without an alpha channel or transparency (Apple refuses those): 8-bit
  RGB, no tRNS chunk;
- whole (its image data inflates to exactly the rows it declares) and not
  blank (an empty window compresses to almost nothing);

and each device needs 1 to 10 of them. Prints one line per file and exits
with the number of problems. Standard library only.
"""

import os
import struct
import sys
import zlib

PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"

SIZES = {
    "iphone": {(1260, 2736), (1290, 2796), (1320, 2868)},
    "ipad": {(2064, 2752), (2048, 2732)},
}
MAX_PER_DEVICE = 10


def chunks(data):
    if not data.startswith(PNG_SIGNATURE):
        raise ValueError("not a PNG file")
    pos = len(PNG_SIGNATURE)
    while pos + 8 <= len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        yield kind, data[pos + 8:pos + 8 + length]
        pos += 12 + length


def check(path, device):
    """The problems with one screenshot (empty list: fine) and a summary."""
    with open(path, "rb") as f:
        data = f.read()
    problems = []
    header = None
    idat = bytearray()
    transparency = False
    for kind, body in chunks(data):
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", body)
        elif kind == b"IDAT":
            idat += body
        elif kind == b"tRNS":
            transparency = True
    if header is None:
        return ["no IHDR chunk"], "?"
    width, height, depth, color, _, _, interlace = header
    summary = "%d x %d" % (width, height)
    allowed = SIZES[device] | {(h, w) for (w, h) in SIZES[device]}
    if (width, height) not in allowed:
        problems.append("%s is not an accepted %s size (%s)" % (
            summary, device, ", ".join("%d x %d" % s for s in sorted(SIZES[device]))))
    if color != 2 or depth != 8:
        problems.append("color type %d, %d bits: expected 8-bit RGB without alpha (type 2)" % (color, depth))
    if transparency:
        problems.append("has a tRNS (transparency) chunk")
    if interlace != 0:
        problems.append("interlaced")
    try:
        raw = zlib.decompress(bytes(idat))
    except zlib.error as e:
        return problems + ["image data does not inflate: %s" % e], summary
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color, 3)
    expected = height * (1 + width * channels * depth // 8)
    if len(raw) != expected:
        problems.append("image data is %d bytes, %d expected (cut short?)" % (len(raw), expected))
    if len(idat) < len(raw) * 0.005:
        problems.append("looks blank (compresses to %.2f%%)" % (100.0 * len(idat) / max(1, len(raw))))
    return problems, summary


def main():
    folder = sys.argv[1] if len(sys.argv) > 1 else "docs/appstore/screenshots"
    names = sorted(n for n in os.listdir(folder) if n.lower().endswith(".png"))
    failures = 0
    counts = {device: 0 for device in SIZES}
    for name in names:
        device = next((d for d in SIZES if name.startswith(d + "-")), None)
        if device is None:
            print("[FAIL] %s: name it iphone-... or ipad-..." % name)
            failures += 1
            continue
        counts[device] += 1
        problems, summary = check(os.path.join(folder, name), device)
        if problems:
            failures += len(problems)
            for problem in problems:
                print("[FAIL] %s: %s" % (name, problem))
        else:
            print("[PASS] %s: %s, RGB, no alpha" % (name, summary))
    for device, count in counts.items():
        if not 1 <= count <= MAX_PER_DEVICE:
            print("[FAIL] %d %s screenshots: App Store Connect takes 1 to %d" % (count, device, MAX_PER_DEVICE))
            failures += 1
    print("%d problem(s) in %d file(s)" % (failures, len(names)))
    return failures


if __name__ == "__main__":
    sys.exit(main())
