// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Construction axes and planes through the interaction layer: the Axis and
// Plane tools with each of their modes, selecting them, the Model panel, and
// the tools that use them (Sketch, Pattern, Rotate, Mirror, Align).
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;

    Harness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
    }
    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left, PointerDevice device = PointerDevice::Mouse)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        e.device = device;
        return e;
    }
    void hover(Vec2 p) { controller.pointerMove(at(p, PointerButton::None)); }
    void clickAt(Vec2 p, PointerDevice device = PointerDevice::Mouse)
    {
        controller.pointerPress(at(p, PointerButton::Left, device));
        controller.pointerRelease(at(p, PointerButton::Left, device));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    Uuid addBody(const std::string& name, std::unique_ptr<doc::Feature> base)
    {
        auto create = std::make_unique<cmd::CreateBodyCommand>(name, std::move(base));
        const Uuid id = create->bodyId();
        EXPECT_TRUE(stack.push(std::move(create), document).ok());
        controller.documentChanged();
        return id;
    }
    Uuid addBox(const std::string& name, Vec3 origin, Vec3 size)
    {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = origin;
        box->size = size;
        return addBody(name, std::move(box));
    }
    // A 20 x 20 x 5 plate from (10, 10, 0) with a hole of radius 3 at (20, 20).
    Uuid addHoledPlate()
    {
        auto feature = std::make_unique<doc::ImportedFeature>();
        feature->setShape(geom::booleanOp(geom::makeBox({10, 10, 0}, {20, 20, 5}).value(),
                                          geom::makeCylinder({20, 20, -1}, {0, 0, 1}, 3.0, 7.0).value(),
                                          geom::BooleanKind::Subtract)
                              .value());
        return addBody("Plate", std::move(feature));
    }
    const DatumOperation* construct() const { return dynamic_cast<const DatumOperation*>(controller.operation()); }
    bool offers(const std::string& id) const
    {
        for (const auto& action : controller.contextActions())
            if (action.id == id)
                return true;
        return false;
    }
    geom::BoundingBox box(const Uuid& body) const { return geom::boundingBox(document.body(body)->shape()); }
};

void expectVec(const Vec3& actual, const Vec3& expected, const char* what)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-9) << what;
    EXPECT_NEAR(actual.y, expected.y, 1e-9) << what;
    EXPECT_NEAR(actual.z, expected.z, 1e-9) << what;
}

} // namespace

