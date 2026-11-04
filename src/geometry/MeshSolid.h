// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <array>
#include <filesystem>
#include <string>
#include <vector>

// Triangle meshes as solids: a scan (src/measure/Scan) or an STL file becomes
// a body whose faces are the mesh's triangles (flat, exact). Meshes are
// checked first, in plain C++: closed, one piece, consistently oriented (a
// flipped triangle is turned round), no triangle crossing another.
namespace os::geom {

struct MeshSolidOptions {
    // Merge neighbouring triangles lying in one plane into one face (a scan's
    // flat bottom, an STL box's sides), exactly coplanar only.
    bool mergeCoplanar = true;
    // Larger meshes are refused (a body of more faces gets slow to work with).
    std::size_t maxTriangles = 50000;
};

// What the checks found (also on success: the repairs made).
struct MeshDiagnosis {
    int weldedVertices = 0;       // vertices at the same place, joined
    int droppedTriangles = 0;     // triangles with a repeated corner, dropped
    int flippedTriangles = 0;     // turned round to match their neighbours
    int boundaryEdges = 0;        // edges in one triangle (a hole)
    int nonManifoldEdges = 0;     // edges in three or more triangles
    int pieces = 0;               // connected pieces
    int crossingPairs = 0;        // triangles crossing others (a found pair stops the search)
    bool insideOut = false;       // the whole mesh faced inward (turned)
};

// A closed solid from a triangle mesh, millimeters. Refused, with a plain
// message, when the mesh is empty, has holes or edges shared by more than two
// triangles, is several pieces, crosses itself, has flat (zero-area)
// triangles, or has more than `maxTriangles` triangles.
Result<Shape> solidFromMesh(const std::vector<Vec3>& vertices, const std::vector<std::array<int, 3>>& triangles,
                            const MeshSolidOptions& options = {}, MeshDiagnosis* diagnosis = nullptr);

// A mesh read from a file.
struct TriangleMesh {
    std::vector<Vec3> vertices;
    std::vector<std::array<int, 3>> triangles;
};

// Reads a binary or ASCII STL file (vertices shared where they coincide;
// millimeters assumed, as STL carries no unit). Refuses files over
// `maxTriangles` triangles, damaged or truncated files, and non-finite
// coordinates, with plain messages.
Result<TriangleMesh> readStl(const std::filesystem::path& path, std::size_t maxTriangles = 50000);

// The mesh's connected pieces (triangles sharing edges), each its own mesh.
std::vector<TriangleMesh> meshPieces(const TriangleMesh& mesh);

} // namespace os::geom
