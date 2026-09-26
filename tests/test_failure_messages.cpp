// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the user reads when an operation cannot be done: plain language that
// says what went wrong and what to try, never kernel jargon and never
// nothing. An operation that would change nothing says so instead of
// quietly adding a step that does nothing.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "interaction/Operation.h"

#include <cstdio>

using namespace os;
using namespace os::test;

namespace {

::testing::AssertionResult plain(const std::string& text)
{
    if (text.empty())
        return ::testing::AssertionFailure() << "no message";
    for (const char* jargon : {"BRep", "OCC", "Standard_", "kernel", "Kernel", "boolean", "Boolean", "solid", "null", "not done",
                               "threw", "0x", "Fuse", "Algo"})
        if (text.find(jargon) != std::string::npos)
            return ::testing::AssertionFailure() << "jargon '" << jargon << "' in: " << text;
    if (text.back() != '.')
        return ::testing::AssertionFailure() << "not a sentence: " << text;
    return ::testing::AssertionSuccess();
}

// The size in "... Try 2.9 mm or less.", or a negative value.
double suggestedSize(const std::string& text)
{
    const auto at = text.find("Try ");
    double value = -1;
    if (at == std::string::npos || std::sscanf(text.c_str() + at, "Try %lf mm or less.", &value) != 1)
        return -1;
    return value;
}

geom::Shape box(double x, double y, double z, Vec3 origin = {})
{
    return geom::makeBox(origin, {x, y, z}).value();
}

int edgeWhere(const geom::Shape& s, const std::function<bool(const geom::EdgeInfo&)>& pick)
{
    for (int i = 0; i < s.edgeCount(); ++i)
        if (const auto info = geom::edgeInfo(s, i); info && pick(*info))
            return i;
    return -1;
}

// A body plus a sketch through commands, like the UI builds them.
struct Scene {
    doc::Document document;
    cmd::UndoStack stack;

    Uuid addBox(Vec3 origin, Vec3 size)
    {
        auto f = std::make_unique<doc::BoxFeature>();
        f->origin = origin;
        f->size = size;
        auto command = std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), std::move(f));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(stack.push(std::move(command), document).ok());
        return id;
    }

    Uuid addSketch(sketch::Sketch s)
    {
        const Uuid id = s.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(s)), document).ok());
        return id;
    }

    doc::ProfileRef profileAt(const Uuid& sketchId, Vec2 p)
    {
        const sketch::Sketch& s = *document.sketch(sketchId);
        auto regions = doc::sketchRegions(s);
        EXPECT_TRUE(regions.ok());
        for (const auto& r : regions.value())
            if (geom::regionContains(r.face, s.plane().toWorld(p)))
                return doc::makeProfileRef(r, s);
        ADD_FAILURE() << "no region at " << p.x << ", " << p.y;
        return {};
    }
};

} // namespace

TEST(FailureMessages, FilletTooLargeNamesARadiusThatWorks)
{
    const geom::Shape s = box(20, 12, 8);
    // A top edge along X: its faces are 12 and 8 mm wide.
    const int edge = edgeWhere(s, [](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(e.tangent.x) > 0.99 && e.midpoint.z > 7.9 && e.midpoint.y < 0.1;
    });
    ASSERT_GE(edge, 0);
    auto r = geom::filletEdges(s, {edge}, 20.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::FilletRadiusTooLarge);
    EXPECT_TRUE(plain(r.userMessage()));
    EXPECT_TRUE(r.userMessage().starts_with("The radius is too large for this edge. Try ")) << r.userMessage();
    const double suggestion = suggestedSize(r.userMessage());
    // The limit is the 8 mm height; the suggestion is just under it and works.
    EXPECT_GT(suggestion, 7.0);
    EXPECT_LE(suggestion, 8.0);
    EXPECT_TRUE(geom::filletEdges(s, {edge}, suggestion).ok());
    // Asking again (a drag keeps asking) gives the same answer at once.
    EXPECT_EQ(geom::filletEdges(s, {edge}, 25.0).userMessage(), r.userMessage());
}

TEST(FailureMessages, ChamferTooLargeNamesADistanceThatWorks)
{
    const geom::Shape s = box(10, 10, 10);
    auto r = geom::chamferEdges(s, verticalEdges(s), 9.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::ChamferTooLarge);
    EXPECT_TRUE(plain(r.userMessage()));
    EXPECT_TRUE(r.userMessage().starts_with("The distance is too large for these edges. Try ")) << r.userMessage();
    const double suggestion = suggestedSize(r.userMessage());
    EXPECT_GT(suggestion, 4.0);
    EXPECT_LE(suggestion, 5.0);
    EXPECT_TRUE(geom::chamferEdges(s, verticalEdges(s), suggestion).ok());
}