// The Plane tool: a face, a typed distance, Enter; the new plane is selected
// and offers Sketch; the sketch follows the plane, and so does what is
// extruded from it when the box grows.
TEST(DatumInteraction, OffsetPlaneThenSketchAndExtrude)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // -10..10, -10..10, 0..20
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.runTool("plane").ok());
    ASSERT_NE(h.construct(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Plane");
    EXPECT_FALSE(h.controller.operation()->prompt().empty());
    EXPECT_TRUE(h.offers("datum:origin:2"));
    h.clickAt(h.screen({0, 0, 20})); // the top face
    ASSERT_TRUE(h.construct()->preview().has_value());
    EXPECT_EQ(h.controller.operation()->handleCount(), 1); // the distance arrow
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Distance");
    EXPECT_EQ(h.controller.setValueText("10"), "");
    expectVec(h.construct()->preview()->origin, {0, 0, 30}, "previewed 10 above the top");
    EXPECT_TRUE(h.document.datums().empty()); // a preview only
    EXPECT_TRUE(h.controller.keyPress(Key::Enter));

    ASSERT_EQ(h.document.datums().size(), 1u);
    const doc::Datum& plane = *h.document.datums().front();
    EXPECT_EQ(plane.name(), "Plane 1");
    expectVec(plane.geometry().origin, {0, 0, 30}, "the plane");
    expectVec(plane.geometry().direction, {0, 0, 1}, "facing up");
    ASSERT_EQ(h.controller.selection().size(), 1u);
    EXPECT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Datum);
    EXPECT_NE(h.controller.selectionSummary().find("Plane 1"), std::string::npos);
    EXPECT_TRUE(h.offers("sketch"));
    EXPECT_TRUE(h.offers("hideDatum"));

    // Sketch on it: a 10 x 8 rectangle, then extrude it 5 mm.
    ASSERT_TRUE(h.controller.triggerAction("sketch").ok());
    ASSERT_NE(h.controller.sketchSession(), nullptr);
    h.controller.skipAnimation();
    const Uuid sketchId = h.controller.sketchSession()->sketchId();
    EXPECT_EQ(h.document.sketch(sketchId)->datumPlane(), plane.id());
    EXPECT_NEAR(h.document.sketch(sketchId)->plane().origin.z, 30.0, 1e-9);
    const sketch::Plane sp = h.document.sketch(sketchId)->plane();
    auto sketchAt = [&](Vec2 local) { return h.screen(sp.toWorld(local)); };
    h.clickAt(sketchAt({-5, -4}));
    h.hover(sketchAt({5, 4}));
    h.clickAt(sketchAt({5, 4}));
    ASSERT_EQ(h.document.sketch(sketchId)->lines().size(), 4u);
    h.controller.finishSketch();
    h.controller.skipAnimation();
    h.controller.fitAll(false);
    h.clickAt(h.screen(sp.toWorld({0, 0})));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::SketchProfile);
    EXPECT_EQ(h.controller.setValueText("5"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.bodies().size(), 2u);
    const Uuid block = h.document.bodies().back()->id();
    EXPECT_NEAR(geom::volume(h.document.body(block)->shape()), 10.0 * 8.0 * 5.0, 1e-6);
    EXPECT_NEAR(h.box(block).min.z, 30.0, 1e-6);
    EXPECT_NEAR(h.box(block).max.z, 35.0, 1e-6);

    // The box grows to 30 mm: plane, sketch and block move up 10.
    const Uuid boxStep = h.document.bodies().front()->features().front()->id();
    ASSERT_TRUE(h.controller.setFeatureParameter(boxStep, "height", "30").ok());
    EXPECT_NEAR(h.document.datums().front()->geometry().origin.z, 40.0, 1e-9);
    EXPECT_NEAR(h.document.sketch(sketchId)->plane().origin.z, 40.0, 1e-9);
    EXPECT_NEAR(h.box(block).min.z, 40.0, 1e-6);
    // The plane's distance in the Model panel.
    bool row = false;
    for (const auto& r : h.controller.historyRows())
        if (r.kind == HistoryRow::Kind::Datum) {
            row = true;
            EXPECT_EQ(r.name, "Plane 1");
            EXPECT_EQ(r.detail, "10.00 mm from a face");
            ASSERT_EQ(r.parameters.size(), 1u);
            EXPECT_EQ(r.parameters[0].key, "distance");
        }
    EXPECT_TRUE(row);
    ASSERT_TRUE(h.controller.setFeatureParameter(plane.id(), "distance", "2").ok());
    EXPECT_NEAR(h.box(block).min.z, 32.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.box(block).min.z, 40.0, 1e-6);
}

