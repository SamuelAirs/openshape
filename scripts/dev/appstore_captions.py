# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The captioned App Store screenshots for 0.3.1 (docs/MARKETING_RENDERS.md,
piece 3): each of the twelve screenshots in docs/appstore/screenshots/
(made by appstore_screenshots.sh) under a short caption, on a canvas of the
same size App Store Connect takes for it (iPhone 6.9" 1320 x 2868, iPad 13"
2752 x 2064), in the order the listing should show them (only the first
three show in search results). RGB, no alpha.

    python scripts/dev/appstore_captions.py [source folder] [output folder]

Defaults: docs/appstore/screenshots -> docs/appstore/captioned. Then checks
the output with check_appstore_screenshots.py. Needs Pillow; the captions
are in Noto Sans (resources/fonts, SIL OFL). Run it again on the owner's
device screenshots (same names) to caption those instead.

The captions say what each picture shows, no prices (guideline 2.3.7), and
the picture of several projects says they come with the in-app purchase
(guideline 2.3.2).
"""

import os
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
FONT_BOLD = os.path.join(ROOT, "resources", "fonts", "NotoSans-Bold.ttf")
FONT = os.path.join(ROOT, "resources", "fonts", "NotoSans-Regular.ttf")

BACKGROUND = (0xEC, 0xEE, 0xF1)
TEXT = (0x1F, 0x23, 0x29)
MUTED = (0x6A, 0x71, 0x7C)
ACCENT = (0x29, 0x77, 0xED)
BEZEL = (0x1F, 0x23, 0x29)

# (order, source scene, caption, line under it)
CAPTIONS = [
    (1, "1-project-box", "Type the size you measured", "Holes that fit M3 screws, with a print allowance"),
    (2, "3-sketch", "Sketch with your finger", "Constraints, and sizes you type"),
    (3, "2-key-tag", "Raised text for labels", "Engraved too: push it in"),
    (4, "5-model-panel", "Change any step later", "Every size stays editable"),
    (5, "4-construction-plane", "Planes and axes where you need them", "Sketch, mirror and turn around them"),
    (6, "6-home", "All your projects, with previews", "Unlimited projects with the in-app purchase"),
]

SIZES = {"iphone": (1320, 2868), "ipad": (2752, 2064)}


def rounded_mask(size, radius):
    mask = Image.new("L", size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, size[0] - 1, size[1] - 1), radius, fill=255)
    return mask


def fit_font(path, text, max_width, start, draw):
    size = start
    while size > 20:
        font = ImageFont.truetype(path, size)
        if draw.textlength(text, font=font) <= max_width:
            return font
        size -= 2
    return ImageFont.truetype(path, size)


def caption(device, shot, title, line):
    W, H = SIZES[device]
    canvas = Image.new("RGB", (W, H), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    # The words: centered at the top.
    if device == "iphone":
        title_size, line_size, top, margin = 96, 50, 150, 90
        room_top = 500
    else:
        title_size, line_size, top, margin = 104, 52, 70, 200
        room_top = 400
    t_font = fit_font(FONT_BOLD, title, W - 2 * margin, title_size, draw)
    l_font = fit_font(FONT, line, W - 2 * margin, line_size, draw)
    # Placed by the glyphs' real extent (descenders included), with gaps.
    tx0, ty0, tx1, ty1 = draw.textbbox((0, 0), title, font=t_font)
    draw.text(((W - (tx1 - tx0)) / 2 - tx0, top - ty0), title, font=t_font, fill=TEXT)
    line_top = top + (ty1 - ty0) + int(t_font.size * 0.35)
    lx0, ly0, lx1, ly1 = draw.textbbox((0, 0), line, font=l_font)
    draw.text(((W - (lx1 - lx0)) / 2 - lx0, line_top - ly0), line, font=l_font, fill=MUTED)
    # A short accent bar under the words.
    bar_y = line_top + (ly1 - ly0) + int(l_font.size * 0.7)
    draw.rounded_rectangle((W / 2 - 60, bar_y, W / 2 + 60, bar_y + 10), 5, fill=ACCENT)

    # The screenshot in a plain dark frame, below the words, whole.
    bottom_margin = 110 if device == "iphone" else 90
    avail_w = W - 2 * (110 if device == "iphone" else 170)
    avail_h = H - room_top - bottom_margin
    bezel = 22 if device == "iphone" else 26
    scale = min((avail_w - 2 * bezel) / shot.size[0], (avail_h - 2 * bezel) / shot.size[1])
    sw, sh = int(shot.size[0] * scale), int(shot.size[1] * scale)
    screen = shot.resize((sw, sh), Image.LANCZOS)
    radius = 60 if device == "iphone" else 36
    frame = Image.new("RGB", (sw + 2 * bezel, sh + 2 * bezel), BEZEL)
    frame.paste(screen, (bezel, bezel), rounded_mask((sw, sh), radius - bezel // 2))
    x = (W - frame.size[0]) // 2
    y = room_top + (avail_h - frame.size[1]) // 2
    # A soft shadow.
    shadow = Image.new("L", (frame.size[0] + 120, frame.size[1] + 120), 0)
    ImageDraw.Draw(shadow).rounded_rectangle((60, 72, 60 + frame.size[0], 72 + frame.size[1]), radius, fill=90)
    shadow = shadow.filter(ImageFilter.GaussianBlur(28))
    dark = Image.new("RGB", shadow.size, (20, 30, 50))
    canvas.paste(dark, (x - 60, y - 60), shadow)
    canvas.paste(frame, (x, y), rounded_mask(frame.size, radius))
    return canvas


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "docs", "appstore", "screenshots")
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "docs", "appstore", "captioned")
    os.makedirs(out, exist_ok=True)
    for device in SIZES:
        for order, scene, title, line in CAPTIONS:
            shot = Image.open(os.path.join(src, f"{device}-{scene}.png")).convert("RGB")
            name = f"{device}-{order}-{scene.split('-', 1)[1]}.png"
            caption(device, shot, title, line).save(os.path.join(out, name), optimize=True)
            print("made", name)
    check = os.path.join(ROOT, "scripts", "dev", "check_appstore_screenshots.py")
    sys.exit(subprocess.run([sys.executable, check, out]).returncode)


if __name__ == "__main__":
    main()