TEST(FailureMessages, FilletOnASmoothEdgeSaysThereIsNoCorner)
{
    // Round a vertical edge, then try to round the seam between that round
    // and a flat side: the faces meet tangentially.
    const geom::Shape s = box(20, 20, 10);
    const int corner = verticalEdges(s).front();
    const geom::Shape rounded = geom::filletEdges(s, {corner}, 4.0).value();
    const int seam = edgeWhere(rounded, [](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.z) - 1) < 1e-9;
    });
    ASSERT_GE(seam, 0);
    // Pick a vertical seam edge next to the cylindrical face.
    int smooth = -1;
    for (int e = 0; e < rounded.edgeCount() && smooth < 0; ++e) {
        const auto info = geom::edgeInfo(rounded, e);
        if (!info || info->kind != geom::CurveKind::Line || std::abs(std::abs(info->tangent.z) - 1) > 1e-9)
            continue;
        for (int f : geom::facesOfEdge(rounded, e))
            if (geom::faceInfo(rounded, f)->kind == geom::SurfaceKind::Cylinder)
                smooth = e;
    }
    ASSERT_GE(smooth, 0);
    auto r = geom::filletEdges(rounded, {smooth}, 1.0);
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(plain(r.userMessage()));
    EXPECT_EQ(r.userMessage(), "This edge joins two faces smoothly, so there is no corner to round. Select sharp edges only.");
}

TEST(FailureMessages, ShellTooThickNamesAWallThatWorks)
{
    const geom::Shape s = box(20, 20, 20);
    const int top = faceWithNormal(s, {0, 0, 1});
    auto r = geom::shell(s, {top}, 12.0);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::ShellTooThick);
    EXPECT_TRUE(plain(r.userMessage()));
    EXPECT_TRUE(r.userMessage().starts_with("The walls are too thick for this body. Try ")) << r.userMessage();
    const double suggestion = suggestedSize(r.userMessage());
    // Walls meet at 10 mm (half the 20 mm width).
    EXPECT_GT(suggestion, 8.0);
    EXPECT_LT(suggestion, 10.0);
    auto hollow = geom::shell(s, {top}, suggestion);
    ASSERT_TRUE(hollow.ok()) << hollow.developerMessage();
    EXPECT_LT(geom::volume(hollow.value()), 8000.0);
}

TEST(FailureMessages, CutThatMissesTheBodyIsRefused)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    // A circle on the top plane, beside the body: pushing it down cuts air.
    sketch::Sketch s(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
    s.setHostBody(body);
    s.addCircle(s.addPoint({40, 10}), 3);
    const Uuid sketchId = scene.addSketch(s);
    const double volume = geom::volume(scene.document.body(body)->shape());

    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = sketchId;
    cut->profiles = {scene.profileAt(sketchId, {40, 10})};
    cut->distance = -5;
    cut->mode = doc::ExtrudeMode::Cut;
    const std::size_t steps = scene.stack.size();
    const Status status = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(cut)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(status.userMessage()));
    EXPECT_EQ(status.userMessage(), "This cut does not reach the body, so nothing would be removed. Extrude toward the body, or further.");
    EXPECT_EQ(scene.stack.size(), steps);
    EXPECT_EQ(scene.document.body(body)->features().size(), 1u);
    EXPECT_NEAR(geom::volume(scene.document.body(body)->shape()), volume, 1e-6);

    // The live preview says the same and cannot be applied.
    auto op = interact::ExtrudeOperation::create(scene.document, sketchId, {scene.profileAt(sketchId, {40, 10})}, {40, 10, 10});
    ASSERT_NE(op, nullptr);
    op->setValue(-5, scene.document);
    EXPECT_EQ(op->mode(), doc::ExtrudeMode::Cut);
    EXPECT_EQ(op->error(), status.userMessage());
    EXPECT_FALSE(op->canCommit());
}

TEST(FailureMessages, BooleansOfBodiesThatDoNotTouch)
{
    const geom::Shape a = box(10, 10, 10);
    const geom::Shape b = box(10, 10, 10, {30, 0, 0});
    // Subtract: nothing to cut away.
    auto subtract = geom::booleanOp(a, b, geom::BooleanKind::Subtract);
    ASSERT_FALSE(subtract.ok());
    EXPECT_EQ(subtract.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(subtract.userMessage()));
    // Intersect: nothing left.
    auto intersect = geom::booleanOp(a, b, geom::BooleanKind::Intersect);
    ASSERT_FALSE(intersect.ok());
    EXPECT_EQ(intersect.userMessage(), "These bodies do not overlap, so nothing would be left.");
    // Union works, but says the body is now in two pieces.
    auto join = geom::booleanOp(a, b, geom::BooleanKind::Union);
    ASSERT_TRUE(join.ok());
    EXPECT_NEAR(geom::volume(join.value()), 2000.0, 1e-6);
    ASSERT_EQ(join.warnings().size(), 1u);
    EXPECT_EQ(join.warnings().front(), "The body is now in 2 separate pieces.");

    // Through the Combine step, the wording is about bodies.
    Scene scene;
    const Uuid first = scene.addBox({0, 0, 0}, {10, 10, 10});
    const Uuid second = scene.addBox({30, 0, 0}, {10, 10, 10});
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = second;
    combine->mode = doc::CombineMode::Subtract;
    const Status status = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(first, std::move(combine)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.userMessage(), "The bodies do not overlap, so nothing would be cut away. Move one into the other first.");
    EXPECT_EQ(scene.document.body(first)->features().size(), 1u);
}

