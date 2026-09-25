#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "geometry/TopoSignature.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <set>

using namespace os;
using namespace os::geom;

namespace {

constexpr double kTol = 1e-6;

Shape box(double x, double y, double z)
{
    auto r = makeBox({0, 0, 0}, {x, y, z});
    EXPECT_TRUE(r.ok()) << r.developerMessage();
    return r.value();
}

// Index of the planar face whose outward normal matches `n`.
int faceWithNormal(const Shape& s, const Vec3& n)
{
    for (int i = 0; i < s.faceCount(); ++i) {
        const auto info = faceInfo(s, i);
        if (info && info->isPlanar() && info->normal.dot(n) > 0.9999)
            return i;
    }
    return -1;
}

std::vector<int> verticalEdges(const Shape& s)
{
    std::vector<int> out;
    for (int i = 0; i < s.edgeCount(); ++i) {
        const auto info = edgeInfo(s, i);
        if (info && info->kind == CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1.0) < 1e-9)
            out.push_back(i);
    }
    return out;
}

std::filesystem::path tempFile(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_test_" + name);
}

} // namespace

TEST(Geometry, BoxDimensionsAndVolume)
{
    const Shape s = box(10, 20, 30);
    EXPECT_EQ(s.faceCount(), 6);
    EXPECT_EQ(s.edgeCount(), 12);
    EXPECT_EQ(s.vertexCount(), 8);
    EXPECT_NEAR(volume(s), 6000.0, kTol);
    const auto bb = boundingBox(s);
    ASSERT_TRUE(bb.valid);
    EXPECT_NEAR(bb.size().x, 10.0, kTol);
    EXPECT_NEAR(bb.size().y, 20.0, kTol);
    EXPECT_NEAR(bb.size().z, 30.0, kTol);
    EXPECT_TRUE(isValid(s));
}

TEST(Geometry, BoxRejectsZeroSize)
{
    auto r = makeBox({0, 0, 0}, {10, 0, 5});
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::InvalidArgument);
    EXPECT_FALSE(r.userMessage().empty());
}

TEST(Geometry, FaceInfoOfBoxTop)
{
    const Shape s = box(10, 20, 30);
    const int top = faceWithNormal(s, {0, 0, 1});
    ASSERT_GE(top, 0);
    const auto info = faceInfo(s, top);
    ASSERT_TRUE(info);
    EXPECT_TRUE(info->isPlanar());
    EXPECT_NEAR(info->area, 200.0, kTol);
    EXPECT_NEAR(info->centroid.x, 5.0, kTol);
    EXPECT_NEAR(info->centroid.y, 10.0, kTol);
    EXPECT_NEAR(info->centroid.z, 30.0, kTol);
}

