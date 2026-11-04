// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "geometry/MeshSolid.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "geometry/Exchange.h"
#include "geometry/internal/KernelUtil.h"
#include "geometry/internal/ShapeData.h"

#include <BRepBuilderAPI_MakeEdge.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepLib.hxx>
#include <BRep_Builder.hxx>
#include <Precision.hxx>
#include <ShapeUpgrade_UnifySameDomain.hxx>
#include <Standard_Failure.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shell.hxx>
#include <TopoDS_Solid.hxx>
#include <TopoDS_Vertex.hxx>
#include <TopoDS_Wire.hxx>
#include <gp_Pln.hxx>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <locale>
#include <sstream>
#include <unordered_map>

namespace os::geom {

namespace {

std::uint64_t edgeKey(int a, int b)
{
    const std::uint32_t lo = std::uint32_t(std::min(a, b)), hi = std::uint32_t(std::max(a, b));
    return (std::uint64_t(lo) << 32) | hi;
}

struct TripleHash {
    std::size_t operator()(const std::array<std::int64_t, 3>& k) const
    {
        std::uint64_t h = 1469598103934665603ull;
        for (std::int64_t v : k) {
            h ^= std::uint64_t(v);
            h *= 1099511628211ull;
        }
        return std::size_t(h);
    }
};

// Whether segment pq crosses triangle abc's interior (strictly: touching at
// a shared vertex or edge does not count).
bool segmentCrossesTriangle(const Vec3& p, const Vec3& q, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 d = q - p;
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 h = d.cross(e2);
    const double det = e1.dot(h);
    const double scale = e1.length() * e2.length() * d.length();
    if (!(scale > 0) || std::abs(det) < 1e-10 * scale)
        return false; // parallel (coplanar overlaps are not looked for)
    const double inv = 1.0 / det;
    const Vec3 s = p - a;
    const double u = s.dot(h) * inv;
    constexpr double eps = 1e-7;
    if (u < eps || u > 1 - eps)
        return false;
    const Vec3 qv = s.cross(e1);
    const double v = d.dot(qv) * inv;
    if (v < eps || u + v > 1 - eps)
        return false;
    const double t = e2.dot(qv) * inv;
    return t > eps && t < 1 - eps;
}

// Pairs of non-neighbouring triangles crossing each other (stops at `limit`).
int crossingPairs(const std::vector<Vec3>& v, const std::vector<std::array<int, 3>>& tris, int limit)
{
    if (tris.size() < 4)
        return 0;
    Vec3 lo = v[tris[0][0]], hi = lo;
    double edgeSum = 0;
    for (const auto& t : tris)
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = v[t[k]];
            lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
            hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
            edgeSum += (v[t[(k + 1) % 3]] - p).length();
        }
    const double cell = std::max(1e-9, 2.0 * edgeSum / (3.0 * tris.size()));
    const Vec3 size = hi - lo;
    const auto cellsAlong = [&](double s) { return std::clamp(int(s / cell) + 1, 1, 256); };
    const int nx = cellsAlong(size.x), ny = cellsAlong(size.y), nz = cellsAlong(size.z);
    const double cx = size.x / nx + 1e-12, cy = size.y / ny + 1e-12, cz = size.z / nz + 1e-12;
    std::vector<std::vector<int>> cells(std::size_t(nx) * ny * nz);
    for (int t = 0; t < int(tris.size()); ++t) {
        Vec3 a = v[tris[t][0]], b = a;
        for (int k = 1; k < 3; ++k) {
            const Vec3& p = v[tris[t][k]];
            a = {std::min(a.x, p.x), std::min(a.y, p.y), std::min(a.z, p.z)};
            b = {std::max(b.x, p.x), std::max(b.y, p.y), std::max(b.z, p.z)};
        }
        const int x0 = std::clamp(int((a.x - lo.x) / cx), 0, nx - 1), x1 = std::clamp(int((b.x - lo.x) / cx), 0, nx - 1);
        const int y0 = std::clamp(int((a.y - lo.y) / cy), 0, ny - 1), y1 = std::clamp(int((b.y - lo.y) / cy), 0, ny - 1);
        const int z0 = std::clamp(int((a.z - lo.z) / cz), 0, nz - 1), z1 = std::clamp(int((b.z - lo.z) / cz), 0, nz - 1);
        for (int z = z0; z <= z1; ++z)
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x)
                    cells[(std::size_t(z) * ny + y) * nx + x].push_back(t);
    }
    int found = 0;
    for (const auto& list : cells)
        for (std::size_t i = 0; i < list.size(); ++i)
            for (std::size_t j = i + 1; j < list.size(); ++j) {
                const auto& A = tris[list[i]];
                const auto& B = tris[list[j]];
                int shared = 0, sharedA = -1, sharedB = -1;
                for (int a = 0; a < 3; ++a)
                    for (int b = 0; b < 3; ++b)
                        if (A[a] == B[b]) {
                            ++shared;
                            sharedA = a;
                            sharedB = b;
                        }
                if (shared >= 2)
                    continue; // neighbours across an edge
                bool cross = false;
                if (shared == 1) {
                    // Only the edges away from the shared corner can cross.
                    cross = segmentCrossesTriangle(v[A[(sharedA + 1) % 3]], v[A[(sharedA + 2) % 3]], v[B[0]], v[B[1]], v[B[2]])
                            || segmentCrossesTriangle(v[B[(sharedB + 1) % 3]], v[B[(sharedB + 2) % 3]], v[A[0]], v[A[1]],
                                                      v[A[2]]);
                } else {
                    for (int k = 0; k < 3 && !cross; ++k)
                        cross = segmentCrossesTriangle(v[A[k]], v[A[(k + 1) % 3]], v[B[0]], v[B[1]], v[B[2]])
                                || segmentCrossesTriangle(v[B[k]], v[B[(k + 1) % 3]], v[A[0]], v[A[1]], v[A[2]]);
                }
                if (cross && ++found >= limit)
                    return found;
            }
    return found;
}

