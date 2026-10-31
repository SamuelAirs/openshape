// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The view cube: taps on faces and corners at many camera poses, the label
// layout (which faces are named, how the square maps onto them), the edges,
// and the controller's tap and orbit (also with a sketch tool armed).
#include "commands/Command.h"
#include "core/Camera.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/ViewCube.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <tuple>
#include <vector>

using namespace os;
using namespace os::interact;

namespace {

Camera cameraAt(double yaw, double pitch)
{
    Camera c;
    c.yaw = yaw;
    c.pitch = pitch;
    c.viewportSize = {1000, 800};
    return c;
}

Camera standard(StandardView view)
{
    Camera c;
    c.viewportSize = {1000, 800};
    c.setStandardView(view);
    return c;
}

// A spread of poses: the isometric one, oblique ones from every quarter,
// above and below, and straight down / up.
std::vector<Camera> poses()
{
    std::vector<Camera> result{standard(StandardView::Isometric)};
    for (const double yaw : {-2.8, -1.9, -0.7, 0.3, 1.2, 2.5})
        for (const double pitch : {-1.1, -0.4, 0.2, 0.9})
            result.push_back(cameraAt(yaw, pitch));
    result.push_back(cameraAt(0.4, kPi / 2));
    result.push_back(cameraAt(-2.0, -kPi / 2));
    return result;
}

void expectNear(const Vec3& a, const Vec3& b, double tolerance, const std::string& what)
{
    EXPECT_NEAR(a.x, b.x, tolerance) << what;
    EXPECT_NEAR(a.y, b.y, tolerance) << what;
    EXPECT_NEAR(a.z, b.z, tolerance) << what;
}

const StandardView kFaceViews[] = {StandardView::Front, StandardView::Back, StandardView::Left,
                                   StandardView::Right, StandardView::Top,  StandardView::Bottom};

} // namespace

TEST(ViewCube, CenterTapAtEachStandardViewIsThatFace)
{
    for (const StandardView view : kFaceViews) {
        const ViewCubeTarget t = ViewCube::hit(standard(view), {0, 0});
        EXPECT_EQ(t.kind, ViewCubeTarget::Kind::Face);
        EXPECT_EQ(t.face, view);
        EXPECT_EQ(t.id(), static_cast<int>(view));
        // A face asks for its own standard view: the camera's backward there.
        expectNear(t.direction, standard(view).backward(), 1e-12, "face direction");
    }
}

TEST(ViewCube, EveryVisibleFaceCenterHitsThatFaceAtManyPoses)
{
    int checked = 0;
    for (const Camera& camera : poses()) {
        const ViewCubeLayout layout = ViewCube::layout(camera);
        for (const ViewCubeFace& face : layout.faces) {
            if (face.facing < 0.2)
                continue; // too foreshortened for a clean center
            const ViewCubeTarget t = ViewCube::hit(camera, ViewCube::project(camera, face.normal));
            EXPECT_EQ(t.kind, ViewCubeTarget::Kind::Face) << face.name;
            EXPECT_EQ(t.face, face.view) << face.name << " yaw " << camera.yaw << " pitch " << camera.pitch;
            expectNear(t.direction, face.normal, 1e-12, face.name);
            ++checked;
        }
    }
    EXPECT_GT(checked, 40);
}

TEST(ViewCube, EveryVisibleCornerHitsThatCornerAtManyPoses)
{
    int checked = 0;
    for (const Camera& camera : poses()) {
        const Vec3 back = camera.backward();
        for (const double x : {-1.0, 1.0}) {
            for (const double y : {-1.0, 1.0}) {
                for (const double z : {-1.0, 1.0}) {
                    const Vec3 corner{x, y, z};
                    // A point near the corner on each of its faces that the
                    // viewer sees well (in-face coordinates 0.85).
                    for (const Vec3 normal : {Vec3{x, 0, 0}, Vec3{0, y, 0}, Vec3{0, 0, z}}) {
                        if (normal.dot(back) < 0.2)
                            continue;
                        const Vec3 onFace = normal + (corner - normal) * 0.85;
                        const ViewCubeTarget t = ViewCube::hit(camera, ViewCube::project(camera, onFace));
                        EXPECT_EQ(t.kind, ViewCubeTarget::Kind::Corner);
                        expectNear(t.corner, corner, 0, "corner");
                        expectNear(t.direction, corner / std::sqrt(3.0), 1e-12, "corner direction");
                        EXPECT_EQ(t.id(), ViewCube::cornerId(corner));
                        ++checked;
                    }
                }
            }
        }
    }
    EXPECT_GT(checked, 60);
}

