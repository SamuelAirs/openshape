// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// What the user reads when an operation cannot be done: plain language that
// says what went wrong and what to try, never kernel jargon and never
// nothing. An operation that would change nothing says so instead of
// quietly adding a step that does nothing.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "interaction/InteractionController.h"
#include "interaction/Operation.h"
#include "io/ProjectFile.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

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

// The size in "... Try 2.9 mm or less." (or "in"), or a negative value.
double suggestedSize(const std::string& text, const char* unit = "mm")
{
    const auto at = text.find("Try ");
    double value = -1;
    const std::string pattern = std::string("Try %lf ") + unit + " or less.";
    if (at == std::string::npos || std::sscanf(text.c_str() + at, pattern.c_str(), &value) != 1)
        return -1;
    return value;
}

// Interactive previews ask for a size that works.
constexpr geom::SizeAdvice kSuggest{true};

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
    auto r = geom::filletEdges(s, {edge}, 20.0, kSuggest);
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
    EXPECT_EQ(geom::filletEdges(s, {edge}, 25.0, kSuggest).userMessage(), r.userMessage());
}

TEST(FailureMessages, ChamferTooLargeNamesADistanceThatWorks)
{
    const geom::Shape s = box(10, 10, 10);
    auto r = geom::chamferEdges(s, verticalEdges(s), 9.0, kSuggest);
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
    auto r = geom::shell(s, {top}, 12.0, kSuggest);
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

    // Dragged in beside the body, the automatic choice becomes a new body
    // (as a join that misses does): nothing is refused.
    auto op = interact::ExtrudeOperation::create(scene.document, sketchId, {scene.profileAt(sketchId, {40, 10})}, {40, 10, 10});
    ASSERT_NE(op, nullptr);
    op->setValue(-5, scene.document);
    EXPECT_EQ(op->mode(), doc::ExtrudeMode::NewBody);
    EXPECT_TRUE(op->error().empty()) << op->error();
    EXPECT_TRUE(op->canCommit());
    ASSERT_TRUE(scene.stack.push(op->makeCommand(scene.document), scene.document).ok());
    ASSERT_EQ(scene.document.bodies().size(), 2u);
    const auto pin = geom::boundingBox(scene.document.bodies().back()->shape());
    EXPECT_NEAR(pin.min.z, 5.0, 1e-6);
    EXPECT_NEAR(pin.max.z, 10.0, 1e-6);
    EXPECT_NEAR(geom::volume(scene.document.bodies().back()->shape()), kPi * 9 * 5, 1e-3);
    ASSERT_TRUE(scene.stack.undo(scene.document));

    // Chosen explicitly, a cut that misses is refused with the reason.
    op->setModeOverride(doc::ExtrudeMode::Cut);
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

// ---- Sizes named in refusals ----------------------------------------------------

namespace {

// The top front edge along X of a box `height` tall.
int topFrontEdge(const geom::Shape& s, double height)
{
    return edgeWhere(s, [height](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(e.tangent.x) > 0.99 && e.midpoint.z > height - 0.1
            && e.midpoint.y < 0.1;
    });
}

} // namespace

TEST(FailureMessages, FarTooLargeSizesStillNameOneThatWorks)
{
    // A slip of the finger: 2000 for a radius whose limit is the 8 mm height.
    const geom::Shape s = box(20, 12, 8);
    const int edge = topFrontEdge(s, 8);
    ASSERT_GE(edge, 0);
    auto r = geom::filletEdges(s, {edge}, 2000.0, kSuggest);
    ASSERT_FALSE(r.ok());
    EXPECT_TRUE(plain(r.userMessage()));
    EXPECT_TRUE(r.userMessage().starts_with("The radius is too large for this edge. Try ")) << r.userMessage();
    const double suggestion = suggestedSize(r.userMessage());
    EXPECT_GT(suggestion, 7.0) << r.userMessage();
    EXPECT_LE(suggestion, 8.0);
    EXPECT_TRUE(geom::filletEdges(s, {edge}, suggestion).ok());
    // The cached answer serves a smaller refused value too.
    EXPECT_EQ(geom::filletEdges(s, {edge}, 25.0, kSuggest).userMessage(), r.userMessage());

    // Walls 100x too thick: the limit is half the 20 mm width.
    const geom::Shape cube = box(20, 20, 20);
    const int top = faceWithNormal(cube, {0, 0, 1});
    auto shelled = geom::shell(cube, {top}, 1000.0, kSuggest);
    ASSERT_FALSE(shelled.ok());
    EXPECT_TRUE(shelled.userMessage().starts_with("The walls are too thick for this body. Try ")) << shelled.userMessage();
    const double wall = suggestedSize(shelled.userMessage());
    EXPECT_GT(wall, 8.0) << shelled.userMessage();
    EXPECT_LT(wall, 10.0);
    EXPECT_TRUE(geom::shell(cube, {top}, wall).ok());
}

TEST(FailureMessages, SuggestedSizesUseTheDocumentUnit)
{
    const geom::Shape s = box(20, 12, 8);
    const int edge = topFrontEdge(s, 8);
    ASSERT_GE(edge, 0);
    auto r = geom::filletEdges(s, {edge}, 20.0, geom::SizeAdvice{true, LengthUnit::Inch});
    ASSERT_FALSE(r.ok());
    const double inches = suggestedSize(r.userMessage(), "in");
    ASSERT_GT(inches, 0) << r.userMessage();
    EXPECT_GT(inches * 25.4, 7.0) << r.userMessage();
    EXPECT_LE(inches * 25.4, 8.0);
    EXPECT_TRUE(geom::filletEdges(s, {edge}, inches * 25.4).ok());

    // Through the document: a preview names the size in the display unit;
    // the history recompute (a commit without preview) keeps the general
    // wording and makes no extra kernel attempts.
    Scene scene;
    scene.document.setDisplayUnit(LengthUnit::Inch);
    const Uuid body = scene.addBox({0, 0, 0}, {20, 12, 8});
    const geom::Shape& shape = scene.document.body(body)->shape();
    const int top = topFrontEdge(shape, 8);
    ASSERT_GE(top, 0);
    auto fillet = std::make_unique<doc::FilletFeature>();
    fillet->edges = {{top, *geom::captureEdgeSignature(shape, top)}};
    fillet->size = 20;
    const auto preview = scene.document.preview(body, *fillet);
    ASSERT_FALSE(preview.ok());
    EXPECT_GT(suggestedSize(preview.userMessage(), "in"), 0) << preview.userMessage();
    const Status status = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(fillet)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.userMessage(), "Unable to create this fillet. Try a smaller radius.");
}

