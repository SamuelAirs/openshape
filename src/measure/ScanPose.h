// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/Homography.h"
#include "measure/ScanTypes.h"

#include <array>
#include <optional>
#include <vector>

// Scan, step 1 (docs/SCAN.md): each photo's camera from the sheet of paper
// the object stands on. The paper's four corners fix a homography between
// the table and the photo; with the camera's focal length (from the photo's
// EXIF, or estimated from all the photos' papers together) it gives the
// camera's rotation and position in the paper's frame.
namespace os::measure {

// A photo's paper corners, in image pixels, in order around the sheet
// (either direction, from any corner), and the image's size.
struct PaperCorners {
    std::array<Vec2, 4> corners{};
    int width = 0;
    int height = 0;
};

// The focal length in pixels from the 35 mm equivalent (EXIF
// FocalLengthIn35mmFilm): the 35 mm frame's diagonal is 43.27 mm.
double focalPxFrom35mm(double focal35mm, int width, int height);
double focal35mmFromPx(double focalPx, int width, int height);

// One way of labelling the corners: which input corner is the paper's
// (-W/2, -H/2) corner, and whether the input runs the other way round. The
// camera this labelling gives and how well it reprojects the corners.
struct PaperPose {
    int firstCorner = 0;
    bool reversed = false;
    ScanCamera camera;
    double reprojectionErrorPx = 0; // RMS over the four corners
};

// The paper's corners in the world (the frame of ScanTypes.h), in the order
// the labellings use: (-W/2,-H/2), (W/2,-H/2), (W/2,H/2), (-W/2,H/2).
std::array<Vec3, 4> paperWorldCorners(const Reference& paper);

// The camera for one labelling and focal length (pixels), refined to fit the
// four corners (rotation and position; the focal stays). nullopt when the
// labelling puts the camera below the paper or the corners are degenerate.
std::optional<PaperPose> poseForLabelling(const PaperCorners& photo, const Reference& paper, double focalPx,
                                          int firstCorner, bool reversed);

// The labellings that fit the photo with this focal length, best first:
// those whose reprojection error is close to the best one's. Usually two,
// half a turn apart (the paper alone cannot tell them apart); four for a
// square-ish reference; empty when no labelling gives a camera above the
// paper.
std::vector<PaperPose> plausiblePoses(const PaperCorners& photo, const Reference& paper, double focalPx);

// The focal length (35 mm equivalent) that best explains all the photos'
// papers as rectangles of the reference's proportions seen by one camera
// (Zhang's two constraints per photo with only the focal unknown: a 1-D
// search, then refined on the corners' reprojection error). Photos looking
// almost straight down say little about it and are left out. nullopt when no
// photo is oblique enough. `usedPhotos` (optional) gets how many photos
// constrained it.
std::optional<double> estimateFocal35mm(const std::vector<PaperCorners>& photos, const Reference& paper,
                                        int* usedPhotos = nullptr);

// The camera's direction from the paper's center: azimuth (degrees, from +x
// toward +y) and elevation above the table (degrees), and its distance (mm).
struct CameraPlacement {
    double azimuthDeg = 0;
    double elevationDeg = 0;
    double distanceMm = 0;
};
CameraPlacement cameraPlacement(const ScanCamera& camera);

// The same camera with the world turned half a turn about z (the other
// labelling of a pair): the paper looks the same, the object does not.
ScanCamera turnedHalf(const ScanCamera& camera);

} // namespace os::measure