std::string plural(int n, const char* one, const char* many) { return std::to_string(n) + " " + (n == 1 ? one : many); }

} // namespace

Result<Shape> solidFromMesh(const std::vector<Vec3>& vertices, const std::vector<std::array<int, 3>>& triangles,
                            const MeshSolidOptions& options, MeshDiagnosis* diagnosisOut)
{
    using R = Result<Shape>;
    ScopedTimer timer("solidFromMesh (" + std::to_string(triangles.size()) + " triangles)");
    MeshDiagnosis diag;
    const auto finish = [&](R r) {
        if (diagnosisOut)
            *diagnosisOut = diag;
        return r;
    };
    if (triangles.size() < 4 || vertices.size() < 4)
        return finish(R::failure(ErrorCode::InvalidArgument, "The mesh is empty.", "solidFromMesh: too few triangles"));
    if (triangles.size() > options.maxTriangles)
        return finish(R::failure(ErrorCode::Unsupported,
                                 "The mesh has " + std::to_string(triangles.size()) + " triangles, more than the "
                                     + std::to_string(options.maxTriangles) + " a body can have.",
                                 "solidFromMesh: too many triangles"));
    Vec3 lo = vertices[0], hi = lo;
    for (const Vec3& p : vertices) {
        if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z))
            return finish(R::failure(ErrorCode::InvalidArgument, "The mesh has a point at an invalid position.",
                                     "solidFromMesh: non-finite vertex"));
        lo = {std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z)};
        hi = {std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z)};
    }
    const double diagonal = (hi - lo).length();
    if (!(diagonal > 1e-3))
        return finish(R::failure(ErrorCode::InvalidArgument, "The mesh is too small to be a body.", "solidFromMesh: tiny"));

    // Join vertices at the same place.
    const double q = std::max(1e-9, 1e-9 * diagonal);
    std::unordered_map<std::array<std::int64_t, 3>, int, TripleHash> where;
    std::vector<int> remap(vertices.size());
    std::vector<Vec3> pts;
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        const Vec3& p = vertices[i];
        const std::array<std::int64_t, 3> key{std::llround(p.x / q), std::llround(p.y / q), std::llround(p.z / q)};
        const auto [it, inserted] = where.emplace(key, int(pts.size()));
        if (inserted)
            pts.push_back(p);
        remap[i] = it->second;
    }
    std::vector<std::array<int, 3>> tris;
    tris.reserve(triangles.size());
    for (const auto& t : triangles) {
        for (int k : t)
            if (k < 0 || std::size_t(k) >= vertices.size())
                return finish(R::failure(ErrorCode::InvalidArgument, "The mesh refers to a point it does not have.",
                                         "solidFromMesh: index out of range"));
        const std::array<int, 3> w{remap[t[0]], remap[t[1]], remap[t[2]]};
        if (w[0] == w[1] || w[1] == w[2] || w[0] == w[2]) {
            ++diag.droppedTriangles;
            continue;
        }
        const double area2 = (pts[w[1]] - pts[w[0]]).cross(pts[w[2]] - pts[w[0]]).length();
        if (!(area2 > 1e-12 * diagonal * diagonal))
            return finish(R::failure(ErrorCode::InvalidArgument, "The mesh has flat (zero-area) triangles.",
                                     "solidFromMesh: zero-area triangle"));
        tris.push_back(w);
    }
    diag.weldedVertices = int(vertices.size() - pts.size());

    // Each edge in exactly two triangles.
    std::unordered_map<std::uint64_t, std::vector<int>> edgeTris;
    edgeTris.reserve(tris.size() * 2);
    for (int t = 0; t < int(tris.size()); ++t)
        for (int k = 0; k < 3; ++k)
            edgeTris[edgeKey(tris[t][k], tris[t][(k + 1) % 3])].push_back(t);
    for (const auto& [key, list] : edgeTris) {
        (void)key;
        if (list.size() == 1)
            ++diag.boundaryEdges;
        else if (list.size() > 2)
            ++diag.nonManifoldEdges;
    }
    if (diag.boundaryEdges > 0)
        return finish(R::failure(ErrorCode::InvalidArgument,
                                 "The mesh is not closed (it has " + plural(diag.boundaryEdges, "open edge", "open edges")
                                     + "): only a closed mesh can become a body.",
                                 "solidFromMesh: boundary edges"));
    if (diag.nonManifoldEdges > 0)
        return finish(R::failure(ErrorCode::InvalidArgument,
                                 "The mesh has " + plural(diag.nonManifoldEdges, "edge", "edges")
                                     + " shared by more than two triangles: it is not a clean closed surface.",
                                 "solidFromMesh: non-manifold edges"));

    // One consistent orientation per piece (flipping triangles that disagree).
    std::vector<int> orient(tris.size(), 0); // 0 unknown, 1 keep, -1 flip
    std::vector<int> stack;
    for (int seed = 0; seed < int(tris.size()); ++seed) {
        if (orient[seed] != 0)
            continue;
        ++diag.pieces;
        orient[seed] = 1;
        stack.push_back(seed);
        while (!stack.empty()) {
            const int t = stack.back();
            stack.pop_back();
            for (int k = 0; k < 3; ++k) {
                const int a = tris[t][k], b = tris[t][(k + 1) % 3];
                for (int u : edgeTris[edgeKey(a, b)]) {
                    if (u == t)
                        continue;
                    // Does u run along the edge the same way (a to b)?
                    bool same = false;
                    for (int m = 0; m < 3; ++m)
                        if (tris[u][m] == a && tris[u][(m + 1) % 3] == b)
                            same = true;
                    const int want = same ? -orient[t] : orient[t];
                    if (orient[u] == 0) {
                        orient[u] = want;
                        stack.push_back(u);
                    } else if (orient[u] != want) {
                        return finish(R::failure(ErrorCode::InvalidArgument,
                                                 "The mesh's surface has no inside and outside (like a Moebius strip).",
                                                 "solidFromMesh: non-orientable"));
                    }
                }
            }
        }
    }
    if (diag.pieces > 1)
        return finish(R::failure(ErrorCode::InvalidArgument,
                                 "The mesh is " + std::to_string(diag.pieces)
                                     + " separate pieces: only one closed piece can become a body.",
                                 "solidFromMesh: several pieces"));
    for (std::size_t t = 0; t < tris.size(); ++t)
        if (orient[t] < 0) {
            std::swap(tris[t][1], tris[t][2]);
            ++diag.flippedTriangles;
        }
    double volume = 0;
    for (const auto& t : tris)
        volume += pts[t[0]].dot(pts[t[1]].cross(pts[t[2]])) / 6.0;
    if (volume < 0) {
        diag.insideOut = true;
        for (auto& t : tris)
            std::swap(t[1], t[2]);
        volume = -volume;
    }
    diag.crossingPairs = crossingPairs(pts, tris, 1);
    if (diag.crossingPairs > 0)
        return finish(R::failure(ErrorCode::InvalidArgument, "The mesh crosses itself: it cannot become a solid body.",
                                 "solidFromMesh: self-intersecting"));

    // The solid: shared vertices and edges, one planar face per triangle.
    return finish(detail::guarded("solidFromMesh", "The mesh could not be made into a solid.", [&]() -> R {
        BRep_Builder builder;
        std::vector<TopoDS_Vertex> vs(pts.size());
        for (std::size_t i = 0; i < pts.size(); ++i)
            builder.MakeVertex(vs[i], detail::toPnt(pts[i]), Precision::Confusion());
        std::unordered_map<std::uint64_t, TopoDS_Edge> edges;
        edges.reserve(tris.size() * 2);
        const auto edgeOf = [&](int a, int b) -> TopoDS_Edge {
            const std::uint64_t key = edgeKey(a, b);
            auto it = edges.find(key);
            if (it == edges.end()) {
                const int lo2 = std::min(a, b), hi2 = std::max(a, b);
                BRepBuilderAPI_MakeEdge make(vs[lo2], vs[hi2]);
                if (!make.IsDone())
                    return TopoDS_Edge();
                it = edges.emplace(key, make.Edge()).first;
            }
            return a < b ? it->second : TopoDS::Edge(it->second.Reversed());
        };
        TopoDS_Shell shell;
        builder.MakeShell(shell);
        for (const auto& t : tris) {
            TopoDS_Wire wire;
            builder.MakeWire(wire);
            for (int k = 0; k < 3; ++k) {
                const TopoDS_Edge e = edgeOf(t[k], t[(k + 1) % 3]);
                if (e.IsNull())
                    return R::failure(ErrorCode::KernelFailure, "The mesh could not be made into a solid.",
                                      "solidFromMesh: edge failed");
                builder.Add(wire, e);
            }
            wire.Closed(Standard_True);
            const Vec3 n = (pts[t[1]] - pts[t[0]]).cross(pts[t[2]] - pts[t[0]]).normalized();
            BRepBuilderAPI_MakeFace face(gp_Pln(detail::toPnt(pts[t[0]]), gp_Dir(n.x, n.y, n.z)), wire, Standard_True);
            if (!face.IsDone())
                return R::failure(ErrorCode::KernelFailure, "The mesh could not be made into a solid.",
                                  "solidFromMesh: face failed");
            builder.Add(shell, face.Face());
        }
        shell.Closed(Standard_True);
        TopoDS_Solid solid;
        builder.MakeSolid(solid);
        builder.Add(solid, shell);
        BRepLib::OrientClosedSolid(solid);
        TopoDS_Shape result = solid;
        if (!detail::validGeometry(result))
            return R::failure(ErrorCode::InvalidResultShape, "The mesh could not be made into a valid solid.",
                              "solidFromMesh: BRepCheck failed on the sewn solid");
        const double built = detail::exactVolume(result);
        if (!(built > 0) || std::abs(built - volume) > 1e-6 * volume + 1e-9)
            return R::failure(ErrorCode::InvalidResultShape, "The mesh could not be made into a valid solid.",
                              "solidFromMesh: volume " + std::to_string(built) + " instead of " + std::to_string(volume));
        if (options.mergeCoplanar) {
            try {
                ShapeUpgrade_UnifySameDomain unify(result, Standard_True, Standard_True, Standard_False);
                unify.SetLinearTolerance(Precision::Confusion());
                unify.SetAngularTolerance(Precision::Angular());
                unify.Build();
                const TopoDS_Shape merged = unify.Shape();
                if (!merged.IsNull() && detail::validGeometry(merged)
                    && std::abs(detail::exactVolume(merged) - built) <= 1e-6 * built)
                    result = merged;
                else
                    OS_LOG(Warning, Kernel) << "solidFromMesh: merging coplanar faces gave an invalid solid; kept the triangles";
            } catch (const Standard_Failure& failure) {
                OS_LOG(Warning, Kernel) << "solidFromMesh: merging faces threw " << detail::describeFailure(failure);
            }
        }
        OS_LOG(Info, Kernel) << "solidFromMesh: " << tris.size() << " triangles, volume " << built << " mm3, "
                             << timer.elapsedMs() << " ms";
        return R::success(makeShape(result));
    }));
}