TEST(FailureMessages, ExtrudingAnOpenOutlineSaysToCloseIt)
{
    Scene scene;
    // Three sides of a square: nothing closed.
    sketch::Sketch s(Uuid::generate(), sketch::Plane::xy());
    const auto p0 = s.addPoint({0, 0}), p1 = s.addPoint({10, 0}), p2 = s.addPoint({10, 10}), p3 = s.addPoint({0, 10});
    s.addLine(p0, p1);
    s.addLine(p1, p2);
    s.addLine(p2, p3);
    const Uuid sketchId = scene.addSketch(s);
    auto regions = doc::sketchRegions(*scene.document.sketch(sketchId));
    ASSERT_TRUE(regions.ok());
    EXPECT_TRUE(regions.value().empty());

    auto extrude = std::make_unique<doc::ExtrudeFeature>();
    extrude->sketchId = sketchId;
    extrude->profiles = {doc::ProfileRef{{5, 5}, 100}};
    extrude->distance = 10;
    const Status status = scene.stack.push(std::make_unique<cmd::CreateBodyCommand>("Body", std::move(extrude)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_TRUE(plain(status.userMessage()));
    EXPECT_EQ(status.userMessage(), "The sketch has no closed shape to extrude. Close its outline first.");
    EXPECT_TRUE(scene.document.bodies().empty());
}

TEST(FailureMessages, PatternsAndMirrorsThatChangeNothing)
{
    const geom::Shape block = box(10, 6, 4);

    // Overlapping copies merge into one piece: that is a valid pattern.
    doc::PatternFeature overlap;
    overlap.count = 3;
    overlap.direction = {1, 0, 0};
    overlap.spacing = 5; // half the width
    auto merged = overlap.compute(block, {});
    ASSERT_TRUE(merged.ok()) << merged.developerMessage();
    EXPECT_NEAR(geom::volume(merged.value()), (10 + 5 + 5) * 6 * 4, 1e-6);
    EXPECT_EQ(merged.value().solidCount(), 1);
    EXPECT_TRUE(merged.warnings().empty());

    // No spacing: every copy on the original.
    doc::PatternFeature stacked = overlap;
    stacked.spacing = 0;
    auto none = stacked.compute(block, {});
    ASSERT_FALSE(none.ok());
    EXPECT_EQ(none.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(none.userMessage()));

    // A round body turned about its own axis lands on itself.
    const geom::Shape pin = geom::makeCylinder({0, 0, 0}, {0, 0, 1}, 3, 10).value();
    doc::PatternFeature turned;
    turned.layout = doc::PatternFeature::Layout::Circular;
    turned.count = 4;
    turned.axisOrigin = {0, 0, 0};
    turned.axis = {0, 0, 1};
    auto same = turned.compute(pin, {});
    ASSERT_FALSE(same.ok());
    EXPECT_EQ(same.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(same.userMessage()));

    // Mirroring a symmetric block across its middle changes nothing.
    auto mirror = geom::mirrorJoined(block, {5, 3, 2}, {1, 0, 0});
    ASSERT_FALSE(mirror.ok());
    EXPECT_EQ(mirror.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(mirror.userMessage()));
    // Across its side it doubles.
    auto doubled = geom::mirrorJoined(block, {10, 0, 0}, {1, 0, 0});
    ASSERT_TRUE(doubled.ok());
    EXPECT_NEAR(geom::volume(doubled.value()), 2 * 240.0, 1e-6);
}

TEST(FailureMessages, OtherRefusalsArePlain)
{
    const geom::Shape s = box(10, 10, 10);
    const int top = faceWithNormal(s, {0, 0, 1});
    std::vector<std::string> messages{
        geom::pushPullFace(s, top, -20).userMessage(),               // pushed through the whole body
        geom::makeBox({}, {0, 1, 1}).userMessage(),                  // zero size
        geom::filletEdges(s, {}, 1).userMessage(),                   // nothing selected
        geom::filletEdges(s, {999}, 1).userMessage(),                // stale edge
        geom::shell(s, {}, 1).userMessage(),                         // no open face
        geom::deleteFaces(s, {top}).userMessage(),                   // a box face cannot be healed away
        geom::offsetFace(s, 999, 1).userMessage(),                   // stale face
        geom::booleanOp(s, geom::Shape(), geom::BooleanKind::Union).userMessage(),
    };
    for (const auto& m : messages)
        EXPECT_TRUE(plain(m)) << m;
}