TEST(FailureMessages, SizeSearchStaysWithinItsLimits)
{
    using namespace std::chrono_literals;
    int calls = 0;
    const auto upTo = [&calls](double limit) {
        return [&calls, limit](double size) {
            ++calls;
            return size <= limit;
        };
    };
    // 100x too large: the geometric steps get there within the attempts.
    auto largest = geom::detail::largestWorkingSize(2000, 0ms, upTo(19.7));
    ASSERT_TRUE(largest.has_value());
    EXPECT_GT(*largest, 19.0);
    EXPECT_LE(*largest, 19.7);
    EXPECT_LE(calls, geom::detail::kMaxSizeAttempts);
    // A small limit is found too.
    largest = geom::detail::largestWorkingSize(25, 0ms, upTo(0.3));
    ASSERT_TRUE(largest.has_value());
    EXPECT_GT(*largest, 0.2);
    EXPECT_LE(*largest, 0.3);
    // Nothing works even at 0.1 mm: "at any size".
    largest = geom::detail::largestWorkingSize(25, 0ms, upTo(0.05));
    ASSERT_TRUE(largest.has_value());
    EXPECT_EQ(*largest, 0.0);
    // One attempt takes longer than the budget: no search, no suggestion.
    calls = 0;
    largest = geom::detail::largestWorkingSize(25, 700ms, upTo(10));
    EXPECT_FALSE(largest.has_value());
    EXPECT_EQ(calls, 0);
    // Slow attempts: the budget ends the search before it is close, and a
    // size far below the limit is not suggested.
    calls = 0;
    const auto start = std::chrono::steady_clock::now();
    largest = geom::detail::largestWorkingSize(100, 0ms, [&calls](double size) {
        ++calls;
        std::this_thread::sleep_for(150ms);
        return size <= 30;
    });
    EXPECT_FALSE(largest.has_value());
    EXPECT_LE(calls, 4);
    EXPECT_LT(std::chrono::steady_clock::now() - start, 1000ms);
}

