# FoldForm marketing: renders, animations and screenshots to ask for

_Written 2026-11-01 at the owner's request ("detailed plans" for renders
or animations, and "requests for any screenshots that may help
marketing"). On 2026-11-04 the owner said to go ahead ("in the order that
makes sense and download the necessary tools"): what is made is marked
**Made** below, with its files; the recordings only the owner can make are
in [Recording briefs](#recording-briefs-for-the-owner)._

Contents: [what this PC has](#what-this-pc-has-checked-2026-11-01),
[Apple's rules](#apples-rules-that-shape-the-plan-read-2026-11-01),
[the pieces](#the-pieces-in-the-order-worth-doing),
[recording briefs](#recording-briefs-for-the-owner),
[screenshot requests for the owner](#screenshot-and-recording-requests-for-the-owner),
[the first App Store screenshots](#the-first-app-store-screenshots-2026-10-31-and-what-marketing-wants-instead),
[decisions](#decisions-for-the-owner).

## What this PC has (checked 2026-11-01)

| Tool | State | Used for |
|---|---|---|
| **ffmpeg 7.0.2** (Gyan full build, winget) | installed, on `PATH` | cutting, scaling, cropping, speed changes, captions (`drawtext`), screen capture of a window or region (`gdigrab`), encoding: H.264 (`libx264`, or the GPU's `h264_amf`), HEVC, ProRes 422 HQ (`prores_ks`), VP9 (web), GIF |
| **Python 3 with Pillow 12.3** | installed | stills: crops, device-frame composites, captions on screenshots, contact sheets |
| **Microsoft Edge** (`msedge.exe --headless --screenshot`) | installed | title and end cards laid out in HTML/CSS, rendered to PNG at an exact size |
| **FoldForm for Windows** (`build/msys2-ucrt64/bin/OpenShape.exe`) | built | the real app: `--demo <scene>` (about 30 scripted scenes, e.g. `enclosure`, `holes`, `text`, `loft`, `store-enclosure`), `--touch --size WxH --dpr N --safe-area t,r,b,l` (any device's point size and insets), `--view iso\|front\|...\|yaw,pitch` (any angle, framed), `--screenshot file.png` (a still at that size), `--store-screenshot` (frameless, larger than the screen) |
| `scripts/dev/drive.py` | in the repo | drives the real window (mouse, keys, taps by coordinates) for scripted screen recordings |
| `scripts/dev/appstore_screenshots.sh`, `doc_screenshots.sh` | in the repo | the store and README stills (rendered, then checked) |
| Noto Sans Regular and Bold (`resources/fonts`, SIL OFL) | in the repo | the captions' and cards' type (free to use in marketing) |
| Blender 4.5 LTS (`~/Tools/blender-4.5.15-windows-x64`, portable) | installed (the owner's word, 2026-11-03) | piece 8: `scripts/dev/marketing_blender.py`, Cycles on the RX 7800 XT (HIP) |
| OBS Studio, ImageMagick, Inkscape, a video editor (DaVinci Resolve, Shotcut), gifski | **not installed** | named below where a piece would need one |
| Apple's device frames (Apple Design Resources, "Product Bezels") | **not downloaded** | device mockups for the website and press (Apple's own terms for using them apply) |

What the app's renderer can and cannot do today: it draws the CAD view
with the same code as on iOS (Qt RHI: Direct3D 11 here, Metal on the
devices), so stills of the model are true to the app. A still always
includes the app's UI around and over the view (view cube, hints); there
is no model-only or transparent-background export, and no option that
writes an animation's frames. Both are small code options for a later
version (below); nothing in this plan needs them first.

What a Windows render cannot be: the iPhone's own look (San Francisco
type instead of Segoe UI, the status bar), the fold of the Duo, the Apple
Pencil's strokes and palm rejection, the share sheet, or a hand on glass.
Those come from the owner's devices.

## Apple's rules that shape the plan (read 2026-11-01)

- **App preview video** (App Store Connect, "App preview
  specifications"): 15 to 30 seconds, up to three per device size, at most
  500 MB; H.264 (`.mov`, `.m4v`, `.mp4`; 10 to 12 Mbps, progressive, High
  Profile up to Level 4.0) or ProRes 422 HQ (`.mov`); at most 30 frames
  per second; a stereo audio track (AAC 256 kbps, 44.1 or 48 kHz).
  Accepted sizes: **iPhone 886 x 1920** portrait or 1920 x 886 landscape
  (the same for the 6.9", the 6.3" and the iPhone Duo); **iPad 13" 1200 x
  1600** or 1600 x 1200. The poster frame defaults to 5 seconds in. Previews
  play before the screenshots.
- **Guideline 2.3.4:** "previews may only use video screen captures of the
  app itself"; narration and text overlays are allowed. Apple's preview
  guidance adds: no people, no hands or over-the-shoulder shots, licensed
  music only, and say when a feature shown needs an in-app purchase (text
  in the footage or the end frame).
- **Guideline 2.3.10:** no other platforms in App Store media, so the
  Windows version stays out of the preview and the screenshots.
- **Screenshots (2.3.3):** the app in use; text and image overlays are
  allowed (captions, touch points). iPhone Duo screenshots (outer 1398 x
  2034, inner 2007 x 2853) give the product page an "optimized for iPhone
  Duo" badge now and are required from April 2027 for apps built with the
  iOS 27.1 SDK.
- **2.3.2:** what needs the purchase must be clear; a picture with several
  projects is fine because the description says so, and a caption can say
  it too.

So: the **App Store preview is a screen recording made on the iPhone**
(not the Windows build: Apple asks for footage captured on the device),
edited here. The Duo's fold, the Windows version, hands and the Pencil go
to the website, social posts and press, never into the App Store media.

## The pieces, in the order worth doing

Hours are the lead's (editing, scripting) and the owner's (recording on a
device) separately; they assume nothing new is installed unless the piece
says so.

### 1. App Store preview, iPhone (the one that sells in the store)

- **For:** the product page; it autoplays above the screenshots.
- **Length:** 25 to 30 s. **Frame:** none (app footage only), portrait,
  886 x 1920, 30 fps, silent stereo AAC track (or a licensed track, a
  decision below).
- **Shot list** (each a separate take, cut tight; a caption of three to
  five words on each, Noto Sans, top third, never over the part):
  1. 0-3 s, "Precise CAD by touch": Home → **New project** → **Box**; the
     20 mm cube drops in.
  2. 3-8 s, "Pull. Or type the size.": tap the top face, drag the arrow up,
     then tap the value and type `35` on FoldForm's keypad, ✓.
  3. 8-12 s, "Round any edge": tap a vertical edge, type `4`, ✓.
  4. 12-17 s, "Shell it": tap the top face, **Shell**, type `2`: an open
     box with 2 mm walls.
  5. 17-22 s, "Holes that fit screws": **Hole**, M3 countersunk with the
     print allowance, tap two spots on a wall.
  6. 22-26 s, "Raised text": **Text** on the side, type a word.
  7. 26-30 s, end card: the app icon, "FoldForm", "Free for one project.
     Unlimited projects: in-app purchase." (The share sheet is skipped
     here: it shows other apps' icons.)
- **Produced:** the owner records each take with iOS **Screen Recording**
  (Control Center) on the iPhone 16 Pro, build 54 or later, Focus on (no
  notifications), Light appearance, and sends the files (AirDrop to a Mac
  is not available; iCloud Drive or a cable to this PC). Here: `ffmpeg`
  trims and speeds up the takes, scales the recording (the 16 Pro
  records at about its screen size, 1206 x 2622) to 886 x 1926 and crops 3 px top and bottom to 886 x 1920 (or pads, if the cut
  would touch a button), drops to 30 fps, burns in the captions
  (`drawtext`), adds the end card (Edge renders it to PNG), and encodes
  H.264 High, Level 4.0, 10 to 12 Mbps, plus a silent AAC 256 kbps stereo
  track. Check: `ffprobe` shows 886 x 1920, ≤30 fps, 15 to 30 s, audio
  present.
- **A dry run first, here:** the same shot list played in the Windows
  build at the iPhone's point size (`--touch --size 443x960 --safe-area
  62,0,34,0`) with `drive.py`, captured with `ffmpeg -f gdigrab` while
  nobody uses the PC: a storyboard to agree on timing and captions before
  the owner records. Not for upload.
- **Hours:** lead 4 (storyboard 1.5, edit 2.5); owner 0.5 to 1
  (recording, two or three tries per take).
- **Only the owner can record:** all of it (the store wants on-device
  footage).

### 2. App Store preview, iPad (optional, after 1)

- **For:** the iPad product page. **Length:** 20 to 30 s. Landscape, 1600
  x 1200, from the iPad Air 11" (2360 x 1640, scaled and cropped to 4:3).
- **Shots:** the full layout with the Model panel; sketch a mounting
  plate with the **Apple Pencil** (strokes and constraints appear; the
  pen's strokes are in the recording, the hand is not); change a size in
  the Model panel and watch the part follow; Split View beside Files.
- **Hours:** lead 2.5; owner 0.5. **Owner only:** the Pencil strokes.

### 3. App Store screenshots v2, with captions (for 0.3.1)

- **For:** the listing's first impression: only the first three
  screenshots show in search results.
- **What changes:** a short benefit caption above each (e.g. "Type exact
  sizes", "Holes that fit M3 screws", "Sketch with your finger", "Raised
  text for labels", "Change any step later", "Made for iPhone Duo"); the
  UI from the owner's real device screenshots (San Francisco type, status
  bar) placed on a plain background in the app's colors, so one iPhone 16
  Pro screenshot fills both the 6.9" (1320 x 2868) and the 6.3" (1206 x
  2622) canvas; a Duo set straight from the Duo (badge).
- **Produced:** a Pillow script in `scripts/dev/` (canvas, caption in
  Noto Sans Bold, the screenshot scaled with rounded corners), checked by
  `check_appstore_screenshots.py`. No device frame (Apple neither needs
  nor shows one).
- **Hours:** lead 3 (script and two rounds of wording); owner 0.5 (the
  screenshots in the requests below).
- **Made (2026-11-04)** from the twelve existing screenshots by
  `scripts/dev/appstore_captions.py` (Pillow, Noto Sans), in
  `docs/appstore/captioned/` for the listing tool to upload with 0.3.1:
  `iphone-1` … `iphone-6` (1320 x 2868) and `ipad-1` … `ipad-6` (2752 x
  2064), RGB, checked by `check_appstore_screenshots.py`. Each shows the
  whole screenshot in a plain dark frame under its caption, in this order:
  1. "Type the size you measured" / "Holes that fit M3 screws, with a
     print allowance" (the project box);
  2. "Sketch with your finger" / "Constraints, and sizes you type";
  3. "Raised text for labels" / "Engraved too: push it in" (0.3.1);
  4. "Change any step later" / "Every size stays editable" (the Model
     panel);
  5. "Planes and axes where you need them" / "Sketch, mirror and turn
     around them";
  6. "All your projects, with previews" / "Unlimited projects with the
     in-app purchase" (guideline 2.3.2).

  No "Made for iPhone Duo" caption (Apple's badge wording) and no Duo
  picture yet: the Duo set comes from the owner's captures. Rerun the
  script on his device screenshots (same names) to caption those.

### 4. The iPhone Duo, folded and unfolded (website, social, press)

- **For:** the strongest proof that FoldForm is made for the Duo; it
  cannot be an App Store preview (it shows the device).
- **Length:** 12 to 20 s, 9:16 vertical (1080 x 1920) and 16:9 (1920 x
  1080) cuts.
- **Shots:** the Duo closed, the outer display showing the project box
  with the value box open; unfold slowly: the same part on the inner
  display, the Model panel beside it, the value still typed; turn the
  inner display; fold back to the outer display: nothing lost. A second
  take: half folded (laptop pose) with the part on the upper half.
- **Produced:** the owner films the device on a table with another phone
  on a stand (4K 30 fps, a plain matte background, one soft light, no
  reflections on the screen, auto-exposure locked), and records the
  Duo's own screen at the same time (Screen Recording) if it keeps
  recording through the fold. Here: `ffmpeg` cuts, stabilizes lightly
  (`deshake`), color-matches roughly, and puts the screen recording
  beside or inside the camera shot for the website cut.
- **Hours:** lead 3; owner 1 (setting up the light is most of it).
- **Only the owner can record:** all of it. No render can fake the fold.

### 5. Website hero (the README today; a GitHub Pages page if wanted)

- **For:** the first thing on github.com/SamuelAirs/openshape (no other
  website exists; GitHub Pages would be a new service, the owner's call).
- **Pieces:** (a) a still, 1600 x 900: the iPhone and the iPad side by
  side in Apple's device frames, the project box on the phone, the Model
  panel on the iPad; (b) a 6 to 8 s silent loop of a part turning (MP4
  H.264 and WebM VP9, under 2 MB, plus a poster PNG).
- **Produced:** (a) Pillow composites the owner's real screenshots into
  the frames (needs **Apple Design Resources "Product Bezels"**, a
  download, under Apple's terms); without them, plain rounded rectangles
  in the app's colors. (b) The turntable from the app's own renderer, with
  no code change: run the Windows build once per frame, `--demo enclosure
  --view <yaw>,25 --size 1600x1200 --screenshot f_<n>.png` for yaw 0 to
  358 in steps of 2 (180 runs, about 15 to 30 minutes, windows open while
  it runs, so not while the PC is in use), crop the view with Pillow, join
  with `ffmpeg` at 30 fps. Check the first frames: `--view` reframes each
  angle, so the part may "breathe" a little; if it does, a fixed-zoom view
  option is a small code change (about 2 hours with its test, a later
  version).
- **Hours:** lead 4 (still 1.5, loop 2.5); owner 0.25 (two screenshots).
- **Made (2026-11-04)** by `scripts/dev/marketing_hero.py`, nothing
  installed: one run of the Windows build opens the project box once with
  `--view "0,25;2,25;...;358,25" --face-contrast` and saves 180 pictures
  (a normal window for about a minute; the pointer is not moved; no breathing:
  `Camera::fit` frames the box's bounding sphere, the same at every angle),
  each cropped to the view's middle and joined by ffmpeg:
  - `docs/marketing/hero-loop.mp4`: 1280 x 720, 30 fps, 6 s, H.264, no
    audio, 1.7 MB (loops seamlessly: 0 to 358 degrees);
  - `docs/marketing/hero-loop.webm`: the same in VP9, 1.3 MB;
  - `docs/marketing/hero-loop.gif`: 480 px, 12 fps, 3.2 MB, for READMEs;
  - `docs/marketing/hero-loop-poster.png`: its three-quarter frame;
  - `docs/marketing/hero.png`: the still, 1600 x 900: "FoldForm", the
    line "Precise CAD for 3D printing, made for touch.", the platforms
    and "Open source · no account · no tracking" beside the iPad and
    iPhone App Store screenshots of the project box in plain dark rounded
    frames (Apple's own device frames need the owner's Apple sign-in).

  The still uses the Windows-rendered screenshots; once the owner's
  device screenshots exist, `--compose-only` rebuilds it from them.

### 6. Social clips (a set of five, 6 to 15 s each)

- **For:** Instagram/TikTok/YouTube Shorts (9:16, 1080 x 1920), X and
  YouTube (16:9), Instagram feed (4:5, 1080 x 1350).
- **One idea each:**
  1. "Type the size": push a face, type `35`, the part follows (iPhone
     recording).
  2. "Holes that fit": Hole M3 with the print allowance, then the printed
     part with a screw in it (needs a printed part, a question below).
  3. "Sketch with your finger": rectangle, circle, a size typed, Finish,
     cut through (iPhone).
  4. "Folds with you": the Duo cut from piece 4.
  5. "Pencil on iPad": a sketch drawn with the Pencil (iPad), filmed over
     the shoulder (allowed off the App Store).
- **Produced:** from the owner's recordings and piece 4; `ffmpeg` crops
  to each aspect, captions burned in, a GIF (`gif` encoder) for READMEs
  and forum posts.
- **Hours:** lead 1 per clip after the first (the first 2); owner 0.5
  for the recordings not already made for pieces 1, 2 and 4.

### 7. Windows beside the iPhone (website, social)

- **For:** "the same project on your phone and your PC, free on Windows".
- **Length:** 10 to 15 s, 16:9. **Shots:** the project box saved on the
  iPhone, shared (AirDrop is not available to Windows: iCloud Drive or
  Mail), opened in FoldForm on Windows, the same history in the Model
  panel, one change, saved.
- **Produced:** the Windows half captured here with `ffmpeg -f gdigrab`
  (a fixed region, since the window title changes with the project),
  scripted by `drive.py` while the PC is idle; the iPhone half from the
  owner's Screen Recording; side by side with `ffmpeg` (`hstack` on a
  1920 x 1080 canvas).
- **Hours:** lead 3; owner 0.25.

### 8. Beauty renders of parts (press kit, website, social backgrounds)

- **For:** pictures where the part itself looks like a product (plastic
  in light), not CAD.
- **Two routes:**
  - **The app's renderer** (now): large stills from any angle with
    `--demo ... --view ... --screenshot`, cropped. True to the app,
    flat CAD shading.
  - **Blender** (installed 2026-11-03): each body as STL
    (`tools/export_bodies`), a PLA-like material, softbox light and a
    seamless backdrop built by the script (no HDRI download), Cycles on
    the RX 7800 XT (AMD HIP).
- **Hours:** app renderer 1; Blender 2 to set up, then about 1 per still
  and 2 per turntable.
- **Made (2026-11-03), the app's renderer route**, from the five sample
  projects, by `scripts/dev/marketing_samples.py` (nothing installed; the
  new `--hide-ui` draws the model's view alone, no panels or view cube):
  in `docs/marketing/samples/`, per sample a seamless 6 s turn
  (`<sample>-loop.mp4`, 1080 x 1080, 30 fps, H.264, no audio) and a still
  (`<sample>.png`, 1800 x 1800, three-quarter view); `samples-showcase.png`
  (1920 x 1080: "Five sample projects, ready to change", the five on white
  cards with what each shows); `samples-reel.mp4` (1080 x 1920, about 17 s,
  for Reels/Shorts/TikTok: a title card, each sample turning with its name
  and what it shows, the end card with the platforms and "Open source · no
  account · no tracking"). Silent: a track is the owner's decision (2).
- **Made (2026-11-03), the Blender route**, by
  `scripts/dev/marketing_blender.py` (`--preview` for small test renders
  in seconds; `--only "<sample>"`): in `docs/marketing/blender/`, per
  sample a still (`<sample>.png`, 1800 x 1800) in a printing colour on a
  seamless backdrop (the lamp shade dark, lit by a warm bulb inside whose
  light falls through its 16 windows), and `samples-family.png` (2400 x
  1350, the five side by side). Colours: the lid in the app's accent blue,
  the rest chosen for the set. About a minute per still on the GPU.
  `--turntable` made `lamp-shade-turn.mp4` and `twisted-vase-turn.mp4`
  (8 s seamless turns, 1080 x 1080, 30 fps, H.264, no audio; 160 samples
  and a fixed noise seed, so nothing flickers). About 10 s a frame: 45
  minutes for the lamp shade, 37 for the vase. The other three samples
  have no Blender turn yet (their app-renderer loops are in `samples/`);
  a stopped run picks up from the frames it already wrote in
  `build/marketing/blender/frames/`.

### Later, code that would make all of this easier (not now)

A model-only or transparent-background export at any size and angle
(from the project-preview renderer), and an option that writes a camera
orbit's frames: about half a day with tests, after 0.3.1's list
(PROJECT_STATUS.md).

## Recording briefs for the owner

Two recordings no render can stand in for. One task each.

### Recording A: the App Store preview (iPhone 16 Pro)

1. **Device and build:** the iPhone 16 Pro, FoldForm 0.3.1 from TestFlight
   (build 55 or newer), held upright (portrait).
2. **Settings first:** Focus on (Do Not Disturb), Light appearance,
   Display Zoom Standard, brightness up, battery over 50 %, Screen Recording
   in Control Center (Settings → Control Center), its microphone off.
3. **A fresh project for each take**, one Screen Recording per take,
   started from Control Center and stopped the same way; 1 s still before
   and after the action; slow, deliberate taps:
   1. Home → **New project** → **Box**: the cube drops in (5 s).
   2. Tap the top face, drag the arrow up a little, then tap the value,
      type **35** on the keypad, tap ✓ (8 s).
   3. Tap a vertical edge, type **4**, ✓: the edge rounds (6 s).
   4. Tap the top face, **Shell**, type **2**, ✓: an open box (6 s).
   5. On a side face: **Hole**, M3, Countersink, tap two spots, ✓ (8 s).
   6. On the other side: **Text**, type **FOLD**, ✓ (8 s).
4. **Length:** about 40 s of takes in all; the lead cuts them to 25-30 s
   (886 x 1920, captions, end card "Free for one project. Unlimited
   projects: in-app purchase.").
5. **Send:** Photos → select the six → Share → Save to Files → iCloud
   Drive (or mail them in batches); tell the lead the folder.

### Recording B: the iPhone Duo, folded and unfolded (website, social)

1. **Devices:** the Duo with FoldForm 0.3.1 (build 55 or newer), and a
   second phone or camera on a stand or tripod filming it: 4K, 30 fps,
   landscape for the 16:9 cut (a second take upright for 9:16).
2. **Setup:** a plain matte table and wall (white or light grey), one soft
   light from the side, no window behind the camera (no reflections on the
   Duo's screen), the Duo's brightness at the top, Focus on, Light
   appearance. On the filming phone, lock focus and exposure on the Duo
   (touch and hold the Duo in the Camera app until AE/AF LOCK shows).
3. **The part:** the project box (or any part) open, a face selected,
   the value box showing a typed value.
4. **Shot 1 (15 s):** the Duo closed, the part on the outer display, held
   2 s; unfold slowly to flat over 3 s; the same part on the inner display,
   held 3 s; turn the Duo a little to show the screen; fold back slowly;
   held 2 s.
5. **Shot 2 (8 s):** half folded on the table (laptop pose), the part on
   the upper half, still; then a pinch on the upper half.
6. **Also, on the Duo:** a Screen Recording during shot 1 (Control Center),
   in case it keeps recording through the fold (the lead puts it beside the
   camera shot).
7. **Send:** the camera's files and the Duo's screen recording (iCloud
   Drive or AirDrop to the filming phone, then iCloud Drive); about 1 GB.

## Screenshot and recording requests for the owner

**Before any capture:** build 54 or newer from TestFlight; **Focus** on
(no notifications); Light appearance; battery charged (a full battery
icon); the sample projects on the device: the six from the 6.9" set are on
the PC in
`C:\Users\ayers\OpenShape\build\appstore-screenshots\iphone-6-home\OpenShape\`
(Project box, Key tag, Mounting plate, Flange plate, Spacer M8, Pen cup);
mail them to yourself or put them in iCloud Drive and open each in
FoldForm. A screenshot is side button + volume up; send the originals
(not via a messaging app, which shrinks them).

### iPhone 16 Pro (portrait screenshots are 1206 x 2622)

1. **Project box, typing a size.** Open *Project box*, tap the cable
   hole's wall, type `8.4` on the keypad, do not apply: the value box,
   the arrow and the keypad on screen. Portrait. For: the listing v2 (#1),
   the website hero.
2. **A sketch with constraints.** *Mounting plate* → Model panel → edit
   its sketch, so the sizes and constraint glyphs show; "Fully defined" in
   view. Portrait. For: listing v2, social clip 3.
3. **The Hole tool.** *Flange plate*, Hole tool, M3 countersunk, the
   allowance field showing 0.2, two holes placed (before applying).
   Portrait. For: listing v2 ("holes that fit"), social clip 2.
4. **Raised text.** *Key tag*, tap the text, its size and depth showing.
   Portrait. For: listing v2, social.
5. **The Model panel.** *Project box*, Model panel open, the fillet step
   open with its radius. Portrait. For: listing v2 ("change any step
   later").
6. **The purchase sheet** is not needed (App Review has its own).
7. **Screen recordings for the preview (piece 1):** the six takes in its
   shot list, each 5 to 10 s, portrait, a fresh project for each take.

### iPhone Duo (outer 1398 x 2034, inner 2007 x 2853, either way round: the badge sizes)

8. **Outer display, upright:** *Project box* with the keypad open (as 1).
   For: the Duo screenshot slot, website.
9. **Outer display, sideways:** the same project, the one-row value box.
   For: the Duo slot.
10. **Inner display, book (side bar showing):** *Project box* with the
    Model panel open and the tool palette, the full layout. For: the Duo
    slot (the strongest one), website hero, press.
11. **Inner display, turned (tall):** the mounting plate's sketch. For:
    the Duo slot.
12. **Split View, FoldForm beside Files:** FoldForm in the phone layout on
    one side. For: website ("works in Split View").
13. **Photos, not screenshots:** the Duo half folded (laptop pose) with
    the project box on the upper half; and the Duo closed and open side by
    side, the same part on both. Another phone or camera, plain
    background. For: website, social, press.
14. **Recording (piece 4):** the fold and unfold film.
15. **And the log:** after 8 to 11, Files → On My iPhone → FoldForm →
    Logs → `openshape.log` (it also decides the 0.3.1 Duo fixes,
    PROJECT_STATUS.md).

### iPad Air 11" (landscape screenshots are 2360 x 1640: the 11" slot)

16. **The full layout:** *Project box*, Model panel open, landscape. For:
    the 11" slot, website.
17. **Apple Pencil sketching:** a sketch half drawn with the Pencil, pen
    mode on (the screenshot shows the strokes; the Pencil itself only in a
    photo). Landscape. For: listing v2 (iPad), social clip 5.
18. **A photo of the Pencil in use** on that sketch (over the shoulder,
    allowed outside the App Store). For: social, press.

### If there is a 3D printer

19. **A printed part made in FoldForm** (the project box or the key tag),
    photographed beside the phone showing the same model, daylight, plain
    background; and a short clip of an M3 screw going into a printed hole.
    For: website, social clip 2, press. Makers trust a real print more than
    any render.

## The first App Store screenshots (2026-10-31) and what marketing wants instead

**Made:** six iPhone 6.9" (1320 x 2868) and six iPad 13" (2752 x 2064)
screenshots plus the purchase's review screenshot, rendered from the real
app's scenes on Windows (`scripts/dev/appstore_screenshots.sh`): project
box with a typed diameter, key tag with text, a sketch, a construction
plane, the Model panel, Home with six projects. Plain UI, no captions,
Segoe UI type, no status bar (TD-95). They are accurate and accepted; **keep
them for 0.3.0** (changing them would only delay the submission).

**For 0.3.1 marketing wants different ones:**
- **Captions** on every screenshot (piece 3): plain UI does not say what
  the app is for in search results, where only the first three show.
- **Real-device UI** (San Francisco type, status bar) from the requests
  above, instead of the Windows rendering.
- **A new order:** typed sizes, holes that fit screws, sketch by finger,
  raised text, change any step, the Duo or the Pencil. Home with six
  projects (now #6) sells least; if it stays, its caption says "Unlimited
  projects with the in-app purchase".
- **The Duo set** (requests 8 to 11) for the "optimized for iPhone Duo"
  badge now, and before it becomes required in April 2027.
- **No share-sheet screenshot** in the listing (other apps' icons).

## Decisions for the owner

1. **Which pieces, in which order?** Recommended: 1 (preview), 3
   (captioned screenshots), 4 (Duo film), then 5, 6, 2, 7, 8.
2. **Sound in the preview:** silent (simplest, no license) or a licensed
   track (a royalty-free library with a written license for App Store
   use).
3. **Installs** (each a separate yes): Blender (piece 8; done, 2026-11-03), Apple Design
   Resources bezels (piece 5a), OBS Studio (only if recording the Windows
   app by hand is wanted; ffmpeg already does it by script).
4. **A 3D printer** for request 19?
5. **A website** beyond the README (GitHub Pages is a new service)?
