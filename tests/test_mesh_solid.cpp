// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Triangle meshes as solids (geometry/MeshSolid): scans and STL files
// becoming bodies; the checks that refuse broken meshes; the STL reader.

#include "core/Uuid.h"
#include "geometry/MeshSolid.h"
#include "geometry/Modeling.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace os;
using namespace os::geom;

namespace {

using Tris = std::vector<std::array<int, 3>>;

// A cube of side s from the origin, 12 outward triangles.
void cube(double s, std::vector<Vec3>& v, Tris& t)
{
    v.clear();
    for (int i = 0; i < 8; ++i)
        v.push_back({i & 1 ? s : 0.0, i & 2 ? s : 0.0, i & 4 ? s : 0.0});
    t = {{0, 2, 1}, {1, 2, 3}, {4, 5, 6}, {5, 7, 6}, {0, 1, 4}, {1, 5, 4},
         {2, 6, 3}, {3, 6, 7}, {0, 4, 2}, {2, 4, 6}, {1, 3, 5}, {3, 7, 5}};
}

// A UV sphere: `rings` x `segments` quads, two triangles each, poles fanned.
void sphere(double r, int rings, int segments, std::vector<Vec3>& v, Tris& t)
{
    v.clear();
    t.clear();
    v.push_back({0, 0, r});
    for (int i = 1; i < rings; ++i) {
        const double th = kPi * i / rings;
        for (int j = 0; j < segments; ++j) {
            const double ph = 2 * kPi * j / segments;
            v.push_back({r * std::sin(th) * std::cos(ph), r * std::sin(th) * std::sin(ph), r * std::cos(th)});
        }
    }
    v.push_back({0, 0, -r});
    const int south = int(v.size()) - 1;
    auto at = [&](int ring, int seg) { return 1 + (ring - 1) * segments + (seg % segments); };
    for (int j = 0; j < segments; ++j)
        t.push_back({0, at(1, j), at(1, j + 1)});
    for (int i = 1; i < rings - 1; ++i)
        for (int j = 0; j < segments; ++j) {
            t.push_back({at(i, j), at(i + 1, j), at(i + 1, j + 1)});
            t.push_back({at(i, j), at(i + 1, j + 1), at(i, j + 1)});
        }
    for (int j = 0; j < segments; ++j)
        t.push_back({south, at(rings - 1, j + 1), at(rings - 1, j)});
}

std::filesystem::path tempFile(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_stl_" + Uuid::generate().toString() + "_" + name);
}

} // namespace

TEST(MeshSolid, Tetrahedron)
{
    const std::vector<Vec3> v{{0, 0, 0}, {10, 0, 0}, {0, 10, 0}, {0, 0, 10}};
    const Tris t{{0, 2, 1}, {0, 1, 3}, {1, 2, 3}, {0, 3, 2}};
    auto r = solidFromMesh(v, t);
    ASSERT_TRUE(r) << r.developerMessage();
    EXPECT_EQ(r->solidCount(), 1);
    EXPECT_EQ(r->faceCount(), 4);
    EXPECT_NEAR(volume(r.value()), 1000.0 / 6.0, 1e-9);
    EXPECT_TRUE(isValid(r.value()));
}

TEST(MeshSolid, CubeIsExactAndMergesToSixFaces)
{
    std::vector<Vec3> v;
    Tris t;
    cube(10, v, t);
    MeshSolidOptions keep;
    keep.mergeCoplanar = false;
    auto triangles = solidFromMesh(v, t, keep);
    ASSERT_TRUE(triangles) << triangles.developerMessage();
    EXPECT_EQ(triangles->faceCount(), 12);
    EXPECT_NEAR(volume(triangles.value()), 1000.0, 1e-9);
    auto merged = solidFromMesh(v, t);
    ASSERT_TRUE(merged) << merged.developerMessage();
    EXPECT_EQ(merged->faceCount(), 6);
    EXPECT_NEAR(volume(merged.value()), 1000.0, 1e-9);
    const BoundingBox box = boundingBox(merged.value());
    EXPECT_NEAR(box.min.x, 0, 1e-6);
    EXPECT_NEAR(box.max.z, 10, 1e-6);
}

TEST(MeshSolid, RepairsOrientationAndInsideOut)
{
    std::vector<Vec3> v;
    Tris t;
    cube(10, v, t);
    std::swap(t[3][1], t[3][2]); // one triangle flipped
    MeshDiagnosis d;
    auto r = solidFromMesh(v, t, {}, &d);
    ASSERT_TRUE(r) << r.developerMessage();
    EXPECT_EQ(d.flippedTriangles, 1);
    EXPECT_NEAR(volume(r.value()), 1000.0, 1e-9);
    for (auto& tri : t)
        std::swap(tri[1], tri[2]); // all inward now (but the one)
    r = solidFromMesh(v, t, {}, &d);
    ASSERT_TRUE(r);
    EXPECT_TRUE(d.insideOut);
    EXPECT_NEAR(volume(r.value()), 1000.0, 1e-9);
}

