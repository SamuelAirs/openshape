// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/MeshTools.h"

#include "measure/ScanMath.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <queue>
#include <unordered_map>

namespace os::measure {

namespace {

// ---- Occupancy clean-up: a well-composed voxel set (no two voxels of one
// kind touching only along an edge or at a corner while the other kind
// separates them) has a 2-manifold boundary, so the surface is closed and
// manifold whatever the field looked like.
struct Occupancy {
    int X = 0, Y = 0, Z = 0;
    std::vector<std::uint8_t> inside;
    std::size_t idx(int x, int y, int z) const { return (std::size_t(z) * Y + y) * X + x; }
    bool padding(int x, int y, int z) const { return x == 0 || y == 0 || z == 0 || x == X - 1 || y == Y - 1 || z == Z - 1; }
};

void makeWellComposed(Occupancy& o)
{
    const int offsets[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    for (int pass = 0; pass < 64; ++pass) {
        bool changed = false;
        for (int z = 0; z + 1 < o.Z; ++z)
            for (int y = 0; y + 1 < o.Y; ++y)
                for (int x = 0; x + 1 < o.X; ++x) {
                    // Face checkerboards in the three planes through (x, y, z).
                    for (int a = 0; a < 3; ++a)
                        for (int b = a + 1; b < 3; ++b) {
                            const int* u = offsets[a];
                            const int* v = offsets[b];
                            const int p0[3] = {x, y, z};
                            const int pu[3] = {x + u[0], y + u[1], z + u[2]};
                            const int pv[3] = {x + v[0], y + v[1], z + v[2]};
                            const int puv[3] = {x + u[0] + v[0], y + u[1] + v[1], z + u[2] + v[2]};
                            if (puv[0] >= o.X || puv[1] >= o.Y || puv[2] >= o.Z)
                                continue;
                            const std::size_t i0 = o.idx(p0[0], p0[1], p0[2]), iu = o.idx(pu[0], pu[1], pu[2]);
                            const std::size_t iv = o.idx(pv[0], pv[1], pv[2]), iuv = o.idx(puv[0], puv[1], puv[2]);
                            if (o.inside[i0] != o.inside[iuv] || o.inside[iu] != o.inside[iv] || o.inside[i0] == o.inside[iu])
                                continue;
                            // Fill one voxel of the outside diagonal (never the padding).
                            if (!o.inside[i0]) {
                                if (!o.padding(p0[0], p0[1], p0[2]))
                                    o.inside[i0] = 1;
                                else
                                    o.inside[iuv] = 1;
                            } else {
                                if (!o.padding(pu[0], pu[1], pu[2]))
                                    o.inside[iu] = 1;
                                else
                                    o.inside[iv] = 1;
                            }
                            changed = true;
                        }
                    if (x + 1 >= o.X || y + 1 >= o.Y || z + 1 >= o.Z)
                        continue;
                    // Corner-only contacts in the 2 x 2 x 2 block.
                    std::size_t ids[8];
                    int count = 0;
                    for (int c = 0; c < 8; ++c) {
                        ids[c] = o.idx(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1));
                        count += o.inside[ids[c]];
                    }
                    if (count == 2 || count == 6) {
                        const std::uint8_t lonely = count == 2 ? 1 : 0;
                        for (int c = 0; c < 4; ++c) {
                            if (o.inside[ids[c]] == lonely && o.inside[ids[7 - c]] == lonely) {
                                if (count == 2) {
                                    // Join them through a neighbour of the first.
                                    const int n = c ^ 1;
                                    o.inside[ids[n]] = 1;
                                } else {
                                    o.inside[ids[c]] = 1;
                                }
                                changed = true;
                                break;
                            }
                        }
                    }
                }
        if (!changed)
            break;
    }
}

struct Quadric {
    double a[10]{}; // xx xy xz xw yy yz yw zz zw ww
    void addPlane(const Vec3& n, double d, double w)
    {
        a[0] += w * n.x * n.x;
        a[1] += w * n.x * n.y;
        a[2] += w * n.x * n.z;
        a[3] += w * n.x * d;
        a[4] += w * n.y * n.y;
        a[5] += w * n.y * n.z;
        a[6] += w * n.y * d;
        a[7] += w * n.z * n.z;
        a[8] += w * n.z * d;
        a[9] += w * d * d;
    }
    Quadric operator+(const Quadric& o) const
    {
        Quadric q;
        for (int i = 0; i < 10; ++i)
            q.a[i] = a[i] + o.a[i];
        return q;
    }
    double eval(const Vec3& p) const
    {
        return a[0] * p.x * p.x + 2 * a[1] * p.x * p.y + 2 * a[2] * p.x * p.z + 2 * a[3] * p.x + a[4] * p.y * p.y
               + 2 * a[5] * p.y * p.z + 2 * a[6] * p.y + a[7] * p.z * p.z + 2 * a[8] * p.z + a[9];
    }
    bool optimum(Vec3& p) const
    {
        const scanmath::Mat3 m{a[0], a[1], a[2], a[1], a[4], a[5], a[2], a[5], a[7]};
        const double scale = std::abs(a[0]) + std::abs(a[4]) + std::abs(a[7]);
        if (!(scale > 0) || std::abs(scanmath::det(m)) < 1e-9 * scale * scale * scale)
            return false;
        scanmath::Mat3 inv{};
        if (!scanmath::invert(m, inv))
            return false;
        p = scanmath::mul(inv, Vec3{-a[3], -a[6], -a[8]});
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    // The best point with z fixed.
    bool optimumAtZ(double z, Vec3& p) const
    {
        const double d = a[0] * a[4] - a[1] * a[1];
        const double scale = std::abs(a[0]) + std::abs(a[4]);
        if (!(scale > 0) || std::abs(d) < 1e-9 * scale * scale)
            return false;
        const double bx = -(a[3] + a[2] * z), by = -(a[6] + a[5] * z);
        p = {(bx * a[4] - by * a[1]) / d, (a[0] * by - a[1] * bx) / d, z};
        return std::isfinite(p.x) && std::isfinite(p.y);
    }
};

Vec3 triangleNormal(const Vec3& a, const Vec3& b, const Vec3& c) { return (b - a).cross(c - a); }

double triangleQuality(const Vec3& a, const Vec3& b, const Vec3& c)
{
    const double area2 = triangleNormal(a, b, c).length();
    const double sum = (b - a).dot(b - a) + (c - b).dot(c - b) + (a - c).dot(a - c);
    return sum > 0 ? 2 * std::sqrt(3.0) * area2 / sum : 0; // 1 for an equilateral triangle
}

} // namespace

ScanMesh surfaceNets(const ScanGrid& grid, bool flatBottom, std::vector<char>* bottomFlags)
{
    ScanMesh mesh;
    if (bottomFlags)
        bottomFlags->clear();
    if (grid.empty() || grid.field.size() != std::size_t(grid.nx) * grid.ny * grid.nz)
        return mesh;
    Occupancy o;
    o.X = grid.nx + 2;
    o.Y = grid.ny + 2;
    o.Z = grid.nz + 2;
    o.inside.assign(std::size_t(o.X) * o.Y * o.Z, 0);
    std::vector<float> f(o.inside.size(), -1.0f);
    for (int k = 0; k < grid.nz; ++k)
        for (int j = 0; j < grid.ny; ++j)
            for (int i = 0; i < grid.nx; ++i) {
                const float v = grid.at(i, j, k);
                const std::size_t p = o.idx(i + 1, j + 1, k + 1);
                f[p] = v;
                o.inside[p] = v > 0 ? 1 : 0;
            }
    makeWellComposed(o);

    // One vertex per cell (a cube of eight voxel centers) the surface passes.
    const int CX = o.X - 1, CY = o.Y - 1, CZ = o.Z - 1;
    std::vector<int> cellVertex(std::size_t(CX) * CY * CZ, -1);
    auto cellIdx = [&](int x, int y, int z) { return (std::size_t(z) * CY + y) * CX + x; };
    auto voxelCenter = [&](int x, int y, int z) {
        return grid.origin + Vec3{(x - 0.5) * grid.voxel, (y - 0.5) * grid.voxel, (z - 0.5) * grid.voxel};
    };
    static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3},
                                     {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    std::vector<char> bottom;
    for (int z = 0; z < CZ; ++z)
        for (int y = 0; y < CY; ++y)
            for (int x = 0; x < CX; ++x) {
                std::uint8_t in[8];
                float val[8];
                int count = 0;
                for (int c = 0; c < 8; ++c) {
                    const std::size_t p = o.idx(x + (c & 1), y + ((c >> 1) & 1), z + ((c >> 2) & 1));
                    in[c] = o.inside[p];
                    val[c] = f[p];
                    count += in[c];
                }
                if (count == 0 || count == 8)
                    continue;
                Vec3 sum;
                int crossings = 0;
                for (const auto& e : edges) {
                    if (in[e[0]] == in[e[1]])
                        continue;
                    // Where the field crosses zero along the edge; the clean-up
                    // may have filled a voxel whose field is still negative.
                    double t = 0.5;
                    const double a = val[e[0]], b = val[e[1]];
                    if ((a > 0) == (in[e[0]] != 0) && (b > 0) == (in[e[1]] != 0) && a != b)
                        t = std::clamp(a / (a - b), 0.08, 0.92);
                    const int c0 = e[0], c1 = e[1];
                    const Vec3 p0 = voxelCenter(x + (c0 & 1), y + ((c0 >> 1) & 1), z + ((c0 >> 2) & 1));
                    const Vec3 p1 = voxelCenter(x + (c1 & 1), y + ((c1 >> 1) & 1), z + ((c1 >> 2) & 1));
                    sum += p0 + (p1 - p0) * t;
                    ++crossings;
                }
                Vec3 v = sum / double(crossings);
                const bool onBottom = flatBottom && z == 0;
                if (onBottom)
                    v.z = grid.origin.z;
                cellVertex[cellIdx(x, y, z)] = int(mesh.vertices.size());
                mesh.vertices.push_back(v);
                bottom.push_back(onBottom ? 1 : 0);
            }

    auto emitQuad = [&](int a, int b, int c, int d) {
        if (a < 0 || b < 0 || c < 0 || d < 0)
            return;
        const Vec3& pa = mesh.vertices[a];
        const Vec3& pb = mesh.vertices[b];
        const Vec3& pc = mesh.vertices[c];
        const Vec3& pd = mesh.vertices[d];
        if ((pa - pc).length() <= (pb - pd).length()) {
            mesh.triangles.push_back({a, b, c});
            mesh.triangles.push_back({a, c, d});
        } else {
            mesh.triangles.push_back({a, b, d});
            mesh.triangles.push_back({b, c, d});
        }
    };
    // One quad per pair of neighbouring voxels across the boundary, facing
    // from the inside one to the outside one.
    for (int z = 1; z < o.Z - 1; ++z)
        for (int y = 1; y < o.Y - 1; ++y)
            for (int x = 0; x < o.X - 1; ++x) {
                const std::uint8_t a = o.inside[o.idx(x, y, z)], b = o.inside[o.idx(x + 1, y, z)];
                if (a == b)
                    continue;
                const int q0 = cellVertex[cellIdx(x, y - 1, z - 1)], q1 = cellVertex[cellIdx(x, y, z - 1)];
                const int q2 = cellVertex[cellIdx(x, y, z)], q3 = cellVertex[cellIdx(x, y - 1, z)];
                if (a)
                    emitQuad(q0, q1, q2, q3);
                else
                    emitQuad(q3, q2, q1, q0);
            }
    for (int z = 1; z < o.Z - 1; ++z)
        for (int y = 0; y < o.Y - 1; ++y)
            for (int x = 1; x < o.X - 1; ++x) {
                const std::uint8_t a = o.inside[o.idx(x, y, z)], b = o.inside[o.idx(x, y + 1, z)];
                if (a == b)
                    continue;
                const int q0 = cellVertex[cellIdx(x - 1, y, z - 1)], q1 = cellVertex[cellIdx(x - 1, y, z)];
                const int q2 = cellVertex[cellIdx(x, y, z)], q3 = cellVertex[cellIdx(x, y, z - 1)];
                if (a)
                    emitQuad(q0, q1, q2, q3);
                else
                    emitQuad(q3, q2, q1, q0);
            }
    for (int z = 0; z < o.Z - 1; ++z)
        for (int y = 1; y < o.Y - 1; ++y)
            for (int x = 1; x < o.X - 1; ++x) {
                const std::uint8_t a = o.inside[o.idx(x, y, z)], b = o.inside[o.idx(x, y, z + 1)];
                if (a == b)
                    continue;
                const int q0 = cellVertex[cellIdx(x - 1, y - 1, z)], q1 = cellVertex[cellIdx(x, y - 1, z)];
                const int q2 = cellVertex[cellIdx(x, y, z)], q3 = cellVertex[cellIdx(x - 1, y, z)];
                if (a)
                    emitQuad(q0, q1, q2, q3);
                else
                    emitQuad(q3, q2, q1, q0);
            }
    if (bottomFlags)
        *bottomFlags = std::move(bottom);
    return mesh;
}

namespace {

std::vector<std::vector<int>> vertexNeighbours(const ScanMesh& mesh)
{
    std::vector<std::vector<int>> n(mesh.vertices.size());
    for (const auto& t : mesh.triangles)
        for (int e = 0; e < 3; ++e) {
            const int a = t[e], b = t[(e + 1) % 3];
            n[a].push_back(b);
            n[b].push_back(a);
        }
    for (auto& list : n) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }
    return n;
}

} // namespace

void taubinSmooth(ScanMesh& mesh, int iterations, const std::vector<char>* pinnedZ, double lambda, double mu)
{
    if (iterations <= 0 || mesh.vertices.empty())
        return;
    const auto neighbours = vertexNeighbours(mesh);
    std::vector<Vec3> next(mesh.vertices.size());
    auto pass = [&](double factor) {
        for (std::size_t i = 0; i < mesh.vertices.size(); ++i) {
            const auto& n = neighbours[i];
            if (n.empty()) {
                next[i] = mesh.vertices[i];
                continue;
            }
            Vec3 avg;
            for (int j : n)
                avg += mesh.vertices[j];
            avg = avg / double(n.size());
            Vec3 p = mesh.vertices[i] + (avg - mesh.vertices[i]) * factor;
            if (pinnedZ && (*pinnedZ)[i])
                p.z = mesh.vertices[i].z;
            next[i] = p;
        }
        mesh.vertices.swap(next);
    };
    for (int it = 0; it < iterations; ++it) {
        pass(lambda);
        pass(mu);
    }
}

void decimate(ScanMesh& mesh, int targetTriangles, std::vector<char>* pinnedZ, double minNormalCos)
{
    const int nv = int(mesh.vertices.size());
    int live = int(mesh.triangles.size());
    if (live <= targetTriangles || nv == 0)
        return;
    std::vector<Vec3>& pos = mesh.vertices;
    std::vector<std::array<int, 3>>& tris = mesh.triangles;
    std::vector<char> alive(tris.size(), 1);
    std::vector<char> removed(nv, 0);
    std::vector<char> pinned(nv, 0);
    if (pinnedZ && int(pinnedZ->size()) == nv)
        pinned = *pinnedZ;
    std::vector<int> version(nv, 0);
    std::vector<std::vector<int>> vt(nv);
    std::vector<Quadric> q(nv);
    for (int t = 0; t < int(tris.size()); ++t) {
        const auto& tri = tris[t];
        Vec3 n = triangleNormal(pos[tri[0]], pos[tri[1]], pos[tri[2]]);
        const double area2 = n.length();
        if (area2 > 0)
            n = n / area2;
        const double d = -n.dot(pos[tri[0]]);
        for (int k = 0; k < 3; ++k) {
            vt[tri[k]].push_back(t);
            q[tri[k]].addPlane(n, d, area2 * 0.5);
        }
    }

    struct Candidate {
        double cost;
        int u, v, vu, vv;
        Vec3 p;
        bool operator<(const Candidate& o) const { return cost > o.cost; } // min-heap
    };
    std::priority_queue<Candidate> heap;

    auto place = [&](int u, int v, Vec3& p) -> double {
        const Quadric sum = q[u] + q[v];
        const Vec3 mid = (pos[u] + pos[v]) * 0.5;
        if (pinned[u] && pinned[v]) {
            std::vector<Vec3> options{pos[u], pos[v], mid};
            Vec3 o;
            if (sum.optimumAtZ(pos[u].z, o) && (o - mid).length() < 2 * (pos[u] - pos[v]).length())
                options.push_back(o);
            double best = 1e300;
            for (const Vec3& c : options)
                if (const double e = sum.eval(c); e < best)
                    best = e, p = c;
            return best;
        }
        if (pinned[u] || pinned[v]) {
            p = pinned[u] ? pos[u] : pos[v];
            return sum.eval(p);
        }
        std::vector<Vec3> options{pos[u], pos[v], mid};
        Vec3 o;
        if (sum.optimum(o) && (o - mid).length() < 2 * (pos[u] - pos[v]).length())
            options.push_back(o);
        double best = 1e300;
        for (const Vec3& c : options)
            if (const double e = sum.eval(c); e < best)
                best = e, p = c;
        return best;
    };
    auto push = [&](int u, int v) {
        if (u == v || removed[u] || removed[v])
            return;
        Candidate c;
        c.u = u;
        c.v = v;
        c.vu = version[u];
        c.vv = version[v];
        c.cost = std::max(0.0, place(u, v, c.p));
        heap.push(c);
    };
    for (int t = 0; t < int(tris.size()); ++t)
        for (int k = 0; k < 3; ++k) {
            const int a = tris[t][k], b = tris[t][(k + 1) % 3];
            if (a < b)
                push(a, b);
        }

    std::vector<int> nu, nvList, common;
    auto neighboursOf = [&](int a, std::vector<int>& out) {
        out.clear();
        for (int t : vt[a]) {
            if (!alive[t])
                continue;
            for (int k = 0; k < 3; ++k)
                if (tris[t][k] != a)
                    out.push_back(tris[t][k]);
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    };

    while (live > targetTriangles && !heap.empty()) {
        const Candidate c = heap.top();
        heap.pop();
        const int u = c.u, v = c.v;
        if (removed[u] || removed[v] || version[u] != c.vu || version[v] != c.vv)
            continue;
        // Link condition: the two vertices share exactly the two neighbours
        // of the edge's two triangles (else the collapse pinches the surface).
        neighboursOf(u, nu);
        neighboursOf(v, nvList);
        common.clear();
        std::set_intersection(nu.begin(), nu.end(), nvList.begin(), nvList.end(), std::back_inserter(common));
        int shared = 0;
        std::array<int, 2> opposite{-1, -1};
        for (int t : vt[u]) {
            if (!alive[t])
                continue;
            const auto& tri = tris[t];
            if (tri[0] != v && tri[1] != v && tri[2] != v)
                continue;
            if (shared < 2)
                for (int k = 0; k < 3; ++k)
                    if (tri[k] != u && tri[k] != v)
                        opposite[shared] = tri[k];
            ++shared;
        }
        if (shared != 2 || common.size() != 2)
            continue;
        if (!((common[0] == opposite[0] && common[1] == opposite[1]) || (common[0] == opposite[1] && common[1] == opposite[0])))
            continue;
        // Neither vertex may end up with fewer than three neighbours.
        if (nu.size() + nvList.size() < 8)
            continue;
        // No triangle around may fold over or become a sliver.
        bool ok = true;
        for (int side = 0; side < 2 && ok; ++side) {
            const int a = side == 0 ? u : v;
            for (int t : vt[a]) {
                if (!alive[t])
                    continue;
                const auto& tri = tris[t];
                if ((tri[0] == u || tri[1] == u || tri[2] == u) && (tri[0] == v || tri[1] == v || tri[2] == v))
                    continue;
                Vec3 p[3], moved[3];
                for (int k = 0; k < 3; ++k) {
                    p[k] = pos[tri[k]];
                    moved[k] = tri[k] == a ? c.p : p[k];
                }
                const Vec3 before = triangleNormal(p[0], p[1], p[2]);
                const Vec3 after = triangleNormal(moved[0], moved[1], moved[2]);
                const double la = after.length(), lb = before.length();
                if (!(la > 1e-12) || (lb > 0 && before.dot(after) < minNormalCos * la * lb)) {
                    ok = false;
                    break;
                }
                const double qa = triangleQuality(moved[0], moved[1], moved[2]);
                if (qa < 0.08 && qa < triangleQuality(p[0], p[1], p[2])) {
                    ok = false;
                    break;
                }
            }
        }
        if (!ok)
            continue;
        // Collapse v into u.
        for (int t : vt[v]) {
            if (!alive[t])
                continue;
            auto& tri = tris[t];
            if (tri[0] == u || tri[1] == u || tri[2] == u) {
                alive[t] = 0;
                --live;
                continue;
            }
            for (int k = 0; k < 3; ++k)
                if (tri[k] == v)
                    tri[k] = u;
            vt[u].push_back(t);
        }
        vt[v].clear();
        vt[u].erase(std::remove_if(vt[u].begin(), vt[u].end(), [&](int t) { return !alive[t]; }), vt[u].end());
        removed[v] = 1;
        pos[u] = c.p;
        q[u] = q[u] + q[v];
        pinned[u] = pinned[u] || pinned[v];
        ++version[u];
        ++version[v];
        neighboursOf(u, nu);
        for (int w : nu)
            push(std::min(u, w), std::max(u, w));
    }

    std::vector<std::array<int, 3>> kept;
    kept.reserve(live);
    for (std::size_t t = 0; t < tris.size(); ++t)
        if (alive[t])
            kept.push_back(tris[t]);
    tris.swap(kept);
    if (pinnedZ && int(pinnedZ->size()) == nv) {
        *pinnedZ = pinned;
        compactMesh(mesh, pinnedZ);
    } else {
        compactMesh(mesh);
    }
}

void compactMesh(ScanMesh& mesh, std::vector<char>* flags)
{
    std::vector<int> map(mesh.vertices.size(), -1);
    std::vector<Vec3> vertices;
    std::vector<char> newFlags;
    for (auto& t : mesh.triangles)
        for (int& i : t) {
            if (map[i] < 0) {
                map[i] = int(vertices.size());
                vertices.push_back(mesh.vertices[i]);
                if (flags)
                    newFlags.push_back((*flags)[i]);
            }
            i = map[i];
        }
    mesh.vertices.swap(vertices);
    if (flags)
        flags->swap(newFlags);
}

namespace {

// Whether segment pq crosses triangle abc's interior (touching at a shared
// corner or edge does not count).
bool segmentCrossesTriangle(const Vec3& p, const Vec3& q, const Vec3& a, const Vec3& b, const Vec3& c)
{
    const Vec3 d = q - p;
    const Vec3 e1 = b - a, e2 = c - a;
    const Vec3 h = d.cross(e2);
    const double det = e1.dot(h);
    const double scale = e1.length() * e2.length() * d.length();
    if (!(scale > 0) || std::abs(det) < 1e-10 * scale)
        return false;
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

} // namespace

int selfIntersections(const ScanMesh& mesh, int limit)
{
    const auto& v = mesh.vertices;
    const auto& tris = mesh.triangles;
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
                for (int ka = 0; ka < 3; ++ka)
                    for (int kb = 0; kb < 3; ++kb)
                        if (A[ka] == B[kb]) {
                            ++shared;
                            sharedA = ka;
                            sharedB = kb;
                        }
                if (shared >= 2)
                    continue;
                bool cross = false;
                if (shared == 1) {
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

MeshReport inspectMesh(const ScanMesh& mesh)
{
    MeshReport r;
    r.vertices = int(mesh.vertices.size());
    r.triangles = int(mesh.triangles.size());
    if (mesh.vertices.empty())
        return r;
    r.min = r.max = mesh.vertices.front();
    for (const Vec3& v : mesh.vertices) {
        r.min = {std::min(r.min.x, v.x), std::min(r.min.y, v.y), std::min(r.min.z, v.z)};
        r.max = {std::max(r.max.x, v.x), std::max(r.max.y, v.y), std::max(r.max.z, v.z)};
    }
    struct EdgeUse {
        int count = 0;
        int forward = 0;
    };
    std::unordered_map<std::uint64_t, EdgeUse> edges;
    edges.reserve(mesh.triangles.size() * 2);
    const std::uint64_t n = mesh.vertices.size();
    for (const auto& t : mesh.triangles) {
        const Vec3& a = mesh.vertices[t[0]];
        const Vec3& b = mesh.vertices[t[1]];
        const Vec3& c = mesh.vertices[t[2]];
        const Vec3 cross = (b - a).cross(c - a);
        r.area += 0.5 * cross.length();
        r.volume += a.dot(b.cross(c)) / 6.0;
        if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2] || cross.length() <= 1e-14)
            ++r.degenerateTriangles;
        for (int e = 0; e < 3; ++e) {
            const std::uint64_t p = std::uint64_t(t[e]), q = std::uint64_t(t[(e + 1) % 3]);
            EdgeUse& use = edges[std::min(p, q) * n + std::max(p, q)];
            ++use.count;
            use.forward += p < q ? 1 : -1;
        }
    }
    for (const auto& [key, use] : edges) {
        (void)key;
        if (use.count == 1)
            ++r.boundaryEdges;
        else if (use.count > 2)
            ++r.nonManifoldEdges;
        else if (use.forward != 0)
            ++r.misorientedEdges;
    }
    return r;
}

} // namespace os::measure
