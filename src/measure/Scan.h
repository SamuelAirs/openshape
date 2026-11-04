// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "measure/Homography.h"
#include "measure/ScanMask.h"
#include "measure/ScanPose.h"
#include "measure/ScanTypes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Scan (docs/SCAN.md): FoldForm's own photogrammetry. The owner stands an
// object on a sheet of A4 or Letter paper and takes 6 to 16 photos walking
// around it; the paper calibrates every photo's camera, the object's outline
// in each photo carves a block of voxels down to its shape (the visual hull),
// and the result is a closed triangle mesh, in millimeters, ready to become
// a body.
//
// The stages are public so another stage can sit between them (multi-view
// stereo refines the carved field before the surface is made):
//   calibrate(photos)      -> cameras, downsampled images, three-state masks
//   carve(views)           -> the visual hull as a signed field (ScanGrid)
//   extractSurface(field)  -> the closed mesh (smoothed, flat bottom, decimated)
// runScan() runs all three. Everything is plain C++ (no Qt, no kernel), runs
// on the device, and takes about a second for twelve photos.
namespace os::measure {

// One photo as the app has it. Pixels and mask are borrowed for the call.
// Pixels, corners and mask must share one frame: the photo as shown (EXIF
// orientation applied), uncropped (the principal point is taken to be the
// image's center).
struct ScanPhoto {
    const std::uint8_t* pixels = nullptr;
    int width = 0;
    int height = 0;
    int stride = 0;        // bytes per row
    int bytesPerPixel = 4; // 3 (RGB) or 4 (RGBA / BGRA; the fourth byte is ignored)
    bool bgr = false;      // the bytes run B, G, R (Qt's ARGB32 on little-endian machines)
    // The paper's corners in pixels, in order around the sheet (either
    // direction, from any corner): Apple Vision's rectangle or the owner's handles.
    std::array<Vec2, 4> paperCorners{};
    // The lens's 35 mm-equivalent focal length (EXIF FocalLengthIn35mmFilm);
    // 0 when unknown (then estimated from all the photos' papers).
    double focal35mm = 0;
    // A subject mask (Apple Vision's foreground mask): 8-bit, 255 = subject,
    // any size (it is scaled to the photo). Optional.
    const std::uint8_t* subjectMask = nullptr;
    int maskWidth = 0;
    int maskHeight = 0;
    int maskStride = 0;
};

struct ScanOptions {
    Reference paper = a4Reference();
    int maxImageSide = 1600;     // photos are shrunk to this first
    double heightMm = 0;         // how high to carve; 0 = the paper's short side
    double coarseVoxelMm = 4;    // the first pass, over the whole sheet, finds the object
    double fineVoxelMm = 0.5;    // the second pass's voxels, at least ...
    int maxFineCells = 200;      // ... and at most this many along the object's longest side
    double boxMarginMm = 4;      // room around the object in the fine pass
    // A voxel is carved when its center lands on background in one photo at
    // least this far (pixels) from anything else, or on background in two photos.
    int carveMarginPx = 2;
    double fieldSigmaVoxels = 0.8; // the carved occupancy is smoothed by this Gaussian
    int taubinIterations = 6;
    int targetTriangles = 3000;
    MaskOptions masks;
};

// What became of each photo.
struct ScanPhotoReport {
    bool used = false;
    std::string problem;            // why it was left out (plain words), or a doubt about it
    double focalPx = 0;             // at the size the stages work at
    double focal35mm = 0;
    bool focalFromExif = false;
    double reprojectionErrorPx = 0; // the paper's corners, RMS, at full size
    int firstCorner = 0;            // which input corner became the paper's (-W/2, -H/2)
    bool reversed = false;          // whether the input corners run the other way round
    int labellings = 0;             // how many labellings fitted the paper (usually 2)
    double labellingMargin = 0;     // how much better the chosen one explains the others' outlines (0 = a toss-up)
    double azimuthDeg = 0;          // where the camera stood, seen from the paper's center
    double elevationDeg = 0;
    double distanceMm = 0;
    double objectFraction = 0;      // of the paper's pixels, those showing the object
    double silhouetteAgreement = 0; // of the photo's object pixels, those the carved shape covers (after carving)
};

struct ScanCalibration {
    std::vector<ScanView> views;          // the photos that are used, in input order
    std::vector<ScanPhotoReport> photos;  // one per input photo
    double focal35mm = 0;                 // the estimate when some photo had none
    bool focalEstimated = false;
    int focalPhotos = 0;                  // photos the estimate rests on
    std::vector<std::string> warnings;
};

struct ScanSurface {
    ScanMesh mesh;
    double volumeMm3 = 0;
    Vec3 boundsMin, boundsMax;
    int triangleCount = 0;
    std::vector<std::string> warnings;
};

// Step 1-3: cameras, labelling, masks. Fails (plain message) when fewer than
// three photos have a usable paper or the focal length cannot be worked out.
Result<ScanCalibration> calibrate(const std::vector<ScanPhoto>& photos, const ScanOptions& options = {},
                                  const ScanProgress& progress = {});

// Step 4: the visual hull as a signed field (> 0 inside), coarse to fine.
// Fails when nothing is left (no photo shows an object on the paper).
// Warnings: the object reaching the carving height.
Result<ScanGrid> carve(const std::vector<ScanView>& views, const ScanOptions& options = {},
                       const ScanProgress& progress = {});

// Step 5: the closed mesh of a field's inside, the bottom flat on z = 0.
Result<ScanSurface> extractSurface(const ScanGrid& field, const ScanOptions& options = {},
                                   const ScanProgress& progress = {});

// How much of each view's object pixels the field's inside covers when
// projected (0..1), one per view.
std::vector<double> silhouetteAgreement(const std::vector<ScanView>& views, const ScanGrid& field);

struct ScanResult {
    ScanSurface surface;
    ScanGrid hull;
    std::vector<ScanPhotoReport> photos;
    double focal35mm = 0;
    bool focalEstimated = false;
    std::vector<std::string> warnings; // calibration's, carving's and the surface's, deduplicated
    double calibrateMs = 0, carveMs = 0, surfaceMs = 0;
};

// All three stages.
Result<ScanResult> runScan(const std::vector<ScanPhoto>& photos, const ScanOptions& options = {},
                           const ScanProgress& progress = {});

} // namespace os::measure
