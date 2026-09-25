// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

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

// The fast box drives mesh resolution and camera fitting: it must never be
// smaller than the tight box, and stay close to it for ordinary parts.
TEST(Geometry, ApproximateBoundingBoxContainsTightBox)
{
    const Shape filleted = filletEdges(box(60, 40, 20), verticalEdges(box(60, 40, 20)), 3.0).value();
    auto cylinder = makeCylinder({5, -3, 2}, {1, 1, 0}, 4.0, 25.0);
    ASSERT_TRUE(cylinder.ok()) << cylinder.developerMessage();
    for (const Shape& s : {filleted, cylinder.value()}) {
        const auto tight = boundingBox(s);
        const auto loose = approximateBoundingBox(s);
        ASSERT_TRUE(tight.valid && loose.valid);
        for (int axis = 0; axis < 3; ++axis) {
            const auto pick = [axis](const Vec3& v) { return axis == 0 ? v.x : axis == 1 ? v.y : v.z; };
            EXPECT_LE(pick(loose.min), pick(tight.min) + kTol);
            EXPECT_GE(pick(loose.max), pick(tight.max) - kTol);
        }
        EXPECT_LT(loose.size().length(), tight.size().length() * 1.25);
    }
    EXPECT_FALSE(approximateBoundingBox(Shape{}).valid);
}

// The tight box is cached per shape: repeated calls return the same value.
TEST(Geometry, BoundingBoxIsStablePerShape)
{
    const Shape s = filletEdges(box(30, 20, 10), verticalEdges(box(30, 20, 10)), 2.0).value();
    const auto first = boundingBox(s);
    const Shape copy = s; // shares the cached data
    const auto second = boundingBox(copy);
    EXPECT_TRUE(first.valid);
    EXPECT_EQ(first.min.x, second.min.x);
    EXPECT_EQ(first.max.z, second.max.z);
    EXPECT_NEAR(first.size().x, 30.0, 1e-5);
}

// What a step touched: a fillet on one edge creates a cylindrical face and
// trims its neighbours; the face opposite the edge is untouched.
TEST(Geometry, FacesChangedByAStep)
{
    const Shape before = box(30, 20, 10);
    const int edge = verticalEdges(before).front();
    const Shape after = filletEdges(before, {edge}, 2.0).value();
    const auto changed = facesChangedBy(before, after, after);
    ASSERT_FALSE(changed.empty());
    EXPECT_LT(changed.size(), std::size_t(after.faceCount()));
    int cylinders = 0;
    for (int f : changed)
        cylinders += faceInfo(after, f)->kind == SurfaceKind::Cylinder ? 1 : 0;
    EXPECT_EQ(cylinders, 1);
    // A base feature (no input) touched every face.
    EXPECT_EQ(facesChangedBy(Shape{}, before, before).size(), std::size_t(before.faceCount()));
    // Faces changed again later are no longer reported for the earlier step.
    const Shape moved = translated(after, {5, 0, 0}).value();
    EXPECT_TRUE(facesChangedBy(before, after, moved).empty());

    // New geometry only: just the fillet surface, not the trimmed neighbours.
    const auto created = facesCreatedBy(before, after, after);
    ASSERT_EQ(created.size(), 1u);
    EXPECT_EQ(faceInfo(after, created.front())->kind, SurfaceKind::Cylinder);
    // A move changes every face (translated() copies the geometry, so the
    // faces also count as created): the whole body is what a Move did.
    EXPECT_EQ(facesChangedBy(after, moved, moved).size(), std::size_t(moved.faceCount()));
    const auto createdByMove = facesCreatedBy(after, moved, moved);
    EXPECT_TRUE(createdByMove.empty() || createdByMove.size() == std::size_t(moved.faceCount()));
}

// Align a box's +X side face onto the top face of a bigger box: the side
// ends up facing down, touching, centered on the target face.
TEST(Geometry, AlignFaceToFaceRotatesAndTouches)
{
    const Shape small = box(10, 10, 10);
    const Shape big = makeBox({40, 0, 0}, {20, 20, 20}).value();
    const auto source = alignFrame(small, SubShapeKind::Face, faceWithNormal(small, {1, 0, 0}));
    const auto target = alignFrame(big, SubShapeKind::Face, faceWithNormal(big, {0, 0, 1}));
    ASSERT_TRUE(source && target);
    EXPECT_TRUE(source->sided && target->sided);

    const RigidMotion motion = alignMotion(*source, *target, false, 0.0);
    EXPECT_NEAR(motion.angle, kPi / 2, 1e-9);
    const Vec3 landed = motion.apply(source->point);
    EXPECT_NEAR((landed - target->point).length(), 0.0, 1e-9);
    const Shape moved = transformed(small, motion).value();
    const auto bb = boundingBox(moved);
    EXPECT_NEAR(bb.min.z, 20.0, 1e-6); // resting on the top face
    EXPECT_NEAR(bb.size().z, 10.0, 1e-6);
    EXPECT_NEAR(bb.center().x, 50.0, 1e-6);
    EXPECT_NEAR(bb.center().y, 10.0, 1e-6);
    EXPECT_NEAR(volume(moved), 1000.0, 1e-6);

    // Flip: flush instead of touching (same direction), and an offset lifts it.
    const auto flush = boundingBox(transformed(small, alignMotion(*source, *target, true, 0.0)).value());
    EXPECT_NEAR(flush.max.z, 20.0, 1e-6);
    const auto gap = boundingBox(transformed(small, alignMotion(*source, *target, false, 2.5)).value());
    EXPECT_NEAR(gap.min.z, 22.5, 1e-6);
}