// ---- Steps that change nothing ---------------------------------------------------

namespace {

std::filesystem::path tempProject(const std::string& stem)
{
    return std::filesystem::temp_directory_path() / ("openshape_" + stem + "_" + Uuid::generate().toString() + ".openshape");
}

// A circle of radius `r` at `center` on the top of a body `top` high, in a
// sketch hosted by the body.
Uuid circleOnTop(Scene& scene, const Uuid& body, double top, Vec2 center, double r)
{
    sketch::Sketch s(Uuid::generate(), sketch::Plane::fromNormal({0, 0, top}, {0, 0, 1}));
    s.setHostBody(body);
    s.addCircle(s.addPoint(center), r);
    return scene.addSketch(s);
}

std::unique_ptr<doc::ExtrudeFeature> cutAt(Scene& scene, const Uuid& sketchId, Vec2 inside, double distance)
{
    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = sketchId;
    cut->profiles = {scene.profileAt(sketchId, inside)};
    cut->distance = distance;
    cut->mode = doc::ExtrudeMode::Cut;
    return cut;
}

// A 2 mm fillet on the vertical edge at the origin corner.
std::unique_ptr<doc::FilletFeature> cornerFillet(const geom::Shape& s)
{
    const int e = edgeWhere(s, [](const geom::EdgeInfo& info) {
        return std::abs(std::abs(info.tangent.z) - 1) < 1e-9 && std::abs(info.midpoint.x) < 1e-9
            && std::abs(info.midpoint.y) < 1e-9;
    });
    EXPECT_GE(e, 0);
    auto f = std::make_unique<doc::FilletFeature>();
    f->edges = {{e, *geom::captureEdgeSignature(s, e)}};
    f->size = 2;
    return f;
}

constexpr double kCornerFillet = (4 - kPi) * 10; // what a 2 mm round takes off a 10 mm tall corner

} // namespace

// An upstream edit moves a cut off the body: the cut becomes a warning that
// passes its input on, and the steps after it still build (they used to stop).
TEST(FailureMessages, ACutThatStopsCuttingDoesNotBlockLaterSteps)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    const Uuid sketchId = circleOnTop(scene, body, 10, {15, 10}, 3);
    ASSERT_TRUE(
        scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, cutAt(scene, sketchId, {15, 10}, -5)), scene.document).ok());
    const doc::Body& b = *scene.document.body(body);
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, cornerFillet(b.shape())), scene.document).ok());
    const double cutVolume = kPi * 9 * 5;
    ASSERT_NEAR(geom::volume(b.shape()), 4000 - cutVolume - kCornerFillet, 1e-3);

    // Narrow the box to 10 mm: the circle at x = 12..18 now misses it.
    const Uuid boxId = b.features()[0]->id();
    const Uuid cutId = b.features()[1]->id();
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "width", 10.0, false), scene.document).ok());
    EXPECT_EQ(b.state(1).status, doc::FeatureStatus::Ok);
    EXPECT_EQ(b.state(1).error, ErrorCode::NoEffect);
    EXPECT_TRUE(plain(b.state(1).note)) << b.state(1).note;
    EXPECT_EQ(b.state(2).status, doc::FeatureStatus::Ok) << b.state(2).userMessage;
    EXPECT_FALSE(b.hasFailures());
    EXPECT_NEAR(geom::volume(b.shape()), 2000 - kCornerFillet, 1e-3);
    EXPECT_NEAR(geom::boundingBox(b.shape()).size().x, 10, 1e-6);

    // The Model panel marks the cut with a warning, not a failure.
    interact::InteractionController controller(scene.document, scene.stack);
    const auto rows = controller.historyRows();
    const auto cutRow = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.id == cutId; });
    ASSERT_NE(cutRow, rows.end());
    EXPECT_EQ(cutRow->status, interact::HistoryRow::Status::Warning);
    EXPECT_EQ(cutRow->message, b.state(1).note);

    // Dragging the cut itself to a value that still misses is refused.
    const Status drag = scene.stack.push(std::make_unique<cmd::SetParameterCommand>(cutId, "distance", -3.0), scene.document);
    EXPECT_FALSE(drag.ok());
    EXPECT_EQ(drag.error(), ErrorCode::NoEffect);
    EXPECT_NEAR(*b.features()[1]->parameter("distance"), -5.0, 1e-12);

    // Undo: the cut cuts again and the warning is gone.
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_EQ(b.state(1).error, ErrorCode::None);
    EXPECT_TRUE(b.state(1).note.empty());
    EXPECT_NEAR(geom::volume(b.shape()), 4000 - cutVolume - kCornerFillet, 1e-3);
}

