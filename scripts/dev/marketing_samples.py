# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.

"""Marketing pieces from the five sample projects (docs/MARKETING_RENDERS.md,
pieces 6 and 8): FoldForm's own renderer, Pillow and ffmpeg; nothing to
install beyond the build.

    python scripts/dev/marketing_samples.py [--encode-only]

1. Each sample (resources/samples) opens once in the Windows build with
   --hide-ui --face-contrast --view "0,25;2,25;...;358,25" and saves one
   picture per angle: the model alone in its view, framed the same at every
   angle. A normal window opens for about a minute per sample; the pointer
   is not moved. Then a still at a three-quarter angle, larger.
2. Out of them, in docs/marketing/samples/:
   - <sample>-loop.mp4: 1080 x 1080, 6 s, 30 fps, H.264, a seamless turn;
   - <sample>.png: 1800 x 1800, the still;
   - samples-showcase.png: 1920 x 1080, the five with their names;
   - samples-reel.mp4: 1080 x 1920, about 17 s: a title card, each sample
     turning with its name and what it shows, an end card.

Needs: the build (BUILDING.md), ffmpeg on PATH, a Python with Pillow.
"""

import os
import shutil
import subprocess
import sys

from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
EXE = os.path.join(ROOT, "build", "msys2-ucrt64", "bin", "OpenShape.exe")
SAMPLES = os.path.join(ROOT, "resources", "samples")
WORK = os.path.join(ROOT, "build", "marketing", "samples")
OUT = os.path.join(ROOT, "docs", "marketing", "samples")
FONT_BOLD = os.path.join(ROOT, "resources", "fonts", "NotoSans-Bold.ttf")
FONT = os.path.join(ROOT, "resources", "fonts", "NotoSans-Regular.ttf")

# The app's colors (src/ui/qml/Theme.qml).
BACKGROUND = (0xEC, 0xEE, 0xF1)
TEXT = (0x1F, 0x23, 0x29)
MUTED = (0x6A, 0x71, 0x7C)
ACCENT = (0x29, 0x77, 0xED)
CARD = (0xFF, 0xFF, 0xFF)

# Each sample: its file, what it shows (Home's summary, shorter), its
# still's angle (yaw, pitch) and where its reel part starts turning.
SAMPLES_SHOWN = [
    ("Lamp shade", "A revolved wall · a pattern of windows", (330, 25), 0),
    ("Twisted vase", "A smooth loft · four turned hexagons", (330, 20), 30),
    ("Electronics box", "Standoffs · M3 holes · vents · a lid", (300, 35), 60),
    ("Phone stand", "A fully sized sketch · fillets · mirror", (300, 20), 90),
    ("Honeycomb organizer", "Polygons · sketch patterns · one cut", (330, 35), 120),
]

STEP = 2      # degrees between frames: 180 frames, 6 s at 30 fps
PITCH = 25    # degrees above the horizon
WINDOW = "1080x1080"
DPR = "2"
# The view frames a part's bounding sphere with room around it: the middle
# of the picture (as fractions of its size) holds every sample at any angle.
CROP = (0.19, 0.21, 0.81, 0.83)


def cropped(im):
    w, h = im.size
    return im.crop((int(w * CROP[0]), int(h * CROP[1]), int(w * CROP[2]), int(h * CROP[3])))


def slug(name):
    return name.lower().replace(" ", "-")


def env():
    e = dict(os.environ)
    msys = os.path.join(os.path.expanduser("~"), "msys64", "ucrt64", "bin")
    e["PATH"] = msys + os.pathsep + e.get("PATH", "")
    return e


def run_app(project, views, shot, size, dpr, log):
    with open(log, "w", encoding="utf-8") as out:
        subprocess.run([EXE, project, "--demo", "empty", "--face-contrast", "--hide-ui", "--size", size, "--dpr", dpr,
                        "--view", views, "--screenshot", shot],
                       env=env(), stdout=out, stderr=subprocess.STDOUT, timeout=2400, check=True)


def frame_files(work):
    return [os.path.join(work, f"frame-{yaw}_{PITCH}.png") for yaw in range(0, 360, STEP)]