// Planes from an origin plane, at an angle through an edge, and midway
// between two faces; Esc steps back one pick, then leaves the tool.
TEST(DatumInteraction, PlaneModes)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {20, 20, 10});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.runTool("plane").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:origin:1").ok()); // from XZ
    ASSERT_TRUE(h.construct()->preview().has_value());
    EXPECT_EQ(h.controller.setValueText("-5"), "");
    expectVec(h.construct()->preview()->origin, {0, -5, 0}, "XZ moved to y = -5");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.document.datums().back()->method, doc::DatumMethod::PlaneOffset);
    EXPECT_EQ(h.document.datums().back()->originIndex, 1);

    // At an angle: the top face's front edge (y = 0, z = 10); the top face is
    // taken as the face the angle is measured from; 90 degrees stands it up.
    h.controller.selectBody(a, false);
    h.controller.cancelOperation(); // clear the selection
    ASSERT_TRUE(h.controller.runTool("plane").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:angle").ok());
    EXPECT_EQ(h.construct()->mode(), DatumOperation::Mode::PlaneAngle);
    h.clickAt(h.screen({10, 0, 10}));
    ASSERT_EQ(h.construct()->picked().size(), 2u) << (h.messages.empty() ? "" : h.messages.back());
    ASSERT_TRUE(h.construct()->preview().has_value());
    EXPECT_TRUE(h.controller.operation()->isAngle());
    EXPECT_NEAR(h.controller.operation()->value(), 45.0, 1e-9);
    const double s = std::sqrt(0.5);
    expectVec(h.construct()->preview()->direction, {0, -s, s}, "45 degrees back over the top");
    EXPECT_EQ(h.controller.setValueText("90"), "");
    EXPECT_NEAR(std::abs(h.construct()->preview()->direction.y), 1.0, 1e-9);
    // Esc drops the face, then the edge; a third Esc leaves the tool.
    h.controller.keyPress(Key::Escape);
    EXPECT_EQ(h.construct()->picked().size(), 1u);
    h.controller.keyPress(Key::Escape);
    EXPECT_TRUE(h.construct()->picked().empty());
    h.controller.keyPress(Key::Escape);
    EXPECT_EQ(h.construct(), nullptr);

    // Midway between the top and bottom faces (the bottom is clicked from below).
    ASSERT_TRUE(h.controller.runTool("plane").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:midway").ok());
    h.clickAt(h.screen({10, 10, 10})); // top
    h.clickAt(h.screen({2, 10, 10}));  // the same face again: not parallel? it is, and replaces nothing yet
    ASSERT_EQ(h.construct()->picked().size(), 2u);
    h.controller.keyPress(Key::Escape); // drop the second pick
    h.controller.setStandardView(StandardView::Front, false);
    h.clickAt(h.screen({20, 10, 5})); // the +X side face: not parallel to the top
    EXPECT_EQ(h.construct()->picked().size(), 1u);
    ASSERT_FALSE(h.messages.empty());
    EXPECT_EQ(h.messages.back(), "Pick a face parallel to the first one.");
    // The bottom, through the midway action's own pick in code (a view from below is not needed).
    const geom::Shape& shape = h.document.body(a)->shape();
    int bottom = -1;
    for (int i = 0; i < shape.faceCount(); ++i)
        if (geom::faceInfo(shape, i)->normal.z < -0.999)
            bottom = i;
    ASSERT_TRUE(const_cast<DatumOperation*>(h.construct())->pick(h.document, a, geom::SubShapeKind::Face, bottom, {10, 10, 0}).ok());
    ASSERT_TRUE(h.construct()->preview().has_value());
    expectVec(h.construct()->preview()->origin, {10, 10, 5}, "midway");
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    EXPECT_EQ(h.document.datums().size(), 2u);
    EXPECT_EQ(h.document.datums().back()->method, doc::DatumMethod::PlaneMidway);
}

// The Axis tool: along an edge, through two corners, parallel to Z through a
// corner, through a hole (the hole's rim selected first).
TEST(DatumInteraction, AxisModes)
{
    Harness h;
    h.addBox("Body 1", {0, 0, 0}, {20, 20, 10});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.runTool("axis").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Axis");
    h.clickAt(h.screen({20, 0, 5})); // the front right vertical edge
    ASSERT_TRUE(h.construct()->preview().has_value());
    expectVec(h.construct()->preview()->origin, {20, 0, 5}, "along the edge");
    EXPECT_NEAR(std::abs(h.construct()->preview()->direction.z), 1.0, 1e-12);
    EXPECT_TRUE(h.offers("apply"));
    ASSERT_TRUE(h.controller.triggerAction("apply").ok());
    ASSERT_EQ(h.document.datums().size(), 1u);
    EXPECT_EQ(h.document.datums().back()->method, doc::DatumMethod::AxisAlongEdge);
    EXPECT_EQ(h.document.datums().back()->name(), "Axis 1");

    // Two points: the top face's front edge near its left end (0, 0, 10), then
    // its right edge near the back (20, 20, 10).
    h.controller.cancelOperation();
    ASSERT_TRUE(h.controller.runTool("axis").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:twoPoints").ok());
    h.clickAt(h.screen({3, 0, 10}));
    h.clickAt(h.screen({20, 17, 10}));
    ASSERT_TRUE(h.construct()->preview().has_value()) << (h.messages.empty() ? "" : h.messages.back());
    expectVec(h.construct()->preview()->origin, {10, 10, 10}, "between the corners");
    const Vec3 d = h.construct()->preview()->direction;
    EXPECT_NEAR(std::abs(d.x), std::sqrt(0.5), 1e-9);
    EXPECT_NEAR(std::abs(d.y), std::sqrt(0.5), 1e-9);
    ASSERT_TRUE(h.controller.commitOperation().ok());

    // Parallel to Z through the corner (20, 20, 10).
    h.controller.cancelOperation();
    ASSERT_TRUE(h.controller.runTool("axis").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:parallel:2").ok());
    h.clickAt(h.screen({20, 17, 10}));
    ASSERT_TRUE(h.construct()->preview().has_value());
    expectVec(h.construct()->preview()->origin, {20, 20, 10}, "through the corner");
    expectVec(h.construct()->preview()->direction, {0, 0, 1}, "parallel to Z");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.document.datums().size(), 3u);

    // A clicked flat face does not make an axis: it says what does.
    h.controller.cancelOperation();
    ASSERT_TRUE(h.controller.runTool("axis").ok());
    h.clickAt(h.screen({10, 10, 10}));
    EXPECT_FALSE(h.construct()->preview().has_value());
    EXPECT_EQ(h.messages.back(), "An axis goes through a hole, a shaft or a circle, or along a straight edge.");
}

// An axis through a hole; a body patterned around it (clicking the axis),
// then rotated about it; a hole aligned onto it.
TEST(DatumInteraction, AxisThroughHoleForPatternRotateAlign)
{
    Harness h;
    const Uuid plate = h.addHoledPlate();
    const Uuid block = h.addBox("Block", {35, 18, 0}, {4, 4, 4});
    h.controller.fitAll(false);
    h.clickAt(h.screen({20, 17, 5})); // the hole's rim
    ASSERT_EQ(h.controller.selection().items().at(0).kind, sel::SelectionKind::Edge);
    ASSERT_TRUE(h.controller.runTool("axis").ok()); // the selected rim is its pick
    ASSERT_TRUE(h.construct()->preview().has_value());
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    ASSERT_EQ(h.document.datums().size(), 1u);
    const doc::Datum& axis = *h.document.datums().front();
    EXPECT_EQ(axis.method, doc::DatumMethod::AxisThrough);
    EXPECT_NEAR(axis.geometry().origin.x, 20.0, 1e-9);
    EXPECT_NEAR(axis.geometry().origin.y, 20.0, 1e-9);
    const Vec2 onAxis = h.screen({20, 20, 14}); // above the plate, on the dashed line
    h.controller.cancelOperation();

    // Pattern the block around it: 6 copies, a full turn.
    ASSERT_TRUE(h.controller.selectBody(block, false).ok());
    ASSERT_TRUE(h.controller.runTool("pattern").ok());
    ASSERT_TRUE(h.controller.triggerAction("layout:circular").ok());
    h.hover(onAxis);
    EXPECT_EQ(h.controller.hover().kind, sel::PickKind::Datum);
    h.clickAt(onAxis);
    const auto* pattern = dynamic_cast<const PatternOperation*>(h.controller.operation());
    ASSERT_NE(pattern, nullptr);
    EXPECT_EQ(pattern->axisIndex(), -1); // the picked axis
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::volume(h.document.body(block)->shape()), 6 * 64.0, 1e-6);
    EXPECT_NEAR(h.box(block).center().x, 20.0, 1e-6); // around the hole
    EXPECT_NEAR(h.box(block).center().y, 20.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());

    // Rotate the block a quarter turn about it.
    ASSERT_TRUE(h.controller.selectBody(block, false).ok());
    ASSERT_TRUE(h.controller.runTool("rotate").ok());
    h.clickAt(onAxis);
    const auto* rotate = dynamic_cast<const RotateOperation*>(h.controller.operation());
    ASSERT_NE(rotate, nullptr);
    ASSERT_TRUE(rotate->axis().has_value());
    EXPECT_NEAR(std::abs(rotate->axis()->z), 1.0, 1e-12);
    EXPECT_EQ(h.controller.setValueText("90"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.box(block).center().x, 20.0, 1e-6); // (37, 20) turned about (20, 20)
    EXPECT_NEAR(h.box(block).center().y, 37.0, 1e-6);
    EXPECT_TRUE(h.controller.undo());

    // Align the block's top face onto... the axis (its normal along the axis,
    // centered on it).
    h.controller.cancelOperation();
    h.controller.cancelOperation();
    h.clickAt(h.screen({37, 20, 4})); // the block's top
    ASSERT_EQ(h.controller.selection().items().at(0).kind, sel::SelectionKind::Face);
    ASSERT_TRUE(h.controller.triggerAction("align").ok());
    h.clickAt(onAxis);
    const auto* align = dynamic_cast<const AlignOperation*>(h.controller.operation());
    ASSERT_NE(align, nullptr);
    EXPECT_EQ(align->datumTarget(), axis.id());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.box(block).center().x, 20.0, 1e-6);
    EXPECT_NEAR(h.box(block).center().y, 20.0, 1e-6);
    (void)plate;
}

// Mirror across a construction plane (clicked); hide, show and delete it
// from the Model panel; Delete on a selected plane; undo brings it back.
TEST(DatumInteraction, MirrorAcrossPlaneAndModelPanel)
{
    Harness h;
    const Uuid a = h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.runTool("plane").ok());
    ASSERT_TRUE(h.controller.triggerAction("datum:origin:0").ok()); // from YZ
    EXPECT_EQ(h.controller.setValueText("15"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const Uuid planeId = h.document.datums().back()->id();
    h.controller.cancelOperation();
    h.controller.fitAll(false);

    ASSERT_TRUE(h.controller.selectBody(a, false).ok());
    ASSERT_TRUE(h.controller.runTool("mirror").ok());
    // Click the plane's outline: its corner lines are at x = 15.
    const auto shape = h.controller.datumShape(h.document.datum(planeId)->geometry(), doc::DatumKind::Plane);
    const Vec3 edgeMiddle = (shape.corners[1] + shape.corners[2]) * 0.5;
    h.clickAt(h.screen(edgeMiddle));
    const auto* mirror = dynamic_cast<const MirrorOperation*>(h.controller.operation());
    ASSERT_NE(mirror, nullptr);
    ASSERT_TRUE(mirror->hasPlane());
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(geom::volume(h.document.body(a)->shape()), 2000.0, 1e-6);
    EXPECT_NEAR(h.box(a).max.x, 30.0, 1e-6); // mirrored across x = 15

    // Hide / show / delete as the Model panel does.
    ASSERT_TRUE(h.controller.setDatumVisible(planeId, false).ok());
    EXPECT_FALSE(h.document.datum(planeId)->isVisible());
    ASSERT_TRUE(h.controller.setDatumVisible(planeId, true).ok());
    ASSERT_TRUE(h.controller.selectDatum(planeId).ok());
    EXPECT_EQ(h.controller.selection().items().at(0).kind, sel::SelectionKind::Datum);
    EXPECT_TRUE(h.controller.keyPress(Key::Delete));
    EXPECT_EQ(h.document.datum(planeId), nullptr);
    EXPECT_TRUE(h.controller.undo());
    ASSERT_NE(h.document.datum(planeId), nullptr);
    ASSERT_TRUE(h.controller.deleteDatum(planeId).ok());
    EXPECT_TRUE(h.document.datums().empty());
}

// A finger tapping empty space gives up an incomplete Axis tool (no Esc).
TEST(DatumInteraction, TapOnEmptySpaceGivesUp)
{
    Harness h;
    h.addBox("Body 1", {0, 0, 0}, {10, 10, 10});
    h.controller.fitAll(false);
    ASSERT_TRUE(h.controller.runTool("axis").ok());
    h.clickAt({20, 20}, PointerDevice::Touch);
    EXPECT_EQ(h.construct(), nullptr);
    EXPECT_TRUE(h.document.datums().empty());
}