TEST(ViewCube, HeadOnFrontRegions)
{
    const Camera front = standard(StandardView::Front);
    const double edge = 1.0 / ViewCube::kViewHalfExtent; // the face's half width in box units
    // In-face 0.875 to the right: still the face (not in a corner square).
    EXPECT_EQ(ViewCube::hit(front, {0.875 * edge, 0}).face, StandardView::Front);
    // 0.875 right and 0.875 down: the lower right corner square, (+X, -Y, -Z).
    const ViewCubeTarget corner = ViewCube::hit(front, {0.875 * edge, 0.875 * edge});
    EXPECT_EQ(corner.kind, ViewCubeTarget::Kind::Corner);
    expectNear(corner.corner, {1, -1, -1}, 0, "lower right");
    EXPECT_EQ(corner.id(), 7);
    // Just inside the corner fraction is still the face.
    EXPECT_EQ(ViewCube::hit(front, {0.5 * edge, 0.5 * edge}).kind, ViewCubeTarget::Kind::Face);
    // Beside the cube: nothing.
    EXPECT_EQ(ViewCube::hit(front, {1.05 * edge, 0}).kind, ViewCubeTarget::Kind::None);
    EXPECT_EQ(ViewCube::hit(front, {0.95, 0.95}).kind, ViewCubeTarget::Kind::None);
    EXPECT_EQ(ViewCube::hit(front, {0.95, 0.95}).id(), -1);
}

TEST(ViewCube, LabelsAtHeadOnIsometricAndObliqueViews)
{
    // Head-on: only the face looked at is named, fully opaque.
    for (const StandardView view : kFaceViews) {
        const ViewCubeLayout layout = ViewCube::layout(standard(view));
        for (const ViewCubeFace& face : layout.faces) {
            if (face.view == view) {
                EXPECT_NEAR(face.facing, 1.0, 1e-12) << face.name;
                EXPECT_DOUBLE_EQ(face.labelOpacity, 1.0) << face.name;
            } else {
                EXPECT_EQ(face.labelOpacity, 0.0) << face.name;
            }
        }
    }
    // Isometric (front, right, above): Top, Front and Right are named alike.
    const ViewCubeLayout iso = ViewCube::layout(standard(StandardView::Isometric));
    for (const ViewCubeFace& face : iso.faces) {
        const bool seen = face.view == StandardView::Top || face.view == StandardView::Front || face.view == StandardView::Right;
        if (seen) {
            EXPECT_NEAR(face.facing, 1 / std::sqrt(3.0), 1e-12) << face.name;
            EXPECT_GT(face.labelOpacity, 0.6) << face.name;
            EXPECT_LT(face.labelOpacity, 0.8) << face.name;
        } else {
            EXPECT_LT(face.facing, 0) << face.name;
            EXPECT_EQ(face.labelOpacity, 0.0) << face.name;
        }
    }
    // Oblique: Right turned toward the viewer only 0.2 is drawn but not named.
    const ViewCubeLayout oblique = ViewCube::layout(cameraAt(-std::acos(0.2), 0));
    const ViewCubeFace& right = oblique.faces[static_cast<std::size_t>(StandardView::Right)];
    EXPECT_NEAR(right.facing, 0.2, 1e-12);
    EXPECT_EQ(right.labelOpacity, 0.0);
    const ViewCubeFace& front = oblique.faces[static_cast<std::size_t>(StandardView::Front)];
    EXPECT_NEAR(front.facing, std::sqrt(1 - 0.04), 1e-12);
    EXPECT_DOUBLE_EQ(front.labelOpacity, 1.0);
    // Just past the threshold: named, faintly.
    const ViewCubeLayout faint = ViewCube::layout(cameraAt(-std::acos(0.26), 0));
    EXPECT_DOUBLE_EQ(faint.faces[static_cast<std::size_t>(StandardView::Right)].labelOpacity, 0.25);
}

TEST(ViewCube, LabelSquareReadsUprightOnItsOwnView)
{
    const double e = 2.0 / ViewCube::kViewHalfExtent;
    for (const StandardView view : kFaceViews) {
        const ViewCubeFace face = ViewCube::layout(standard(view)).faces[static_cast<std::size_t>(view)];
        EXPECT_NEAR(face.origin.x, -e / 2, 1e-12) << face.name;
        EXPECT_NEAR(face.origin.y, -e / 2, 1e-12) << face.name;
        EXPECT_NEAR(face.across.x, e, 1e-12) << face.name;
        EXPECT_NEAR(face.across.y, 0, 1e-12) << face.name;
        EXPECT_NEAR(face.down.x, 0, 1e-12) << face.name;
        EXPECT_NEAR(face.down.y, e, 1e-12) << face.name;
    }
    // The four corners of Front's square: top-left is (-X, -Y, +Z), and so on.
    const ViewCubeFace front = ViewCube::layout(standard(StandardView::Front)).faces[0];
    EXPECT_EQ(front.cornerIds[0], ViewCube::cornerId({-1, -1, 1}));
    EXPECT_EQ(front.cornerIds[1], ViewCube::cornerId({1, -1, 1}));
    EXPECT_EQ(front.cornerIds[2], ViewCube::cornerId({1, -1, -1}));
    EXPECT_EQ(front.cornerIds[3], ViewCube::cornerId({-1, -1, -1}));
}