def render(name, still_view):
    work = os.path.join(WORK, slug(name))
    if all(os.path.exists(f) for f in frame_files(work)) and os.path.exists(os.path.join(work, "still.png")):
        print("  (rendered before; delete", work, "to render again)", flush=True)
        return
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work)
    project = os.path.join(SAMPLES, name + ".openshape")
    views = ";".join(f"{yaw},{PITCH}" for yaw in range(0, 360, STEP))
    run_app(project, views, os.path.join(work, "frame.png"), WINDOW, DPR, os.path.join(work, "turn.log"))
    missing = [f for f in frame_files(work) if not os.path.exists(f)]
    if missing:
        sys.exit(f"{name}: {len(missing)} frames missing, e.g. {missing[0]}")
    # The still: one view, larger.
    run_app(project, f"{still_view[0]},{still_view[1]}", os.path.join(work, "still.png"), "1000x1000", "3",
            os.path.join(work, "still.log"))


def encode_loop(name):
    work = os.path.join(WORK, slug(name))
    scaled = os.path.join(work, "scaled")
    os.makedirs(scaled, exist_ok=True)
    for i, f in enumerate(frame_files(work)):
        cropped(Image.open(f).convert("RGB")).resize((1080, 1080), Image.LANCZOS).save(os.path.join(scaled, f"{i:03d}.png"))
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", os.path.join(scaled, "%03d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-profile:v", "high", "-crf", "22",
                    "-movflags", "+faststart", "-an", os.path.join(OUT, slug(name) + "-loop.mp4")], check=True)
    still = cropped(Image.open(os.path.join(work, "still.png")).convert("RGB")).resize((1800, 1800), Image.LANCZOS)
    still.save(os.path.join(OUT, slug(name) + ".png"), optimize=True)


def font(size, bold=False):
    return ImageFont.truetype(FONT_BOLD if bold else FONT, size)


def rounded(im, radius):
    mask = Image.new("L", im.size, 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, im.size[0] - 1, im.size[1] - 1), radius, fill=255)
    out = Image.new("RGBA", im.size, (0, 0, 0, 0))
    out.paste(im, (0, 0), mask)
    return out


def shadowed(canvas, piece, at, radius, strength=60):
    pad = 60
    shadow = Image.new("RGBA", (piece.size[0] + 2 * pad, piece.size[1] + 2 * pad), (0, 0, 0, 0))
    ImageDraw.Draw(shadow).rounded_rectangle((pad, pad + 10, pad + piece.size[0], pad + 10 + piece.size[1]), radius,
                                             fill=(16, 24, 40, strength))
    canvas.alpha_composite(shadow.filter(ImageFilter.GaussianBlur(22)), (at[0] - pad, at[1] - pad))
    canvas.alpha_composite(piece, at)


def centered(draw, y, text, f, fill, width):
    draw.text(((width - draw.textlength(text, font=f)) / 2, y), text, font=f, fill=fill)


def compose_showcase():
    W, H = 1920, 1080
    canvas = Image.new("RGBA", (W, H), BACKGROUND + (255,))
    draw = ImageDraw.Draw(canvas)
    centered(draw, 92, "Five sample projects, ready to change", font(64, True), TEXT, W)
    centered(draw, 186, "Each opens with every step in the Model panel: change a size and the part follows.",
             font(28), MUTED, W)
    tile, gap = 330, 32
    x0 = (W - 5 * tile - 4 * gap) // 2
    for i, (name, what, _, _) in enumerate(SAMPLES_SHOWN):
        still = Image.open(os.path.join(OUT, slug(name) + ".png")).convert("RGB")
        card = Image.new("RGB", (tile, tile + 120), CARD)
        card.paste(still.resize((tile - 24, tile - 24), Image.LANCZOS), (12, 12))
        d = ImageDraw.Draw(card)
        d.text((18, tile + 6), name, font=font(26, True), fill=TEXT)
        f = font(19)
        # Wrapped to the card between its parts (never inside "M3 holes").
        rows, line = [], ""
        for part in what.split(" · "):
            trial = part if not line else line + " · " + part
            if line and d.textlength(trial, font=f) > tile - 36:
                rows.append(line)
                line = part
            else:
                line = trial
        rows.append(line)
        for r, row in enumerate(rows[:2]):
            d.text((18, tile + 46 + 26 * r), row, font=f, fill=MUTED)
        shadowed(canvas, rounded(card, 20), (x0 + i * (tile + gap), 300), 20)
    centered(draw, 870, "FoldForm", font(44, True), TEXT, W)
    centered(draw, 930, "Precise CAD for 3D printing, made for touch.  ·  Open source  ·  no account  ·  no tracking",
             font(24), MUTED, W)
    out = os.path.join(OUT, "samples-showcase.png")
    canvas.convert("RGB").save(out, optimize=True)
    return out