// ---------------------------------------------------------------------- STL

namespace {

struct Welder {
    std::unordered_map<std::array<std::int64_t, 3>, int, TripleHash> where;
    TriangleMesh mesh;
    int add(double x, double y, double z)
    {
        // STL repeats each corner exactly (as 32-bit floats): join equal ones.
        const auto bits = [](double v) {
            const float f = float(v);
            std::uint32_t u;
            std::memcpy(&u, &f, 4);
            return std::int64_t(f == 0.0f ? 0u : u);
        };
        const std::array<std::int64_t, 3> key{bits(x), bits(y), bits(z)};
        const auto [it, inserted] = where.emplace(key, int(mesh.vertices.size()));
        if (inserted)
            mesh.vertices.push_back({double(float(x)), double(float(y)), double(float(z))});
        return it->second;
    }
};

} // namespace

Result<TriangleMesh> readStl(const std::filesystem::path& path, std::size_t maxTriangles)
{
    using R = Result<TriangleMesh>;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return R::failure(ErrorCode::FileNotFound, "The file could not be found.", "readStl: not a file");
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size > kMaxImportFileBytes)
        return R::failure(ErrorCode::FileReadError, "The file is too large to import.", "readStl: size");
    if (size < 15)
        return R::failure(ErrorCode::FileFormatError, "This STL file is empty.", "readStl: tiny file");
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return R::failure(ErrorCode::FileReadError, "The file could not be opened.", "readStl: open failed");
    std::string head(std::size_t(std::min<std::uintmax_t>(size, 84)), '\0');
    in.read(head.data(), std::streamsize(head.size()));
    const auto tooMany = [&](std::size_t n) {
        return R::failure(ErrorCode::Unsupported,
                          "This STL file has " + std::to_string(n) + " triangles, more than the "
                              + std::to_string(maxTriangles) + " a body can have. Simplify it first.",
                          "readStl: too many triangles");
    };
    Welder weld;
    std::uint32_t count = 0;
    if (size >= 84)
        std::memcpy(&count, head.data() + 80, 4);
    const bool binary = size >= 84 && std::uintmax_t(84) + std::uintmax_t(count) * 50 == size;
    if (binary) {
        if (count > maxTriangles)
            return tooMany(count);
        std::vector<char> buffer(std::size_t(count) * 50);
        in.read(buffer.data(), std::streamsize(buffer.size()));
        if (!in)
            return R::failure(ErrorCode::FileFormatError, "This STL file is cut short.", "readStl: truncated binary");
        for (std::uint32_t t = 0; t < count; ++t) {
            std::array<int, 3> tri{};
            for (int k = 0; k < 3; ++k) {
                float xyz[3];
                std::memcpy(xyz, buffer.data() + std::size_t(t) * 50 + 12 + 12 * k, 12);
                if (!std::isfinite(xyz[0]) || !std::isfinite(xyz[1]) || !std::isfinite(xyz[2]))
                    return R::failure(ErrorCode::FileFormatError, "This STL file has invalid coordinates.",
                                      "readStl: non-finite vertex");
                tri[k] = weld.add(xyz[0], xyz[1], xyz[2]);
            }
            weld.mesh.triangles.push_back(tri);
        }
    } else {
        if (head.compare(0, 5, "solid") != 0)
            return R::failure(ErrorCode::FileFormatError, "This does not look like an STL file.", "readStl: unknown format");
        in.clear();
        in.seekg(0);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        std::istringstream words(text);
        words.imbue(std::locale::classic());
        std::string word;
        std::vector<int> corners;
        bool facets = false;
        while (words >> word) {
            if (word == "facet")
                facets = true;
            if (word != "vertex")
                continue;
            double x, y, z;
            if (!(words >> x >> y >> z) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z))
                return R::failure(ErrorCode::FileFormatError, "This STL file is damaged (a corner could not be read).",
                                  "readStl: bad ASCII vertex");
            corners.push_back(weld.add(x, y, z));
            if (corners.size() == 3) {
                weld.mesh.triangles.push_back({corners[0], corners[1], corners[2]});
                corners.clear();
                if (weld.mesh.triangles.size() > maxTriangles)
                    return tooMany(weld.mesh.triangles.size());
            }
        }
        if (!facets || !corners.empty())
            return R::failure(ErrorCode::FileFormatError, "This STL file is damaged.", "readStl: incomplete ASCII facets");
    }
    if (weld.mesh.triangles.empty())
        return R::failure(ErrorCode::FileFormatError, "This STL file has no triangles.", "readStl: no triangles");
    OS_LOG(Info, File) << "read STL: " << weld.mesh.triangles.size() << " triangles, " << weld.mesh.vertices.size()
                       << " vertices";
    return R::success(std::move(weld.mesh));
}

