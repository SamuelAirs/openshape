# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""The website hero (docs/MARKETING_RENDERS.md, piece 5): a still and a
turning-part loop, from FoldForm's own renderer and ffmpeg. Nothing to install
beyond the build, ffmpeg and Pillow.

    python scripts/dev/marketing_hero.py [--frames-only | --compose-only | --encode-only]

1. The loop: the Windows build opens the project box from the App Store
   screenshots (build/appstore-screenshots/.../Project box.openshape) once,
   with --view "0,25;2,25;...;358,25" --face-contrast (no selection, no hover)
   and saves one picture per angle; the framing is the same for every angle
   (Camera::fit frames the box's bounding sphere). A normal window opens on
   the screen for about a minute; the pointer is not moved.
2. Each frame is cropped to the view's middle (clear of the tool column, the
   Model panel, the view cube and the hint) and joined at 30 fps into
   docs/marketing/hero-loop.mp4 (H.264, no audio) and .webm (VP9), plus a
   poster PNG and a GIF for READMEs.
3. The still (1600 x 900): the headline beside the iPhone and iPad App Store
   screenshots in plain rounded frames in the app's colors (Apple's device
   frames need the owner's Apple sign-in), docs/marketing/hero.png.

Needs: the build (BUILDING.md), ffmpeg on PATH, a Python with Pillow
(PIL). The app's DLLs come from MSYS2 (added to PATH here).
"""

import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(ROOT, "build", "msys2-ucrt64", "bin", "OpenShape.exe")
PROJECT = os.path.join(ROOT, "build", "appstore-screenshots", "iphone-6-home", "OpenShape", "Project box.openshape")
WORK = os.path.join(ROOT, "build", "marketing", "turntable")
OUT = os.path.join(ROOT, "docs", "marketing")
FONT_BOLD = os.path.join(ROOT, "resources", "fonts", "NotoSans-Bold.ttf")
FONT = os.path.join(ROOT, "resources", "fonts", "NotoSans-Regular.ttf")
SHOTS = os.path.join(ROOT, "docs", "appstore", "screenshots")

# The app's colors (src/ui/qml/Theme.qml).
BACKGROUND = (0xEC, 0xEE, 0xF1)
PANEL = (0xF9, 0xFA, 0xFB)
BORDER = (0xD8, 0xDC, 0xE2)
TEXT = (0x1F, 0x23, 0x29)
MUTED = (0x6A, 0x71, 0x7C)
ACCENT = (0x29, 0x77, 0xED)

STEP = 2      # degrees between frames: 180 frames, 6 s at 30 fps
PITCH = 25    # degrees above the horizon
SIZE = "1600x1200"
DPR = "2"


def env():
    e = dict(os.environ)
    msys = os.path.join(os.path.expanduser("~"), "msys64", "ucrt64", "bin")
    e["PATH"] = msys + os.pathsep + e.get("PATH", "")
    return e


def render_frames():
    shutil.rmtree(WORK, ignore_errors=True)
    os.makedirs(WORK)
    views = ";".join(f"{yaw},{PITCH}" for yaw in range(0, 360, STEP))
    shot = os.path.join(WORK, "frame.png")
    log = os.path.join(WORK, "render.log")
    with open(log, "w", encoding="utf-8") as out:
        subprocess.run([EXE, PROJECT, "--demo", "empty", "--face-contrast", "--size", SIZE, "--dpr", DPR,
                        "--view", views, "--screenshot", shot],
                       env=env(), stdout=out, stderr=subprocess.STDOUT, timeout=900, check=True)
    frames = [os.path.join(WORK, f"frame-{yaw}_{PITCH}.png") for yaw in range(0, 360, STEP)]
    missing = [f for f in frames if not os.path.exists(f)]
    if missing:
        sys.exit(f"{len(missing)} frames missing, e.g. {missing[0]} (see {log})")
    return frames


def crop_box(size):
    """The view's middle: a 16:9 box clear of the panels, the cube and the hint."""
    w, h = size
    # As fractions of the window (the layout scales with it): the tool column
    # ends near 9 %, the Model panel starts near 73 % (down to 40 %), the view
    # cube and the buttons sit in the bottom right, the hint along the bottom.
    left, right = int(w * 0.2), int(w * 0.725)
    width = right - left
    height = width * 9 // 16
    cy = int(h * 0.515)
    top = cy - height // 2
    return (left, top, left + width, top + height)