// Circles align concentric: a peg's rim onto a rim whose axis is X.
TEST(Geometry, AlignCirclesConcentric)
{
    const Shape peg = makeCylinder({0, 0, 0}, {0, 0, 1}, 3.0, 10.0).value();
    const Shape tube = makeCylinder({20, 5, 5}, {1, 0, 0}, 3.0, 8.0).value();
    auto circleEdge = [](const Shape& s) {
        for (int i = 0; i < s.edgeCount(); ++i)
            if (const auto e = edgeInfo(s, i); e && e->kind == CurveKind::Circle)
                return i;
        return -1;
    };
    const auto source = alignFrame(peg, SubShapeKind::Edge, circleEdge(peg));
    const auto target = alignFrame(tube, SubShapeKind::Edge, circleEdge(tube));
    ASSERT_TRUE(source && target);
    EXPECT_FALSE(source->sided);
    const RigidMotion motion = alignMotion(*source, *target, false, 0.0);
    const Shape moved = transformed(peg, motion).value();
    const auto bb = boundingBox(moved);
    EXPECT_NEAR(bb.size().x, 10.0, 1e-6); // the peg's axis now runs along X
    EXPECT_NEAR(bb.center().y, 5.0, 1e-6);
    EXPECT_NEAR(bb.center().z, 5.0, 1e-6);
    // A cylinder face gives its axis too (holes and shafts).
    int side = -1;
    for (int i = 0; i < peg.faceCount(); ++i)
        if (faceInfo(peg, i)->kind == SurfaceKind::Cylinder)
            side = i;
    const auto axis = alignFrame(peg, SubShapeKind::Face, side);
    ASSERT_TRUE(axis);
    EXPECT_NEAR(std::abs(axis->direction.z), 1.0, 1e-9);
    EXPECT_NEAR(axis->point.x, 0.0, 1e-9);
    EXPECT_NEAR(axis->point.y, 0.0, 1e-9);
}