def card_frame(lines, W=1080, H=1920):
    """A title or end card: centred lines of (text, size, bold, color)."""
    canvas = Image.new("RGB", (W, H), BACKGROUND)
    draw = ImageDraw.Draw(canvas)
    total = sum(int(size * 1.5) for _, size, _, _ in lines)
    y = (H - total) // 2
    for text, size, bold, color in lines:
        centered(draw, y, text, font(size, bold), color, W)
        y += int(size * 1.5)
    return canvas


def compose_reel():
    W, H = 1080, 1920
    reel = os.path.join(WORK, "reel")
    shutil.rmtree(reel, ignore_errors=True)
    os.makedirs(reel)
    n = 0

    def put(im):
        nonlocal n
        im.save(os.path.join(reel, f"{n:04d}.png"))
        n += 1

    title = card_frame([("FoldForm", 96, True, TEXT), ("Five sample projects", 48, False, TEXT),
                        ("to open, change and learn from", 40, False, MUTED)])
    for _ in range(45):
        put(title)
    per = 78  # frames per sample: 2.6 s, a little more than a quarter turn
    for name, what, _, start in SAMPLES_SHOWN:
        frames = frame_files(os.path.join(WORK, slug(name)))
        first = start // STEP
        for k in range(per):
            view = cropped(Image.open(frames[(first + k) % len(frames)]).convert("RGB")).resize((1000, 1000), Image.LANCZOS)
            canvas = Image.new("RGB", (W, H), BACKGROUND)
            canvas.paste(view, (40, 420))
            d = ImageDraw.Draw(canvas)
            # The words fade in over the first 8 frames.
            a = min(1.0, (k + 1) / 8)
            mix = lambda c: tuple(int(BACKGROUND[j] + (c[j] - BACKGROUND[j]) * a) for j in range(3))
            centered(d, 210, name, font(72, True), mix(TEXT), W)
            centered(d, 318, what, font(38), mix(MUTED), W)
            centered(d, 1520, "Open it on Home, change any step", font(34), mix(ACCENT), W)
            put(canvas)
    end = card_frame([("FoldForm", 96, True, TEXT), ("Precise CAD for 3D printing,", 44, False, TEXT),
                      ("made for touch.", 44, False, TEXT), ("", 30, False, TEXT),
                      ("iPhone  ·  iPad  ·  iPhone Duo  ·  Windows", 34, False, ACCENT),
                      ("Open source  ·  no account  ·  no tracking", 32, False, MUTED)])
    for _ in range(60):
        put(end)
    out = os.path.join(OUT, "samples-reel.mp4")
    subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", "30", "-i", os.path.join(reel, "%04d.png"),
                    "-c:v", "libx264", "-pix_fmt", "yuv420p", "-profile:v", "high", "-crf", "21",
                    "-movflags", "+faststart", "-an", out], check=True)
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    if "--encode-only" not in sys.argv[1:]:
        for name, _, still_view, _ in SAMPLES_SHOWN:
            print("rendering", name, flush=True)
            render(name, still_view)
    for name, _, _, _ in SAMPLES_SHOWN:
        encode_loop(name)
    print(compose_showcase())
    print(compose_reel())


if __name__ == "__main__":
    main()