def encode(frames):
    os.makedirs(OUT, exist_ok=True)
    cropped = os.path.join(WORK, "cropped")
    os.makedirs(cropped, exist_ok=True)
    box = None
    for i, f in enumerate(frames):
        im = Image.open(f).convert("RGB")
        box = box or crop_box(im.size)
        im.crop(box).resize((1280, 720), Image.LANCZOS).save(os.path.join(cropped, f"{i:03d}.png"))
    pattern = os.path.join(cropped, "%03d.png")
    common = ["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", pattern]
    subprocess.run(common + ["-c:v", "libx264", "-pix_fmt", "yuv420p", "-profile:v", "high", "-crf", "24",
                             "-movflags", "+faststart", "-an", os.path.join(OUT, "hero-loop.mp4")], check=True)
    subprocess.run(common + ["-c:v", "libvpx-vp9", "-b:v", "0", "-crf", "46", "-row-mt", "1", "-an",
                             os.path.join(OUT, "hero-loop.webm")], check=True)
    # A small GIF for READMEs and forum posts: 480 px, 12 fps, its own palette.
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", pattern, "-vf",
                    "fps=12,scale=480:-1:flags=lanczos,split[a][b];[a]palettegen=max_colors=48:stats_mode=diff[p];"
                    "[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
                    os.path.join(OUT, "hero-loop.gif")], check=True)
    # The poster: a three-quarter view from the front left (the first frame
    # looks along the long side).
    shutil.copy(os.path.join(cropped, f"{(330 // STEP):03d}.png"), os.path.join(OUT, "hero-loop-poster.png"))


def existing_frames():
    frames = [os.path.join(WORK, f"frame-{yaw}_{PITCH}.png") for yaw in range(0, 360, STEP)]
    if not all(os.path.exists(f) for f in frames):
        sys.exit("no frames yet: run without --encode-only first")
    return frames


def rounded(im, radius, border=None, width=0):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), radius, fill=255)
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(im, (0, 0), mask)
    if border:
        ImageDraw.Draw(out).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), radius, outline=border, width=width)
    return out


def shadowed(canvas, piece, at, radius):
    shadow = Image.new("RGBA", (piece.size[0] + 80, piece.size[1] + 80), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((40, 48, 40 + piece.size[0], 48 + piece.size[1]), radius, fill=(20, 30, 50, 70))
    shadow = shadow.filter(ImageFilter.GaussianBlur(18))
    canvas.alpha_composite(shadow, (at[0] - 40, at[1] - 40))
    canvas.alpha_composite(piece, at)


def compose_still():
    W, H = 1600, 900
    canvas = Image.new("RGBA", (W, H), BACKGROUND + (255,))
    draw = ImageDraw.Draw(canvas)
    # A soft lighter band behind the devices.
    glow = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((620, -120, 1720, 1020), fill=(255, 255, 255, 150))
    canvas.alpha_composite(glow.filter(ImageFilter.GaussianBlur(80)))

    # The iPad (landscape) behind, the iPhone in front.
    ipad = Image.open(os.path.join(SHOTS, "ipad-1-project-box.png")).convert("RGB")
    iw = 780
    ipad = ipad.resize((iw, iw * ipad.size[1] // ipad.size[0]), Image.LANCZOS)
    bezel = 18
    ipad_frame = Image.new("RGB", (ipad.size[0] + 2 * bezel, ipad.size[1] + 2 * bezel), TEXT)
    ipad_frame.paste(rounded(ipad, 14), (bezel, bezel), rounded(ipad, 14))
    ipad_frame = rounded(ipad_frame, 34)
    shadowed(canvas, ipad_frame, (700, 150), 34)

    phone = Image.open(os.path.join(SHOTS, "iphone-1-project-box.png")).convert("RGB")
    ph = 640
    phone = phone.resize((ph * phone.size[0] // phone.size[1], ph), Image.LANCZOS)
    pb = 12
    phone_frame = Image.new("RGB", (phone.size[0] + 2 * pb, phone.size[1] + 2 * pb), TEXT)
    phone_frame.paste(rounded(phone, 34), (pb, pb), rounded(phone, 34))
    phone_frame = rounded(phone_frame, 44)
    shadowed(canvas, phone_frame, (1250, 190), 44)

    # The words.
    title = ImageFont.truetype(FONT_BOLD, 76)
    lead = ImageFont.truetype(FONT, 34)
    small = ImageFont.truetype(FONT, 24)
    x = 96
    draw.text((x, 250), "FoldForm", font=title, fill=TEXT)
    draw.text((x, 360), "Precise CAD for 3D printing,", font=lead, fill=TEXT)
    draw.text((x, 404), "made for touch.", font=lead, fill=TEXT)
    lines = ["Push and pull faces, sketch with constraints,", "type exact sizes. Export STL, 3MF or STEP."]
    for i, line in enumerate(lines):
        draw.text((x, 480 + 34 * i), line, font=small, fill=MUTED)
    # A pill naming the platforms.
    pill = "iPhone  ·  iPad  ·  iPhone Duo  ·  Windows"
    pw = draw.textlength(pill, font=small)
    draw.rounded_rectangle((x, 600, x + pw + 40, 650), 25, fill=ACCENT)
    draw.text((x + 20, 609), pill, font=small, fill=(255, 255, 255))
    draw.text((x, 690), "Open source  ·  no account  ·  no tracking", font=small, fill=MUTED)

    out = os.path.join(OUT, "hero.png")
    canvas.convert("RGB").save(out, optimize=True)
    return out


def main():
    args = set(sys.argv[1:])
    if "--compose-only" not in args:
        # --encode-only: the frames rendered last time (no window opens).
        frames = existing_frames() if "--encode-only" in args else render_frames()
        encode(frames)
    if "--frames-only" not in args:
        print(compose_still())


if __name__ == "__main__":
    main()