TEST(Geometry, MirrorJoinedMergesAcrossAFace)
{
    const Shape b = box(10, 10, 10);
    auto joined = mirrorJoined(b, {10, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(joined.ok()) << joined.developerMessage();
    EXPECT_EQ(joined.value().solidCount(), 1);
    EXPECT_EQ(joined.value().faceCount(), 6); // one clean 20 x 10 x 10 block
    EXPECT_NEAR(volume(joined.value()), 2000.0, 1e-6);
    EXPECT_NEAR(boundingBox(joined.value()).size().x, 20.0, 1e-6);
    // A plane away from the body leaves two pieces (reported, not an error).
    auto apart = mirrorJoined(b, {-5, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(apart.ok());
    EXPECT_EQ(apart.value().solidCount(), 2);
    EXPECT_FALSE(apart.warnings().empty());
    EXPECT_NEAR(boundingBox(apart.value()).min.x, -20.0, 1e-6);
}

TEST(Geometry, RepeatJoinedLinearAndCircular)
{
    const Shape b = box(10, 10, 10);
    auto apart = repeatJoined(b, {{{}, {0, 0, 1}, 0, {15, 0, 0}}, {{}, {0, 0, 1}, 0, {30, 0, 0}}});
    ASSERT_TRUE(apart.ok()) << apart.developerMessage();
    EXPECT_EQ(apart.value().solidCount(), 3);
    EXPECT_NEAR(volume(apart.value()), 3000.0, 1e-6);
    auto overlapping = repeatJoined(b, {{{}, {0, 0, 1}, 0, {5, 0, 0}}, {{}, {0, 0, 1}, 0, {10, 0, 0}}});
    ASSERT_TRUE(overlapping.ok());
    EXPECT_EQ(overlapping.value().solidCount(), 1);
    EXPECT_NEAR(volume(overlapping.value()), 2000.0, 1e-6);
    // A centered bar turned by 90 degrees: a plus sign.
    const Shape bar = makeBox({-10, -2, 0}, {20, 4, 4}).value();
    auto plus = repeatJoined(bar, {{{0, 0, 0}, {0, 0, 1}, kPi / 2, {}}});
    ASSERT_TRUE(plus.ok());
    EXPECT_NEAR(volume(plus.value()), 2 * 320.0 - 64.0, 1e-6);
}

namespace {
Shape plateWithHole()
{
    const Shape plate = box(40, 30, 10);
    auto pin = makeCylinder({20, 15, -1}, {0, 0, 1}, 3.0, 12.0);
    return booleanOp(plate, pin.value(), BooleanKind::Subtract).value();
}
int cylinderFace(const Shape& s, double radius)
{
    for (int i = 0; i < s.faceCount(); ++i)
        if (const auto f = faceInfo(s, i); f && f->kind == SurfaceKind::Cylinder && std::abs(f->radius - radius) < 1e-6)
            return i;
    return -1;
}
} // namespace

TEST(Geometry, DeleteFacesHealsHolesAndFillets)
{
    const Shape plate = plateWithHole();
    const int hole = cylinderFace(plate, 3.0);
    ASSERT_GE(hole, 0);
    auto healed = deleteFaces(plate, {hole});
    ASSERT_TRUE(healed.ok()) << healed.developerMessage();
    EXPECT_NEAR(volume(healed.value()), 40 * 30 * 10, 1e-6);
    EXPECT_EQ(healed.value().faceCount(), 6);

    const Shape rounded = filletEdges(box(60, 40, 20), verticalEdges(box(60, 40, 20)), 3.0).value();
    auto oneLess = deleteFaces(rounded, {cylinderFace(rounded, 3.0)});
    ASSERT_TRUE(oneLess.ok()) << oneLess.developerMessage();
    EXPECT_NEAR(volume(oneLess.value()) - volume(rounded), (9.0 - kPi * 9.0 / 4.0) * 20.0, 1e-6);

    // A box side cannot be healed away: refused, not a broken solid.
    EXPECT_FALSE(deleteFaces(box(10, 10, 10), {0}).ok());
}

TEST(Geometry, OffsetFaceResizesHolesAndRefusesWhatCannotFollow)
{
    const Shape plate = plateWithHole();
    const int hole = cylinderFace(plate, 3.0);
    // The hole wall's outward normal points into the hole: +0.2 shrinks it.
    auto smaller = offsetFace(plate, hole, 0.2);
    ASSERT_TRUE(smaller.ok()) << smaller.developerMessage();
    EXPECT_EQ(smaller.value().solidCount(), 1);
    EXPECT_NEAR(volume(smaller.value()) - volume(plate), kPi * (9.0 - 2.8 * 2.8) * 10, 1e-4);
    auto larger = offsetFace(plate, hole, -0.2);
    ASSERT_TRUE(larger.ok()) << larger.developerMessage();
    EXPECT_NEAR(volume(larger.value()) - volume(plate), -kPi * (3.2 * 3.2 - 9.0) * 10, 1e-4);

    const Shape b = box(20, 20, 20);
    auto taller = offsetFace(b, faceWithNormal(b, {0, 0, 1}), 5.0);
    ASSERT_TRUE(taller.ok()) << taller.developerMessage();
    EXPECT_NEAR(boundingBox(taller.value()).size().z, 25.0, 1e-6);

    // A flat face between tangent fillets cannot take them along: refused.
    const Shape rounded = filletEdges(box(40, 30, 10), verticalEdges(box(40, 30, 10)), 4.0).value();
    EXPECT_FALSE(offsetFace(rounded, faceWithNormal(rounded, {1, 0, 0}), 5.0).ok());
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

TEST(Geometry, MeasureWallAndAngles)
{
    const Shape s = box(60, 40, 30);
    auto sh = shell(s, {faceWithNormal(s, {0, 0, 1})}, 2.0);
    ASSERT_TRUE(sh.ok());
    const Shape& e = sh.value();
    // Outer and inner +X walls: parallel, 2 mm apart.
    int outer = -1, inner = -1;
    for (int i = 0; i < e.faceCount(); ++i) {
        const auto info = faceInfo(e, i);
        if (!info->isPlanar())
            continue;
        if (info->normal.x > 0.999 && info->centroid.x > 59)
            outer = i;
        if (info->normal.x < -0.999 && info->centroid.x > 50)
            inner = i;
    }
    ASSERT_GE(outer, 0);
    ASSERT_GE(inner, 0);
    const auto m = measure({&e, SubShapeKind::Face, outer}, {&e, SubShapeKind::Face, inner});
    ASSERT_TRUE(m.has_value());
    EXPECT_NEAR(m->distance, 2.0, 1e-7);
    ASSERT_TRUE(m->parallelGap.has_value());
    EXPECT_NEAR(*m->parallelGap, 2.0, 1e-7);
    EXPECT_NEAR(*m->angle, 0.0, 1e-9);

    // Top rim face vs outer wall: perpendicular, touching.
    const auto square = measure({&e, SubShapeKind::Face, outer}, {&e, SubShapeKind::Face, faceWithNormal(e, {0, 0, 1})});
    ASSERT_TRUE(square.has_value());
    EXPECT_NEAR(square->distance, 0.0, 1e-7);
    EXPECT_NEAR(*square->angle, kPi / 2, 1e-9);
}

TEST(Geometry, FaceThicknessBetweenParallelFaces)
{
    const Shape s = box(10, 20, 30);
    const struct {
        Vec3 normal;
        double expected;
    } cases[] = {{{0, 0, 1}, 30}, {{0, 0, -1}, 30}, {{1, 0, 0}, 10}, {{-1, 0, 0}, 10}, {{0, 1, 0}, 20}, {{0, -1, 0}, 20}};
    for (const auto& c : cases) {
        const int face = faceWithNormal(s, c.normal);
        ASSERT_GE(face, 0);
        const auto info = faceInfo(s, face);
        const auto t = faceThickness(s, face, info->centroid);
        ASSERT_TRUE(t.has_value());
        EXPECT_NEAR(t->distance, c.expected, kTol);
        EXPECT_EQ(t->oppositeFace, faceWithNormal(s, -c.normal));
        EXPECT_NEAR((t->to - t->from).length(), c.expected, kTol);
        EXPECT_NEAR((t->to - (info->centroid - c.normal * c.expected)).length(), 0.0, kTol);
    }
}

TEST(Geometry, FaceThicknessNeedsAParallelOppositeFace)
{
    // A deep chamfer on the bottom edge at x = 20: a line down from the middle
    // of the top face leaves through the slanted chamfer.
    const Shape s = box(20, 20, 20);
    int edge = -1;
    for (int i = 0; i < s.edgeCount(); ++i)
        if (const auto e = edgeInfo(s, i); e && std::abs(e->midpoint.x - 20) < kTol && std::abs(e->midpoint.z) < kTol)
            edge = i;
    ASSERT_GE(edge, 0);
    const auto chamfered = chamferEdges(s, {edge}, 15.0);
    ASSERT_TRUE(chamfered.ok()) << chamfered.developerMessage();
    const int top = faceWithNormal(chamfered.value(), {0, 0, 1});
    ASSERT_GE(top, 0);
    EXPECT_FALSE(faceThickness(chamfered.value(), top, {10, 10, 20}).has_value());
    // Beside the chamfer the line still reaches the flat bottom.
    const auto t = faceThickness(chamfered.value(), top, {2, 10, 20});
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(t->distance, 20.0, kTol);
}

TEST(Geometry, PointOnFaceAvoidsHoles)
{
    // 40 x 40 x 5 plate with a round hole in the middle: the top face's
    // centroid lies in the hole.
    const Shape plate = makeBox({-20, -20, 0}, {40, 40, 5}).value();
    const Shape pin = makeCylinder({0, 0, -1}, {0, 0, 1}, 10.0, 7.0).value();
    const Shape washer = booleanOp(plate, pin, BooleanKind::Subtract).value();
    const int top = faceWithNormal(washer, {0, 0, 1});
    ASSERT_GE(top, 0);
    const auto info = faceInfo(washer, top);
    ASSERT_TRUE(info.has_value());
    EXPECT_LT(std::hypot(info->centroid.x, info->centroid.y), 10.0) << "the centroid is in the hole";

    const auto p = pointOnFace(washer, top, info->centroid);
    ASSERT_TRUE(p.has_value());
    EXPECT_GT(std::hypot(p->x, p->y), 11.0) << "clear of the hole";
    EXPECT_LT(std::max(std::abs(p->x), std::abs(p->y)), 19.0) << "inside the outline";
    EXPECT_NEAR(p->z, 5.0, kTol);
    const auto t = faceThickness(washer, top, *p);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(t->distance, 5.0, kTol);

    // A preferred point already on the face is kept; curved faces are not handled.
    const auto kept = pointOnFace(washer, top, {15, 0, 5});
    ASSERT_TRUE(kept.has_value());
    EXPECT_NEAR((*kept - Vec3{15, 0, 5}).length(), 0.0, kTol);
    for (int i = 0; i < washer.faceCount(); ++i) {
        if (!faceInfo(washer, i)->isPlanar()) {
            EXPECT_FALSE(pointOnFace(washer, i, {0, 0, 0}).has_value());
        }
    }
}