// Older versions could save steps that change nothing (a cut beside the
// body, a subtraction of a body that does not touch): such files open with
// every later step built.
TEST(FailureMessages, AFileWithAStepThatChangesNothingStillBuildsTheStepsAfterIt)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    const Uuid apart = scene.addBox({40, 0, 0}, {10, 10, 10});
    const Uuid sketchId = circleOnTop(scene, body, 10, {30, 10}, 3);
    // Inserted directly: the commands refuse such steps now.
    const doc::FeatureState& cut = scene.document.insertFeature(body, cutAt(scene, sketchId, {30, 10}, -5));
    EXPECT_EQ(cut.status, doc::FeatureStatus::Ok);
    EXPECT_EQ(cut.error, ErrorCode::NoEffect);
    auto subtract = std::make_unique<doc::CombineFeature>();
    subtract->toolBody = apart;
    subtract->mode = doc::CombineMode::Subtract;
    EXPECT_EQ(scene.document.insertFeature(body, std::move(subtract)).error, ErrorCode::NoEffect);
    const doc::FeatureState& fillet = scene.document.insertFeature(body, cornerFillet(scene.document.body(body)->shape()));
    ASSERT_EQ(fillet.status, doc::FeatureStatus::Ok) << fillet.userMessage;
    EXPECT_NEAR(geom::volume(scene.document.body(body)->shape()), 4000 - kCornerFillet, 1e-3);

    const auto path = tempProject("noeffect");
    ASSERT_TRUE(io::saveProject(scene.document, path).ok());
    auto loaded = io::loadProject(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(loaded.ok()) << loaded.userMessage();
    const doc::Body& b = *loaded.value()->body(body);
    ASSERT_EQ(b.features().size(), 4u);
    EXPECT_EQ(b.state(1).error, ErrorCode::NoEffect);
    EXPECT_EQ(b.state(2).error, ErrorCode::NoEffect);
    EXPECT_TRUE(plain(b.state(1).note)) << b.state(1).note;
    EXPECT_TRUE(plain(b.state(2).note)) << b.state(2).note;
    EXPECT_EQ(b.state(3).status, doc::FeatureStatus::Ok) << b.state(3).userMessage;
    EXPECT_FALSE(b.hasFailures());
    EXPECT_NEAR(geom::volume(b.shape()), 4000 - kCornerFillet, 1e-3);
}

// A heat-set insert on a hole that already fits it: nothing to drill, and
// the message says so (not that the hole misses the body).
TEST(FailureMessages, InsertOnAHoleThatAlreadyFitsSaysSo)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    const Uuid sketchId = circleOnTop(scene, body, 10, {5, 10}, 2.5); // a 5 mm hole
    sketch::Sketch both = *scene.document.sketch(sketchId);
    both.addCircle(both.addPoint({15, 10}), 1.5); // and a 3 mm one
    scene.document.replaceSketch(both);
    auto cut = cutAt(scene, sketchId, {5, 10}, -8);
    cut->profiles.push_back(scene.profileAt(sketchId, {15, 10}));
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(cut)), scene.document).ok());
    const doc::Body& b = *scene.document.body(body);
    const auto insertAt = [&](double rimRadius) {
        const int e = edgeWhere(b.shape(), [rimRadius](const geom::EdgeInfo& info) {
            return info.kind == geom::CurveKind::Circle && std::abs(info.radius - rimRadius) < 1e-6 && info.center.z > 9.9;
        });
        EXPECT_GE(e, 0);
        auto hole = std::make_unique<doc::HoleFeature>();
        hole->rim = {e, *geom::captureEdgeSignature(b.shape(), e)};
        hole->diameter = 4.0; // M3
        hole->depth = 6.0;
        return hole;
    };
    const double before = geom::volume(b.shape());

    const Status refused = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, insertAt(2.5)), scene.document);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(refused.userMessage()));
    EXPECT_EQ(refused.userMessage(), "This hole is already wide and deep enough for the insert, so nothing would change. "
                                     "Pick a smaller hole, or a larger insert.");
    EXPECT_NEAR(geom::volume(b.shape()), before, 1e-6);

    // The 3 mm hole is widened to 4 mm, 6 mm deep.
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, insertAt(1.5)), scene.document).ok());
    EXPECT_NEAR(before - geom::volume(b.shape()), kPi * (4 - 2.25) * 6, 1e-3);
}