TEST(ViewCube, FaceSquareMapsOntoTheProjectedFace)
{
    // At any pose, the square's corners land on the projected cube corners
    // the corner ids name (the drawing and the taps agree).
    for (const Camera& camera : poses()) {
        const ViewCubeLayout layout = ViewCube::layout(camera);
        for (const ViewCubeFace& face : layout.faces) {
            const Vec2 squareCorners[4] = {face.origin, face.origin + face.across, face.origin + face.across + face.down,
                                           face.origin + face.down};
            for (int i = 0; i < 4; ++i) {
                const int id = face.cornerIds[static_cast<std::size_t>(i)] - 6;
                const Vec3 corner{(id & 1) ? 1.0 : -1.0, (id & 2) ? 1.0 : -1.0, (id & 4) ? 1.0 : -1.0};
                const Vec2 expected = ViewCube::project(camera, corner);
                EXPECT_NEAR(squareCorners[i].x, expected.x, 1e-12) << face.name;
                EXPECT_NEAR(squareCorners[i].y, expected.y, 1e-12) << face.name;
            }
        }
    }
}

TEST(ViewCube, EdgesSeenAndTheirAxes)
{
    auto visible = [](const ViewCubeLayout& layout) {
        int count = 0;
        bool axes[3] = {false, false, false};
        for (const ViewCubeEdge& edge : layout.edges) {
            if (edge.visible) {
                ++count;
                axes[edge.axis] = true;
            }
        }
        return std::tuple{count, axes[0], axes[1], axes[2]};
    };
    // Straight down: the top face's four edges, along X and Y.
    EXPECT_EQ(visible(ViewCube::layout(standard(StandardView::Top))), std::tuple(4, true, true, false));
    // Front: along X and Z.
    EXPECT_EQ(visible(ViewCube::layout(standard(StandardView::Front))), std::tuple(4, true, false, true));
    // Isometric: three faces, nine edges, every axis.
    EXPECT_EQ(visible(ViewCube::layout(standard(StandardView::Isometric))), std::tuple(9, true, true, true));
    // Every edge runs along its axis: its ends differ only there, by the cube's size.
    const Camera iso = standard(StandardView::Isometric);
    const ViewCubeLayout isoLayout = ViewCube::layout(iso);
    for (const ViewCubeEdge& edge : isoLayout.edges) {
        const Vec3 axis{edge.axis == 0 ? 2.0 : 0.0, edge.axis == 1 ? 2.0 : 0.0, edge.axis == 2 ? 2.0 : 0.0};
        const Vec2 span = ViewCube::project(iso, axis);
        EXPECT_NEAR(edge.to.x - edge.from.x, span.x, 1e-12);
        EXPECT_NEAR(edge.to.y - edge.from.y, span.y, 1e-12);
    }
}

TEST(ViewCube, FitsItsBox)
{
    auto reach = [](const Camera& camera) {
        double farthest = 0;
        for (const double x : {-1.0, 1.0})
            for (const double y : {-1.0, 1.0})
                for (const double z : {-1.0, 1.0}) {
                    const Vec2 p = ViewCube::project(camera, {x, y, z});
                    farthest = std::max({farthest, std::abs(p.x), std::abs(p.y)});
                }
        return farthest;
    };
    // Head-on with a margin, isometric just inside (its outline's corners are
    // sqrt(8/3) from the center).
    for (const StandardView view : kFaceViews)
        EXPECT_LT(reach(standard(view)), 0.65);
    EXPECT_NEAR(reach(standard(StandardView::Isometric)), std::sqrt(8.0 / 3.0) / ViewCube::kViewHalfExtent, 1e-12);
    EXPECT_LT(reach(standard(StandardView::Isometric)), 1.0);
    // Anywhere: at most 5 % past it (a corner sqrt(3) out, e.g. looking level
    // along a diagonal of the ground square).
    double worst = 0;
    for (int yaw = -180; yaw < 180; yaw += 5)
        for (int pitch = -90; pitch <= 90; pitch += 5)
            worst = std::max(worst, reach(cameraAt(yaw * kPi / 180, pitch * kPi / 180)));
    EXPECT_LE(worst, std::sqrt(3.0) / ViewCube::kViewHalfExtent + 1e-12);
    EXPECT_LT(worst, 1.05);
}