TEST(Geometry, PushPullTopFaceOutward)
{
    const Shape s = box(20, 20, 20);
    const int top = faceWithNormal(s, {0, 0, 1});
    auto r = pushPullFace(s, top, 15.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    const auto bb = boundingBox(r.value());
    EXPECT_NEAR(bb.size().z, 35.0, kTol);
    EXPECT_NEAR(bb.size().x, 20.0, kTol);
    EXPECT_NEAR(volume(r.value()), 20.0 * 20.0 * 35.0, 1e-4);
    // Coplanar side faces are merged back: still a 6-face box.
    EXPECT_EQ(r.value().faceCount(), 6);
}

TEST(Geometry, PushPullTopFaceInward)
{
    const Shape s = box(20, 20, 20);
    const int top = faceWithNormal(s, {0, 0, 1});
    auto r = pushPullFace(s, top, -5.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    EXPECT_NEAR(boundingBox(r.value()).size().z, 15.0, kTol);
    EXPECT_NEAR(volume(r.value()), 20.0 * 20.0 * 15.0, 1e-4);
    EXPECT_EQ(r.value().faceCount(), 6);
}

TEST(Geometry, PushPullSideFace)
{
    const Shape s = box(10, 20, 30);
    const int side = faceWithNormal(s, {-1, 0, 0});
    auto r = pushPullFace(s, side, 5.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    const auto bb = boundingBox(r.value());
    EXPECT_NEAR(bb.min.x, -5.0, kTol);
    EXPECT_NEAR(bb.size().x, 15.0, kTol);
}

TEST(Geometry, PushPullThroughWholeBodyFails)
{
    const Shape s = box(20, 20, 20);
    const int top = faceWithNormal(s, {0, 0, 1});
    auto r = pushPullFace(s, top, -25.0);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::EmptyResult);
}

TEST(Geometry, PushPullInvalidIndex)
{
    const Shape s = box(20, 20, 20);
    auto r = pushPullFace(s, 99, 5.0);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::InvalidReference);
}

TEST(Geometry, FilletVerticalEdges)
{
    const Shape s = box(60, 40, 20);
    const auto edges = verticalEdges(s);
    ASSERT_EQ(edges.size(), 4u);
    auto r = filletEdges(s, edges, 3.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    // Each fillet removes (r^2 - pi r^2 / 4) * height.
    const double removed = 4 * (9.0 - kPi * 9.0 / 4.0) * 20.0;
    EXPECT_NEAR(volume(r.value()), 60.0 * 40.0 * 20.0 - removed, 1e-3);
    EXPECT_EQ(r.value().faceCount(), 10);
    const auto bb = boundingBox(r.value());
    EXPECT_NEAR(bb.size().x, 60.0, 1e-5);
    EXPECT_NEAR(bb.size().y, 40.0, 1e-5);
}

TEST(Geometry, FilletTooLargeFailsGracefully)
{
    const Shape s = box(10, 10, 10);
    const auto edges = verticalEdges(s);
    auto r = filletEdges(s, edges, 8.0);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::FilletRadiusTooLarge);
    EXPECT_EQ(r.userMessage(), "Unable to create this fillet. Try a smaller radius.");
    EXPECT_FALSE(r.developerMessage().empty());
}

TEST(Geometry, ChamferEdge)
{
    const Shape s = box(20, 20, 20);
    const auto edges = verticalEdges(s);
    auto r = chamferEdges(s, {edges.front()}, 2.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    EXPECT_NEAR(volume(r.value()), 8000.0 - 0.5 * 2.0 * 2.0 * 20.0, 1e-4);
    EXPECT_EQ(r.value().faceCount(), 7);
}

TEST(Geometry, BooleanUnionAndSubtract)
{
    const Shape a = box(20, 20, 20);
    auto b = makeBox({10, 0, 0}, {20, 20, 20});
    ASSERT_TRUE(b.ok());

    auto u = booleanOp(a, b.value(), BooleanKind::Union);
    ASSERT_TRUE(u.ok()) << u.developerMessage();
    EXPECT_NEAR(volume(u.value()), 30.0 * 20.0 * 20.0, 1e-4);
    EXPECT_EQ(u.value().faceCount(), 6);

    auto d = booleanOp(a, b.value(), BooleanKind::Subtract);
    ASSERT_TRUE(d.ok()) << d.developerMessage();
    EXPECT_NEAR(volume(d.value()), 10.0 * 20.0 * 20.0, 1e-4);

    auto i = booleanOp(a, b.value(), BooleanKind::Intersect);
    ASSERT_TRUE(i.ok()) << i.developerMessage();
    EXPECT_NEAR(volume(i.value()), 10.0 * 20.0 * 20.0, 1e-4);
}

TEST(Geometry, SubtractCylinderMakesHole)
{
    const Shape plate = box(60, 30, 5);
    auto cyl = makeCylinder({15, 15, -1}, {0, 0, 1}, 3.0, 7.0);
    ASSERT_TRUE(cyl.ok());
    auto r = booleanOp(plate, cyl.value(), BooleanKind::Subtract);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    EXPECT_NEAR(volume(r.value()), 60.0 * 30.0 * 5.0 - kPi * 9.0 * 5.0, 1e-3);
}

TEST(Geometry, TransformKeepsVolume)
{
    const Shape s = box(10, 20, 30);
    auto t = translated(s, {5, 0, 0});
    ASSERT_TRUE(t.ok());
    EXPECT_NEAR(boundingBox(t.value()).min.x, 5.0, kTol);
    auto r = rotated(s, {0, 0, 0}, {0, 0, 1}, kPi / 2);
    ASSERT_TRUE(r.ok());
    EXPECT_NEAR(volume(r.value()), 6000.0, 1e-6);
    EXPECT_NEAR(boundingBox(r.value()).size().x, 20.0, 1e-6);
}

TEST(Geometry, BrepRoundTrip)
{
    const Shape s = box(10, 20, 30);
    const std::string text = toBrepString(s);
    EXPECT_FALSE(text.empty());
    auto r = fromBrepString(text);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    EXPECT_NEAR(volume(r.value()), 6000.0, kTol);
    EXPECT_EQ(r.value().faceCount(), 6);
}

TEST(Geometry, BrepGarbageFailsGracefully)
{
    auto r = fromBrepString("this is not a brep file");
    EXPECT_FALSE(r.ok());
}

TEST(Geometry, TessellationCoversAllFaces)
{
    const Shape s = box(20, 20, 20);
    const Mesh mesh = tessellate(s);
    EXPECT_EQ(mesh.triangleCount(), 12u);
    EXPECT_EQ(mesh.triangleFace.size(), mesh.triangleCount());
    std::set<std::uint32_t> faces(mesh.triangleFace.begin(), mesh.triangleFace.end());
    EXPECT_EQ(faces.size(), 6u);
    EXPECT_EQ(mesh.edges.size(), 12u);
    // Triangle winding must agree with outward normals.
    for (std::size_t t = 0; t < mesh.triangleCount(); ++t) {
        const Vec3 a = mesh.vertex(mesh.indices[3 * t]);
        const Vec3 b = mesh.vertex(mesh.indices[3 * t + 1]);
        const Vec3 c = mesh.vertex(mesh.indices[3 * t + 2]);
        const Vec3 geometric = (b - a).cross(c - a).normalized();
        const std::size_t v = mesh.indices[3 * t];
        const Vec3 stored{mesh.normals[3 * v], mesh.normals[3 * v + 1], mesh.normals[3 * v + 2]};
        EXPECT_GT(geometric.dot(stored), 0.99);
        const auto info = faceInfo(s, static_cast<int>(mesh.triangleFace[t]));
        ASSERT_TRUE(info);
        EXPECT_GT(geometric.dot(info->normal), 0.99);
    }
}

TEST(Geometry, FaceSignatureResolvesAfterUpstreamChange)
{
    const Shape small = box(20, 20, 20);
    const int top = faceWithNormal(small, {0, 0, 1});
    const auto sig = captureFaceSignature(small, top);
    ASSERT_TRUE(sig);
    EXPECT_EQ(resolveFace(small, *sig, top), top);

    // Same conceptual face on a taller box: resolved by signature.
    const Shape tall = box(20, 20, 35);
    const auto resolved = resolveFace(tall, *sig, -1);
    ASSERT_TRUE(resolved);
    EXPECT_EQ(*resolved, faceWithNormal(tall, {0, 0, 1}));
}

TEST(Geometry, EdgeSignatureResolves)
{
    const Shape s = box(20, 20, 20);
    const auto edges = verticalEdges(s);
    const auto sig = captureEdgeSignature(s, edges[1]);
    ASSERT_TRUE(sig);
    EXPECT_EQ(resolveEdge(s, *sig, edges[1]), edges[1]);
    EXPECT_EQ(resolveEdge(s, *sig, -1), edges[1]);
}

TEST(Geometry, ExportStepAndStl)
{
    const Shape s = box(60, 40, 20);
    const auto step = tempFile("box.step");
    const auto stl = tempFile("box.stl");
    std::filesystem::remove(step);
    std::filesystem::remove(stl);

    ASSERT_TRUE(exportStep({{"Box", s}}, step).ok());
    ASSERT_TRUE(exportStl({{"Box", s}}, stl).ok());
    EXPECT_GT(std::filesystem::file_size(step), 1000u);
    // Binary STL: 80-byte header + 4-byte count + 50 bytes per triangle.
    const auto stlSize = std::filesystem::file_size(stl);
    EXPECT_EQ((stlSize - 84) % 50, 0u);
    EXPECT_GE((stlSize - 84) / 50, 12u);

    auto imported = importStep(step);
    ASSERT_TRUE(imported.ok()) << imported.developerMessage();
    ASSERT_EQ(imported.value().size(), 1u);
    EXPECT_NEAR(volume(imported.value().front().shape), 48000.0, 1e-3);
}

TEST(Geometry, ImportMissingOrGarbageStep)
{
    EXPECT_EQ(importStep(tempFile("does_not_exist.step")).error(), ErrorCode::FileNotFound);
    const auto garbage = tempFile("garbage.step");
    {
        std::FILE* f = std::fopen(garbage.string().c_str(), "wb");
        ASSERT_NE(f, nullptr);
        std::fputs("ISO-10303-21;\nthis is garbage\n", f);
        std::fclose(f);
    }
    EXPECT_FALSE(importStep(garbage).ok());
}

TEST(Geometry, ShellOpenTopBox)
{
    // 60 x 40 x 30 enclosure, open top, 2 mm walls.
    const Shape s = box(60, 40, 30);
    const int top = faceWithNormal(s, {0, 0, 1});
    auto r = shell(s, {top}, 2.0);
    ASSERT_TRUE(r.ok()) << r.developerMessage();
    const double inner = 56.0 * 36.0 * 28.0;
    EXPECT_NEAR(volume(r.value()), 60.0 * 40.0 * 30.0 - inner, 1e-3);
    const auto bb = boundingBox(r.value());
    EXPECT_NEAR(bb.size().x, 60.0, 1e-6);
    EXPECT_NEAR(bb.size().z, 30.0, 1e-6);
    EXPECT_EQ(r.value().faceCount(), 11); // 5 outside + 5 inside + rim
}

TEST(Geometry, ShellTooThickFails)
{
    const Shape s = box(10, 10, 10);
    auto r = shell(s, {faceWithNormal(s, {0, 0, 1})}, 6.0);
    EXPECT_FALSE(r.ok());
    EXPECT_FALSE(r.userMessage().empty());
}