// A join must add material: one pushed into the body (Join chosen, then
// dragged in) used to commit a step that changed nothing, with no message.
TEST(FailureMessages, AJoinThatStaysInsideTheBodyIsRefused)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    const Uuid sketchId = circleOnTop(scene, body, 10, {10, 10}, 3);
    auto join = cutAt(scene, sketchId, {10, 10}, -5);
    join->mode = doc::ExtrudeMode::Join;
    const std::size_t steps = scene.stack.size();
    const Status status = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(join)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error(), ErrorCode::NoEffect);
    EXPECT_EQ(status.userMessage(), "This extrusion stays inside the body, so nothing would be added. Pull it outward, or choose Cut.");
    EXPECT_TRUE(plain(status.userMessage()));
    EXPECT_EQ(scene.stack.size(), steps);
    EXPECT_NEAR(geom::volume(scene.document.body(body)->shape()), 4000, 1e-6);

    // Outward it adds the boss; an edit (or an older file) that moves it into
    // the body leaves a warning that passes the body on.
    auto boss = cutAt(scene, sketchId, {10, 10}, 5);
    boss->mode = doc::ExtrudeMode::Join;
    const Uuid bossId = boss->id();
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(boss)), scene.document).ok());
    const doc::Body& b = *scene.document.body(body);
    EXPECT_NEAR(geom::volume(b.shape()), 4000 + kPi * 9 * 5, 1e-3);
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::SetParameterCommand>(bossId, "distance", -5.0, false), scene.document).ok());
    EXPECT_EQ(b.state(1).status, doc::FeatureStatus::Ok);
    EXPECT_EQ(b.state(1).error, ErrorCode::NoEffect);
    EXPECT_EQ(b.state(1).note, status.userMessage());
    EXPECT_FALSE(b.hasFailures());
    EXPECT_NEAR(geom::volume(b.shape()), 4000, 1e-6);
}

// The same for a revolve whose solid lies inside the body.
TEST(FailureMessages, ARevolveJoinThatStaysInsideTheBodyIsRefused)
{
    Scene scene;
    const Uuid body = scene.addBox({0, 0, 0}, {20, 20, 10});
    // A 2 x 2 square beside the sketch's Y axis, on a plane through the
    // box's middle: turned about the axis it is a small cylinder inside.
    sketch::Sketch s(Uuid::generate(), sketch::Plane::fromNormal({10, 10, 5}, {0, 1, 0}));
    s.setHostBody(body);
    sketch::addRectangle(s, {0, -1}, {2, 1});
    const Uuid sketchId = scene.addSketch(s);
    auto revolve = std::make_unique<doc::RevolveFeature>();
    revolve->sketchId = sketchId;
    revolve->profiles = {scene.profileAt(sketchId, {1, 0})};
    revolve->mode = doc::ExtrudeMode::Join;
    const Status status = scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(revolve)), scene.document);
    ASSERT_FALSE(status.ok());
    EXPECT_EQ(status.error(), ErrorCode::NoEffect);
    EXPECT_TRUE(plain(status.userMessage())) << status.userMessage();
    EXPECT_NEAR(geom::volume(scene.document.body(body)->shape()), 4000, 1e-6);
}
