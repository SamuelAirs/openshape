# Scan: FoldForm's own photogrammetry

Sam's ask (2026-11-03): photogrammetry built by us (not Apple's Object Capture), very polished.
Scan turns 6 to 16 photos of an object standing on a sheet of A4, Letter or Legal paper into a closed
mesh in millimeters, and the mesh into a body. It all runs on the device, in plain C++
(`src/measure`, no Qt, no kernel), in about a second for twelve photos.

## Taking the photos

- Stand the object on a sheet of white A4, Letter or Legal paper (8.5 x 14 in, 215.9 x 355.6
  mm), on a table darker than the paper. A yellow or lined pad works, less reliably; tear the
  sheet off (a legal pad's glued top hides part of it, and Legal means the whole sheet).
  Lines or marks on the object help stereo; lines on the paper don't (see "Paper that is not
  plain white").
- Walk around it: 8 to 16 photos, the whole sheet in every photo, about 30 to 60 degrees above
  the table. **Add 3 to 5 photos from low down (10 to 20 degrees)**: only they see over a flat
  top (see "Limits").
- Even light; shadows are fine (they are recognised as paper), a white or grey object on white
  paper needs the subject mask (Apple Vision) to be told from the shadows.

## The pipeline (`measure/Scan.h`)

The stages are public; multi-view stereo sits between carving and the surface
(`ScanOptions::stereo`, on in the app's Scan, off in the library's default so the silhouette
tests measure the hull):

| Stage | Function | Gives |
|---|---|---|
| 1-3 | `calibrate(photos, options, progress)` | `ScanCalibration`: one `ScanView` per used photo (camera, downsampled RGB, three-state mask), per-photo reports, warnings |
| 4 | `carve(views, options, progress)` | `ScanGrid`: the visual hull as a signed field (> 0 inside) |
| 4b | `refineWithStereo(views, hull, stereoOptions, progress, stats)` (`measure/ScanStereo.h`) | the field with what the photos' texture shows carved away (never adds volume) |
| 5 | `extractSurface(grid, options, progress)` | `ScanSurface`: the closed mesh, volume, box, triangle count |
| all | `runScan(photos, options, progress)` | `ScanResult` with timings |

The data the stages exchange is in `measure/ScanTypes.h` (`ScanCamera`, `ScanView`, `ScanGrid`,
`ScanMesh`, `ScanProgress`). World frame: mm, the paper's center at the origin, x along its
long side, y along its short side, z up; the paper at z = 0. `ScanProgress` returns false to
cancel (the stage then fails with "Cancelled.").

1. **Cameras from the paper** (`ScanPose`). The principal point is the image center, pixels
   square. The focal length comes from EXIF (35 mm equivalent: f_px = f35 x diagonal / 43.27),
   or, when a photo has none, from all the photos' papers together: Zhang's two constraints
   (the paper's axes perpendicular and of equal scale after K^-1) as a 1-D search per photo,
   the median of the photos that look at a slant (straight-down photos say nothing), refined on
   the corners' reprojection error. The pose from H = K [r1 r2 t] (nearest rotation by the polar
   decomposition), refined by Levenberg-Marquardt on the four corners.
2. **Corner labelling**. The corners come from Vision or the handles in any order. Of the 8
   labellings, the mirrored ones put the camera under the table and the ones with the long side
   on the short side do not reproject: two are left, half a turn apart, which the paper cannot
   tell apart. A coarse grid (about 6 mm) is carved with every photo's candidates; a wrong
   candidate carves the object in the wrong place and the hull stops filling the other photos'
   outlines. The choice maximizes how much of every outline the hull fills (coordinate ascent
   from three starts: the smoothest camera path, the first candidates, greedy), with the camera
   path's continuity as a tie-break. A symmetric object on the paper's center is a toss-up, but
   then either choice gives the same shape. `labellingMargin` says how decisive it was.
3. **Masks** (`ScanMask`). Photos are shrunk to 1600 px first. On the paper, the paper's colour
   is a plane per channel fitted robustly to a band just inside the sheet's edge; a pixel of the
   paper's chromaticity at 35 % of its brightness or more is paper (shadows included), anything
   else object. Off the paper, and on the sheet's last 3 mm, only a subject mask decides
   (unknown without one). With a subject mask: it decides off the paper and where it says
   object (a white object's top looks like lit paper); where it says background, colour that is
   not the paper's is unknown (a part the mask missed, or a mark). An opening, a closing and small enclosed holes filled (highlights) clean it up.
4. **Carving**. A coarse pass (4 mm) over the sheet finds the object (the largest connected
   part), a fine pass (0.5 mm, at most 200 cells along its longest side) carves a box around it.
   A voxel goes when its center lands on background in one photo at least 2 px from anything
   else, or on background in two photos; it stays only if some photo sees object there. The
   largest connected part is kept and smoothed (Gaussian, 0.8 voxel; mirrored at the paper so
   the bottom stays flat).
5. **Surface** (`MeshTools`). The occupancy is made well-composed (no voxels touching only
   along an edge or at a corner), so Surface Nets gives a closed 2-manifold whatever the field
   was; vertices sit where the field crosses zero; the vertices on the grid's bottom are put on
   z = 0. Taubin smoothing (6 lambda/mu passes, the bottom keeps z = 0), then Garland-Heckbert
   edge collapse to 3000 triangles (link condition, no folded or sliver triangles, the bottom
   stays on its plane). Checked closed and outward before it is returned.

Warnings: fewer than 6 usable photos, photos left out (and why, in each photo's report), a gap
of more than 100 degrees in the cameras' azimuths, an object reaching the carving height, the
focal estimate resting on fewer than 3 photos.

## As a body

- `geom::solidFromMesh` (`geometry/MeshSolid.h`): checked in plain C++ (welds coincident
  vertices, drops collapsed triangles, refuses holes, edges in 3+ triangles, several pieces,
  zero-area triangles and self-crossing; turns flipped triangles and inside-out meshes), then one
  planar face per triangle on shared vertices and edges, a solid oriented outward, checked with
  BRepCheck, and exactly coplanar neighbours merged (`ShapeUpgrade_UnifySameDomain`: a scan's
  bottom becomes one face, an STL box six).
- `AppController::addMeshBody(vertices, triangles, name, source)`: the solid becomes an
  Imported body (as a STEP import does: the project keeps its BRep), one undo step, named
  "Scan 1", "Scan 2", ..., selected and framed.
- **File → Import STL…** (`AppController::importStl`, `geom::readStl`: binary and ASCII, at most
  50 000 triangles): each closed piece becomes a body named after the file.
- `AppController::addDemoScan` (and `--demo scan`) runs the whole pipeline on synthetic photos
  (`measure/ScanSynthetic`: the renderer the tests use) and adds the result.

## Real photos on the desktop

`measure::detectPaper` (`measure/PaperDetect.h`) finds the sheet without Vision: the largest
bright colourless (or pale yellow) region, closed over ruled lines, its convex hull (an object hiding part of the edge does not matter),
the best four-sided fit, each side refitted to the edge pixels. `tools/scan_photos`
(OPENSHAPE_BUILD_TOOLS) runs the stages on a folder of PPM photos and writes `scan.stl`, the
masks as PGMs and a line per photo; `scripts/dev/scan_photos.py <photos> <work>` (Windows
Python, Pillow; put `%USERPROFILE%\msys64\ucrt64\bin` on PATH) makes the PPMs from JPEGs with
the EXIF orientation applied and lists their 35 mm focal lengths in `photos.txt`, where
corners can be fixed by hand (eight numbers after the focal length).

## Paper that is not plain white

Sam, after build 63: "Scan an object kind of struggled to find the paper each time, but that
may have been my fault. I used a lined legal pad." Three things were against it: Scan knew
only A4 and Letter (a legal sheet is 1.65 : 1, A4 1.41 : 1: the corners fit neither, and the
focal estimate and the scale went wrong with them), the paper finder wanted bright pixels with
a saturation under 0.3 (a pad's yellow is about 0.4 to 0.5), and ruled lines cut the bright
region into strips.

- **Legal** is a third paper (`measure::legalReference`, the picker's third choice). Nothing in
  the engine assumes A4's proportions: the corners' labelling, the focal estimate and the
  carving height take the sheet's sides from the `Reference`.
- **Apple Vision** (`MeasureDevicesIos.mm`, `VNDetectRectanglesRequest`) looks first, as for
  A4: any aspect ratio from 0.3 to 1, so Legal goes through it unchanged. Seen side-on from
  low down, a sheet looks narrower than 0.3 (Legal from 20 degrees up: 0.61 x sin 20° = 0.21;
  A4 0.24) and Vision passes; FoldForm's own finder then takes over.
- **FoldForm's finder** (`measure::detectPaper`) also takes pale yellow as paper: blue the
  lowest channel, a hue of 38 to 75 degrees, saturation under 0.7 (wood, cardboard and
  oranges lie below 38 degrees; a deep yellow is over 0.7), above Otsu's brightness threshold
  like white. Then a closing (a 5 x 5 square at the 800 px working size) joins the strips
  between ruled lines and over a margin line before the largest region is taken.
- **Masks**: the paper's colour model is fitted to the sheet itself, so yellow is fine. A
  ruled line is a thin non-paper mark: by colour alone the opening drops it; with a subject
  mask it is "unknown" (never carves).
- **Measured** on synthetic photos (`ScanSynthetic`'s `paperColour`, `ruleSpacingMm`,
  `marginMm`): a yellow (0.98, 0.92, 0.52) legal sheet with blue lines 8.7 mm apart and a
  red margin 32 mm in, on wood (0.50, 0.33, 0.19), seen from four sides at 38 to 60
  degrees: the old finder found no sheet in any view; now every corner within 1.4 px (white
  Letter on a dark table, white A4 on a light beige one and white Legal within 1.5 px too).
  A 50 x 40 x 25 box on the pad, twelve photos, the lens unknown: the focal length within
  0.1 mm, the footprint within 2 mm, by subject masks and by colour alone alike (the hull's
  roof as on white paper). Through the app (`measurescan`): all twelve pads found by
  FoldForm's finder (corners within 1.8 px), built to within 2 mm of the same box on A4.

## Limits (what to tell Sam)

- **Silhouettes only see outlines.** A concavity open toward the cameras (a bowl's inside, the
  slot of a U-channel, the step of an L seen from above) is filled; from 30-60 degrees a flat
  top grows a low roof, because no outline passes above it; and a flat side that no photo
  sees edge-on bulges into a shallow wedge (its outline comes from photos looking at it at a
  slant: about (side / 2) x tan(angle to the nearest photo's direction)). Measured on the
  synthetic 40 x 30 x 20 box: 12 photos at 35-55 degrees, 11 mm too tall at the middle and
  +32 % volume; with 6 low photos added, 3 mm and +18 % (the bulges). A cylinder has no flat
  sides: +2 % with low photos. Multi-view stereo recovers these (next paragraph).
- **Multi-view stereo** (`ScanStereo`, on in the app): for each photo a depth map by PatchMatch
  with slanted planes, scored by ZNCC against its 2 to 4 best neighbours (the object downsampled
  to about 240 px across), filtered by texture, score, distinctness and agreement with two other
  depth maps, fused as a TSDF inside the hull (only carving). Measured on the synthetic 12-photo
  ring: the U-channel +6.7 % (hull +51 %), a textured box +5.2 % (hull +30.9 %), a tray's pocket
  floor within 0.1 mm; about 0.5 s at 4 threads. It needs texture: plain, shiny or very dark
  surfaces keep the hull, and holes the cameras cannot see into stay filled. Surfaces err a
  fraction of a millimeter outward. Only synthetic photos so far (no lens distortion modelled).
- Footprints: corners and round parts within about a millimeter (the paper is a good ruler);
  flat sides bulge out by a few millimeters at their middle, less with more photos.
- Without a subject mask a neutral grey or white object cannot be told from shadows on white
  paper; a dark or coloured object can. And without one the room around the paper is
  unknown, so low photos, which see the object's top against the room, do not lower the roof
  (the synthetic cylinder: +15 % by colour alone, +2 % with masks).
- The object must stand on the paper and fit within its carving height (the paper's short side
  by default, `ScanOptions::heightMm`).

## Tests

`tests/test_scan.cpp` renders synthetic photos (`measure/ScanSynthetic`: paper with uneven
light and a faint grain on a dark table, soft shadows, optional solid-noise texture, subject
masks, corners handed over from a shuffled start and direction) of a box, a cylinder, an
L-block and a U-channel, and checks cameras, the focal estimate, the labelling, masks, closed
meshes, footprints, volumes, warnings, progress and cancelling. `tests/test_mesh_solid.cpp`
(test_geometry) checks `solidFromMesh` and the STL reader; `tests/test_scan_body.cpp`
(test_interaction) a scan as a body through undo and a saved project; the `scan` acceptance
scenario clicks File → Import STL… and adds a demo scan.