std::vector<TriangleMesh> meshPieces(const TriangleMesh& mesh)
{
    std::unordered_map<std::uint64_t, std::vector<int>> edgeTris;
    for (int t = 0; t < int(mesh.triangles.size()); ++t)
        for (int k = 0; k < 3; ++k)
            edgeTris[edgeKey(mesh.triangles[t][k], mesh.triangles[t][(k + 1) % 3])].push_back(t);
    std::vector<int> piece(mesh.triangles.size(), -1);
    std::vector<TriangleMesh> out;
    std::vector<int> stack;
    for (int seed = 0; seed < int(mesh.triangles.size()); ++seed) {
        if (piece[seed] >= 0)
            continue;
        const int id = int(out.size());
        out.emplace_back();
        std::unordered_map<int, int> local;
        piece[seed] = id;
        stack.push_back(seed);
        while (!stack.empty()) {
            const int t = stack.back();
            stack.pop_back();
            std::array<int, 3> tri{};
            for (int k = 0; k < 3; ++k) {
                const int g = mesh.triangles[t][k];
                const auto [it, inserted] = local.emplace(g, int(out[id].vertices.size()));
                if (inserted)
                    out[id].vertices.push_back(mesh.vertices[g]);
                tri[k] = it->second;
                for (int u : edgeTris[edgeKey(g, mesh.triangles[t][(k + 1) % 3])])
                    if (piece[u] < 0) {
                        piece[u] = id;
                        stack.push_back(u);
                    }
            }
            out[id].triangles.push_back(tri);
        }
    }
    return out;
}

} // namespace os::geom