TEST(Camera, LookFromAnyDirection)
{
    Camera c;
    c.target = {5, 6, 7};
    c.distance = 123;
    c.lookFrom({1, 1, 1});
    expectNear(c.backward(), Vec3{1, 1, 1} / std::sqrt(3.0), 1e-12, "diagonal");
    c.lookFrom({-2, 3, -4});
    expectNear(c.backward(), Vec3{-2, 3, -4} / std::sqrt(29.0), 1e-12, "any length");
    EXPECT_DOUBLE_EQ(c.distance, 123);
    expectNear(c.target, {5, 6, 7}, 0, "the target stays");
    // Straight down keeps the yaw (and the screen's right).
    c.yaw = 0.7;
    c.lookFrom({0, 0, 5});
    EXPECT_DOUBLE_EQ(c.yaw, 0.7);
    EXPECT_NEAR(c.backward().z, 1.0, 1e-12);
    // A zero direction changes nothing.
    const double yaw = c.yaw, pitch = c.pitch;
    c.lookFrom({0, 0, 0});
    EXPECT_EQ(c.yaw, yaw);
    EXPECT_EQ(c.pitch, pitch);
}

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};

    Harness()
    {
        controller.setViewportSize({1200, 800});
        controller.fitAll(false);
    }
};

} // namespace

TEST(ViewCubeController, TapTopThenCornerAnimatesThere)
{
    Harness h;
    const Vec3 target = h.controller.camera().target;
    const double distance = h.controller.camera().distance;
    const Camera& camera = h.controller.camera();
    ASSERT_TRUE(h.controller.tapViewCube(ViewCube::project(camera, {0, 0, 1})));
    EXPECT_TRUE(h.controller.isAnimating());
    h.controller.skipAnimation();
    expectNear(h.controller.camera().backward(), {0, 0, 1}, 1e-9, "Top");
    EXPECT_NEAR(h.controller.camera().yaw, -kPi / 2, 1e-12); // as the View menu's Top
    // From Top, the corner (+X, +Y, +Z) near the top face's upper right.
    ASSERT_TRUE(h.controller.tapViewCube(ViewCube::project(h.controller.camera(), {0.85, 0.85, 1})));
    h.controller.skipAnimation();
    expectNear(h.controller.camera().backward(), Vec3{1, 1, 1} / std::sqrt(3.0), 1e-9, "corner");
    expectNear(h.controller.camera().target, target, 1e-9, "the target stays");
    EXPECT_NEAR(h.controller.camera().distance, distance, 1e-9);
    // Off the cube: nothing.
    const Camera before = h.controller.camera();
    EXPECT_FALSE(h.controller.tapViewCube({0.99, -0.99}));
    EXPECT_FALSE(h.controller.isAnimating());
    EXPECT_EQ(h.controller.camera().yaw, before.yaw);
    EXPECT_EQ(h.controller.camera().pitch, before.pitch);
}

TEST(ViewCubeController, OrbitWithTheLineToolArmedAddsNothing)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    const auto& sketch = h.controller.sketchSession()->sketch();
    const std::size_t points = sketch.points().size(), lines = sketch.lines().size();
    const double yaw = h.controller.camera().yaw, pitch = h.controller.camera().pitch;
    const Vec3 target = h.controller.camera().target;
    // 80 px right: the view turns 0.64 rad about Z (Camera::orbit's rate) around its center.
    h.controller.orbitBy(80, 0);
    EXPECT_NEAR(std::remainder(h.controller.camera().yaw - yaw, 2 * kPi), -0.64, 1e-12);
    EXPECT_NEAR(h.controller.camera().pitch, pitch, 1e-12);
    expectNear(h.controller.camera().target, target, 1e-9, "orbits around the view's center");
    // 50 px down: lower (the sketch view looks straight down: the pitch drops).
    h.controller.orbitBy(0, -50);
    EXPECT_NEAR(h.controller.camera().pitch, pitch - 0.4, 1e-12);
    ASSERT_NE(h.controller.sketchSession(), nullptr);
    EXPECT_EQ(h.controller.sketchSession()->tool(), SketchTool::Line);
    EXPECT_EQ(sketch.points().size(), points);
    EXPECT_EQ(sketch.lines().size(), lines);
    // A tap on the cube in the sketch too: Top, still sketching.
    ASSERT_TRUE(h.controller.tapViewCube(ViewCube::project(h.controller.camera(), {0, 0, 1})));
    h.controller.skipAnimation();
    expectNear(h.controller.camera().backward(), {0, 0, 1}, 1e-9, "Top");
    EXPECT_EQ(h.controller.sketchSession()->tool(), SketchTool::Line);
    EXPECT_EQ(sketch.lines().size(), lines);
}
