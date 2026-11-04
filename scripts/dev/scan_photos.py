# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Scan (docs/SCAN.md) on real photos: JPEGs of an object standing on a sheet
of paper -> binary PPMs (EXIF orientation applied) and photos.txt with each
photo's 35 mm focal length, then tools/scan_photos on them.

    python scripts/dev/scan_photos.py <photo folder> <work folder> [--letter] [--max 1600]

Needs Pillow (the Windows Python has it). Corners are found by
measure::detectPaper; to fix one, add its eight numbers after the focal
length in <work folder>/photos.txt and run build/msys2-ucrt64/bin/scan_photos.exe
<work folder> again. The result is <work folder>/scan.stl (File > Import STL...).
"""
import os
import subprocess
import sys

from PIL import Image, ImageOps

FOCAL_35MM_TAG = 41989  # FocalLengthIn35mmFilm
EXIF_IFD = 0x8769


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if len(args) < 2:
        print(__doc__)
        return 2
    source, work = args[0], args[1]
    letter = '--letter' in sys.argv
    largest = 1600
    if '--max' in sys.argv:
        largest = int(sys.argv[sys.argv.index('--max') + 1])
    os.makedirs(work, exist_ok=True)
    lines = []
    for name in sorted(os.listdir(source)):
        if not name.lower().endswith(('.jpg', '.jpeg', '.png')):
            continue
        image = Image.open(os.path.join(source, name))
        focal = 0.0
        try:
            exif = image.getexif()
            value = exif.get_ifd(EXIF_IFD).get(FOCAL_35MM_TAG) or exif.get(FOCAL_35MM_TAG)
            focal = float(value or 0)
        except Exception:
            focal = 0.0
        image = ImageOps.exif_transpose(image).convert('RGB')
        image.thumbnail((largest, largest), Image.LANCZOS)
        ppm = os.path.splitext(name)[0] + '.ppm'
        image.save(os.path.join(work, ppm))
        lines.append('%s %g' % (ppm, focal))
        print('%s: %dx%d, focal %g mm (35 mm equivalent)' % (name, image.width, image.height, focal))
    with open(os.path.join(work, 'photos.txt'), 'w', encoding='utf-8', newline='\n') as out:
        out.write('\n'.join(lines) + '\n')
    here = os.path.dirname(os.path.abspath(__file__))
    tool = os.path.join(here, '..', '..', 'build', 'msys2-ucrt64', 'bin', 'scan_photos.exe')
    command = [tool, work] + (['--letter'] if letter else [])
    return subprocess.call(command)


if __name__ == '__main__':
    sys.exit(main())