TEST(MeshSolid, RefusesBrokenMeshes)
{
    std::vector<Vec3> v;
    Tris t;
    cube(10, v, t);
    Tris open = t;
    open.pop_back();
    auto r = solidFromMesh(v, open);
    EXPECT_FALSE(r);
    EXPECT_NE(r.userMessage().find("not closed"), std::string::npos) << r.userMessage();

    // Two cubes: two pieces.
    std::vector<Vec3> two = v;
    Tris twoTris = t;
    for (const Vec3& p : v)
        two.push_back(p + Vec3{20, 0, 0});
    for (const auto& tri : t)
        twoTris.push_back({tri[0] + 8, tri[1] + 8, tri[2] + 8});
    r = solidFromMesh(two, twoTris);
    EXPECT_FALSE(r);
    EXPECT_NE(r.userMessage().find("2 separate pieces"), std::string::npos) << r.userMessage();

    // A cube with one corner pushed through the opposite face: crosses itself.
    std::vector<Vec3> bent = v;
    bent[7] = {-5, -5, -5};
    r = solidFromMesh(bent, t);
    EXPECT_FALSE(r);
    EXPECT_NE(r.userMessage().find("crosses itself"), std::string::npos) << r.userMessage();

    EXPECT_FALSE(solidFromMesh({}, {}));
    MeshSolidOptions small;
    small.maxTriangles = 10;
    EXPECT_FALSE(solidFromMesh(v, t, small));
}

TEST(MeshSolid, ThreeThousandTrianglesInGoodTime)
{
    std::vector<Vec3> v;
    Tris t;
    sphere(20, 39, 40, v, t); // 40 + 37*80 + 40 = 3040 triangles
    ASSERT_GT(t.size(), 3000u);
    const auto start = std::chrono::steady_clock::now();
    auto r = solidFromMesh(v, t);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    ASSERT_TRUE(r) << r.developerMessage();
    std::printf("[mesh] solidFromMesh: %zu triangles in %.0f ms\n", t.size(), ms);
    double expected = 0;
    for (const auto& tri : t)
        expected += v[tri[0]].dot(v[tri[1]].cross(v[tri[2]])) / 6.0;
    EXPECT_NEAR(volume(r.value()), expected, 1e-6 * expected);
    EXPECT_TRUE(isValid(r.value()));
    EXPECT_LT(ms, 20000.0);
}

TEST(MeshSolid, ReadsBinaryAndAsciiStl)
{
    std::vector<Vec3> v;
    Tris t;
    cube(10, v, t);
    // Binary, every corner repeated per triangle as STL does.
    const auto binary = tempFile("cube.stl");
    {
        std::ofstream out(binary, std::ios::binary);
        char header[80] = "solid but actually binary";
        out.write(header, 80);
        const std::uint32_t n = std::uint32_t(t.size());
        out.write(reinterpret_cast<const char*>(&n), 4);
        for (const auto& tri : t) {
            float data[12] = {0, 0, 0};
            for (int k = 0; k < 3; ++k) {
                data[3 + 3 * k] = float(v[tri[k]].x);
                data[4 + 3 * k] = float(v[tri[k]].y);
                data[5 + 3 * k] = float(v[tri[k]].z);
            }
            out.write(reinterpret_cast<const char*>(data), 48);
            const std::uint16_t attr = 0;
            out.write(reinterpret_cast<const char*>(&attr), 2);
        }
    }
    auto mesh = readStl(binary);
    ASSERT_TRUE(mesh) << mesh.developerMessage();
    EXPECT_EQ(mesh->triangles.size(), 12u);
    EXPECT_EQ(mesh->vertices.size(), 8u);
    auto solid = solidFromMesh(mesh->vertices, mesh->triangles);
    ASSERT_TRUE(solid);
    EXPECT_NEAR(volume(solid.value()), 1000.0, 1e-6);
    EXPECT_FALSE(readStl(binary, 11)); // over the limit
    std::filesystem::remove(binary);

    const auto ascii = tempFile("cube_ascii.stl");
    {
        std::ofstream out(ascii);
        out << "solid cube\n";
        for (const auto& tri : t) {
            out << "  facet normal 0 0 0\n    outer loop\n";
            for (int k = 0; k < 3; ++k)
                out << "      vertex " << v[tri[k]].x << " " << v[tri[k]].y << " " << v[tri[k]].z << "\n";
            out << "    endloop\n  endfacet\n";
        }
        out << "endsolid cube\n";
    }
    mesh = readStl(ascii);
    ASSERT_TRUE(mesh) << mesh.developerMessage();
    EXPECT_EQ(mesh->triangles.size(), 12u);
    EXPECT_EQ(meshPieces(mesh.value()).size(), 1u);
    std::filesystem::remove(ascii);

    const auto junk = tempFile("junk.stl");
    {
        std::ofstream out(junk, std::ios::binary);
        out << "this is not a mesh at all, just some words to fill the file up";
    }
    EXPECT_FALSE(readStl(junk));
    std::filesystem::remove(junk);
}
