# FoldForm: handover (2026-11-05)

For whoever continues. Read `CLAUDE.md` first (the lead-engineer rules: GitHub only compiles,
run the local suites before every push, the real-UI suite moves the pointer on this PC for
about 15 minutes), then `PROJECT_STATUS.md`, then `docs/LAUNCH_CHECKLIST.md` (the owner's list)
and `docs/APP_STORE.md` (every listing text, marked in blocks the listing tool reads), then
this. The owner is Sam; he tests on an iPhone 16 Pro, an iPhone Duo and an iPad.

## Where it stands

- **App Store:** version 0.3.0, build 54, is **Waiting for Review** since 2026-11-04, with the
  in-app purchase Unlimited Projects (`io.github.samuelairs.openshape.unlimited`, USD 9.99) in
  the same submission; release is manual. Free in 147 of 175 territories: the 27 EU countries
  and China mainland are off on purpose (Sam is not a trader under the EU Digital Services Act;
  China needs an ICP filing). "Make available in new territories" is on. The listing texts,
  twelve screenshots (6.9-inch iPhone and 13-inch iPad) and the review contact were written by
  a tool from `docs/APP_STORE.md` and read back; nothing on a version in review can change.
- **TestFlight:** build 55 is 0.3.1 (iOS 17 and later), from main `e6576ff`: the paywall leak
  closed (projects in subfolders count), several faces selected at once with a long press
  selecting all of a text, push-to-subtract (faces pushed into another body cut it, one undo
  step), the pinch jitter fixed (the view no longer reveals the selection mid-gesture), the
  value box docked at an edge on the Duo. Sam has not tested 55 yet.
- **Docs and marketing on main:** the independent critique (`docs/CRITIQUE_2026-11-03.md`), the
  marketing plan (`docs/MARKETING_RENDERS.md`, with two recording briefs for Sam), the website
  hero still and the turning-part loop (`docs/marketing/`, rebuilt by
  `scripts/dev/marketing_hero.py`), the captioned App Store screenshot set for 0.3.1
  (`docs/appstore/captioned/`, rebuilt by `scripts/dev/appstore_captions.py`).

## Open, in Sam's words and the leads' findings

- Sam's video on the Duo (2026-11-03, frames in `docs/feedback/2026-11-03-duo-select/`): "Fold
  form is a bit clunky"; he could not select several faces, and wanted to "select all the faces
  of the text then drag the faces towards the object in order to subtract". Built in 0.3.1,
  untested on his Duo. "I tried to zoom in and it was very jittery": fixed in 0.3.1, untested.
- Still owed by Sam: six Duo screenshots on build 54 (`docs/appstore/duo/`, names in
  `APP_STORE.md` section 3b; they go up with 0.3.1, optional, they earn the "optimized for
  iPhone Duo" badge); his word that the description's Duo line matches what he sees; Restore
  purchase on a second device; `Logs/openshape.log` from the Duo (the real safe areas, TD-150).
- Technical debt worth knowing: TD-150 (Duo safe areas are estimates), TD-155 (resolved, the
  paywall leak), TD-156 (a cut into another body does not follow the pushed body if it moves
  later), TD-157 (the value box docks top or bottom, not along the Duo's side bar; the 720-pt
  threshold is a guess). `CLAUDE.md` still says the real-UI run takes about 30 s; it takes about
  15 minutes.
- The critique's remaining ideas, ranked: a guided first part with six sample projects; a
  half-folded "workbench" layout on the Duo (model above the crease, keypad below); icons and a
  denser phone tool strip; STL and 3MF import for remixing. Nothing of it is started.

## Rules that stand

- One tag per App Store version; App Store Connect takes no more builds of a tagged version.
  Main is 0.3.1; the next tag is `v0.3.1` when Sam says.
- Every push of main uploads a TestFlight build (ipad.yml), so push deliberately. Never push
  `[testflight]`-style markers here; the workflow needs none.
- Run the headless suite (855) and the real-UI suite (4,356 checks, the pointer moves) before a
  push; never weaken a test to pass.
- The written source offer in the Licenses screen gives ayers177@gmail.com from 0.3.1 (two
  tests that treated "@" as a placeholder were changed for it).
- No Pencil claims in the listing until Apple enables the Pencil on the Duo; no "Made for
  iPhone Duo" wording (that is Apple's badge).
