// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/ScanTypes.h"

#include <functional>

// Scan, the multi-view stereo stage: the silhouettes' visual hull fills every
// concavity the outline cannot show (a bowl's inside, a channel, a pocket);
// the photos' texture can. refineWithStereo carves those away.
//
// The method, a depth-map MVS in the manner of COLMAP / OpenMVS with the
// cameras given (they come from the calibration sheet):
//  1. View selection: for each photo, the 2-4 others that see the object's
//     center from 8-45 degrees away (the best near 20 degrees).
//  2. A depth map per photo (downsampled so the object spans at most
//     `maxObjectPixels`, never enlarged), only at pixels
//     inside its mask whose ray meets the hull: PatchMatch stereo with slanted
//     planes. Each pixel's depth is searched between the ray's entry into the
//     hull and its last exit; it starts at the hull's own surface (with the
//     hull's normal) or at random, then improves by red-black propagation
//     from neighbouring pixels' planes and random refinement. A plane is
//     scored by ZNCC over a sparse window (`windowRadius`, every
//     `windowStep` pixels), warping the window into each neighbour photo
//     through the plane's homography, aggregated as the mean of the best
//     `bestNeighbours` neighbours (occlusion-tolerant).
//  3. Filtering: drop pixels with too little texture (window spread), a poor
//     score, an ambiguous score (shifting the depth by about a pixel in the
//     neighbours hardly changes it: smooth shading, repeated texture), and
//     pixels whose 3D point fewer than `minConsistentViews` other depth maps
//     agree with (within a voxel or a pixel's footprint, the larger).
//  4. Fusion: a truncated signed distance (TSDF) of the filtered depth maps
//     on the hull's grid (the band a few voxels or pixels' footprints; weights
//     from the score and the viewing angle; at a depth edge, free space only
//     in front of the nearest depth around the lookup). Where enough views
//     observed a voxel, the result is min(hull, tsdf), so stereo only ever
//     carves; elsewhere the hull stands. Clean-up: the empty space is opened
//     (erode then dilate) and carved regions under `minCarvedComponent` voxels
//     get the hull back (speckle); solid left as a thin film next to carved
//     space is carved too (its surface would fold through itself once
//     smoothed); and when nothing is carved the hull is returned exactly.
//
// Conventions shared with the silhouette stage: pixel (0, 0) is the top-left
// pixel's top-left corner (its center is (0.5, 0.5)); the grid's sample
// (i, j, k) sits at origin + (i + 0.5, j + 0.5, k + 0.5) * voxel. The hull's
// field may be any signed field (an occupancy blurred and shifted by 0.5, or
// a signed distance): the TSDF is brought to its scale (its slope across the
// surface and its range) before the two are combined.
namespace os::measure {

struct StereoOptions {
    int threads = 0;              // 0: half the hardware threads (at least 1)
    int maxObjectPixels = 240;    // photos are downsampled so the hull's box spans at most this many pixels
    int maxImageWidth = 1600;     // and to at most this width
    int neighbours = 4;           // neighbour photos per reference photo (2..4 used)
    double minBaselineDeg = 8;    // angle between the viewing rays at the object's center
    double maxBaselineDeg = 45;
    double bestBaselineDeg = 20;
    int windowRadius = 4;         // the ZNCC window: (2r+1)^2 pixels, sampled every windowStep
    int windowStep = 2;
    int bestNeighbours = 2;       // the score: mean of the best k neighbours' costs
    int iterations = 3;           // PatchMatch sweeps (each updates every pixel once)
    double minTextureStd = 3.0;   // grey levels (0..255): flatter windows get no depth
    double maxCost = 0.5;         // 1 - ZNCC, aggregated: worse pixels are dropped
    double minDistinctness = 0.04; // cost rise when the depth moves ~1.5 px in the neighbours
    int minConsistentViews = 2;   // other depth maps that must agree with a pixel's point
    double consistencyVoxels = 1.0; // their depth within this many voxels or pixels' footprints (the larger)
    double consistencyPixels = 1.0;
    double truncationVoxels = 3.0;  // the TSDF's truncation band: voxels or pixels' footprints (the larger)
    double truncationPixels = 3.0;
    int minFusionViews = 2;       // views that must observe a voxel before stereo may carve it
    double minFusionWeight = 0.8; // and their summed weight
    // Solid left thinner than about 2 * thinVoxels voxels next to carved space
    // (a skin over a carved pocket, a sliver between carved voxels) is carved
    // too: the surface of such a film folds through itself once smoothed.
    int thinVoxels = 2;
    // Carved regions smaller than this (voxels, 6-connected) get the hull back.
    int minCarvedComponent = 64;
};

// What the stage did (for logs and tests).
struct StereoStats {
    int viewsUsed = 0;              // photos with a depth map
    long long depthPixels = 0;      // pixels searched, all views
    long long confidentPixels = 0;  // after the texture / score / distinctness checks
    long long consistentPixels = 0; // after the geometric check (the ones fused)
    long long carvedVoxels = 0;     // hull voxels (inside) the result puts outside
    long long restoredVoxels = 0;   // carved voxels the clean-up gave back to the hull
    long long thinVoxels = 0;       // thin solid next to carved space, carved as well
    double secondsDepth = 0, secondsFilter = 0, secondsFusion = 0, secondsTotal = 0;
    bool cancelled = false;
};

// The hull refined by the photos: the same grid, a field <= the hull's
// everywhere (it never adds volume). `progress` is called regularly from the
// calling thread with the fraction done; returning false cancels, and the hull
// is returned unchanged. With fewer than two usable views (rgb of the
// camera's size) the hull is returned unchanged.
ScanGrid refineWithStereo(const std::vector<ScanView>& views, const ScanGrid& hull, const StereoOptions& options,
                          const std::function<bool(double fraction)>& progress, StereoStats* stats = nullptr);

} // namespace os::measure
