// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "measure/ScanTypes.h"

#include <vector>

// Scan, step 5 (docs/SCAN.md): from a signed field on a voxel grid to a
// closed triangle mesh small enough to become a body: the surface (Surface
// Nets on a cleaned-up occupancy, so it is always a closed 2-manifold), a
// flat bottom on the paper, Taubin smoothing (no shrinking), and quadric edge
// collapse down to a triangle budget (manifold and flip checks: it stays
// closed). Plain C++; no kernel.
namespace os::measure {

// The field's inside (field > 0) as a closed, outward-oriented mesh. The
// grid's outside border closes it; with `flatBottom` the vertices on the
// grid's bottom face lie exactly on z = origin.z (the paper). `bottom`
// (optional) gets, per vertex, whether it is on that bottom.
ScanMesh surfaceNets(const ScanGrid& grid, bool flatBottom, std::vector<char>* bottom = nullptr);

// Taubin's lambda/mu smoothing (umbrella weights). Vertices marked in
// `pinnedZ` keep their z (the flat bottom stays flat).
void taubinSmooth(ScanMesh& mesh, int iterations, const std::vector<char>* pinnedZ = nullptr, double lambda = 0.5,
                  double mu = -0.53);

// Garland-Heckbert edge collapse down to at most `targetTriangles` (when
// the mesh allows: collapses that would make it non-manifold or fold a
// triangle over are skipped). Vertices marked in `pinnedZ` stay on their
// plane z (a collapse onto them keeps their place); the flags follow the
// vertices' new numbering. A collapse may turn no triangle's normal by more
// than acos(minNormalCos).
void decimate(ScanMesh& mesh, int targetTriangles, std::vector<char>* pinnedZ = nullptr, double minNormalCos = 0.2);

// Pairs of non-neighbouring triangles that cross each other, counted up to
// `limit` (0 for a clean surface). The kernel refuses meshes that cross
// themselves (geom::solidFromMesh makes the same check).
int selfIntersections(const ScanMesh& mesh, int limit = 1);

struct MeshReport {
    int vertices = 0;
    int triangles = 0;
    int boundaryEdges = 0;    // in one triangle
    int nonManifoldEdges = 0; // in three or more
    int misorientedEdges = 0; // two triangles running the same way along it
    int degenerateTriangles = 0;
    double volume = 0;        // mm³, positive for an outward mesh
    double area = 0;          // mm²
    Vec3 min, max;
    bool closed() const { return triangles > 0 && boundaryEdges == 0 && nonManifoldEdges == 0; }
    bool oriented() const { return closed() && misorientedEdges == 0; }
};
MeshReport inspectMesh(const ScanMesh& mesh);

// Drops unused vertices (indices are renumbered).
void compactMesh(ScanMesh& mesh, std::vector<char>* flags = nullptr);

} // namespace os::measure
