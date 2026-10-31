// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Values typed into sketch dimensions, the value box and the keypad, checked
// against the cases of OpenShape3D's keypad tests (NumericKeypadTests.swift,
// DimensionKeypadCommitTests): units, arithmetic, bad input, the wrong kind
// of unit, counts, reopening a stored dimension and the seeded text. Their
// test ideas, our code; where OpenShape differs on purpose the comment says
// why. Every check measures: lengths, constraint counts, history steps.
#include "commands/Command.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/NumericKeypad.h"

#include <gtest/gtest.h>

#include <cmath>
#include <initializer_list>
#include <string>

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
        controller.fitAll(false);
    }

    SketchSession& session() { return *controller.sketchSession(); }
    const sketch::Sketch& sketch() { return session().sketch(); }

    static PointerEvent at(Vec2 p, PointerButton button = PointerButton::Left)
    {
        PointerEvent e;
        e.position = p;
        e.button = button;
        return e;
    }
    Vec2 screen(Vec2 local) const
    {
        return controller.camera().project(controller.sketchSession()->sketch().plane().toWorld(local));
    }
    void click(Vec2 local)
    {
        controller.pointerMove(at(screen(local), PointerButton::None));
        controller.pointerPress(at(screen(local)));
        controller.pointerRelease(at(screen(local)));
    }
    void move(Vec2 local) { controller.pointerMove(at(screen(local), PointerButton::None)); }

    // A sketch with one free line from (0, 0) to (length, 0), selected.
    sketch::EntityId line(double length = 40)
    {
        EXPECT_TRUE(controller.startSketch().ok());
        controller.skipAnimation();
        controller.setSketchTool(SketchTool::Line);
        click({0, 0});
        click({length, 0});
        EXPECT_TRUE(controller.keyPress(Key::Escape)); // the chain ends
        controller.setSketchTool(SketchTool::Select);
        EXPECT_EQ(sketch().lines().size(), 1u);
        const sketch::EntityId id = sketch().lines().begin()->first;
        session().select(id, false);
        EXPECT_NEAR(lengthOf(id), length, 1e-9);
        return id;
    }
    double lengthOf(sketch::EntityId id)
    {
        const auto* l = sketch().line(id);
        return (sketch().point(l->end)->position - sketch().point(l->start)->position).length();
    }
    std::size_t dimensions()
    {
        std::size_t n = 0;
        for (const auto& [id, c] : sketch().constraints())
            n += c.isDimension() ? 1 : 0;
        return n;
    }
    sketch::EntityId onlyDimension()
    {
        sketch::EntityId found = sketch::kNoEntity;
        for (const auto& [id, c] : sketch().constraints())
            if (c.isDimension())
                found = id;
        return found;
    }
    // The label the dimension editor opens with (SketchOverlay.qml seeds it
    // with the label's text, less the Ø, R and ° signs).
    std::string labelText(SketchLabel::Kind kind, sketch::EntityId id)
    {
        for (const auto& label : session().labels(controller.camera()))
            if (label.kind == kind && (kind == SketchLabel::Kind::Size ? label.entity : label.constraint) == id)
                return label.text;
        return "(none)";
    }
};

} // namespace

// ---- Units -------------------------------------------------------------------------

// OpenShape3D: testATypedUnitBeatsTheDisplayUnit, testExplicitInches/Feet...,
// testMixedImperialConversion..., testWithoutASuffixTheDisplayUnitStillApplies.
TEST(TypedValues, ATypedUnitBeatsTheDisplayUnitOnASketchDimension)
{
    struct Case {
        LengthUnit display;
        const char* text;
        double millimeters;
    };
    for (const Case& c : {Case{LengthUnit::Millimeter, "20 cm", 200.0}, Case{LengthUnit::Inch, "20 cm", 200.0},
                          Case{LengthUnit::Millimeter, "0.05 in", 1.27}, Case{LengthUnit::Centimeter, "0.05 in", 1.27},
                          Case{LengthUnit::Millimeter, "0.0025 ft", 0.762}, Case{LengthUnit::Centimeter, "0.0025 ft", 0.762},
                          Case{LengthUnit::Millimeter, "0.00125 ft + 0.025 in", 1.016},
                          Case{LengthUnit::Inch, "0.1 cm + 0.2 mm", 1.2}, Case{LengthUnit::Millimeter, "1 cm - 2 mm", 8.0},
                          Case{LengthUnit::Millimeter, "1' 2\"", 355.6}, Case{LengthUnit::Centimeter, "20", 200.0},
                          Case{LengthUnit::Inch, "2", 50.8}, Case{LengthUnit::Meter, "0.03", 30.0}}) {
        SCOPED_TRACE(std::string(c.text) + " in " + std::string(unitSymbol(c.display)));
        Harness h;
        const sketch::EntityId line = h.line(); // drawn in mm: exactly 40
        h.document.setDisplayUnit(c.display);
        EXPECT_EQ(h.session().setDimension(line, c.text), "");
        EXPECT_NEAR(h.lengthOf(line), c.millimeters, 1e-9);
        ASSERT_EQ(h.dimensions(), 1u);
        EXPECT_NEAR(h.sketch().constraint(h.onlyDimension())->value, c.millimeters, 1e-9);
        // One step, and undo gives the line back as drawn.
        EXPECT_EQ(h.stack.undoLabel(), "Length");
        EXPECT_TRUE(h.controller.undo());
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        EXPECT_EQ(h.dimensions(), 0u);
    }
}

// ---- Bad input -----------------------------------------------------------------------

// OpenShape3D: testInvalidLengthInputsPreserveGeometryAndAllowRecovery and
// testFreeSmallLineAcceptsRecoveredArithmeticLength. (They close the editor
// with a notice for 0 and -1; ours keeps it open with the message for
// every refusal, so each is corrected the same way.)
TEST(TypedValues, InvalidInputLeavesTheSketchAloneAndCanBeCorrected)
{
    for (const char* raw : {"0", "-1", "", "1/0", "2+", "12+", "unknown_dimension", "5 furlongs", "(3"}) {
        SCOPED_TRACE(raw);
        Harness h;
        const sketch::EntityId line = h.line();
        const std::size_t steps = h.stack.index();
        EXPECT_NE(h.session().setDimension(line, raw), "") << "refused with a message";
        EXPECT_EQ(h.stack.index(), steps) << "no history step";
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        EXPECT_EQ(h.dimensions(), 0u);
        // Corrected, it is taken.
        EXPECT_EQ(h.session().setDimension(line, "12+1"), "");
        EXPECT_NEAR(h.lengthOf(line), 13.0, 1e-9);
        EXPECT_EQ(h.stack.index(), steps + 1);
        EXPECT_TRUE(h.controller.undo());
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
    }
    // The same on a dimension already there.
    Harness h;
    const sketch::EntityId line = h.line();
    ASSERT_EQ(h.session().setDimension(line, "25"), "");
    const sketch::EntityId dimension = h.onlyDimension();
    const std::size_t steps = h.stack.index();
    for (const char* raw : {"0", "-1", "", "1/0", "2+"}) {
        EXPECT_NE(h.session().setDimension(dimension, raw), "") << raw;
        EXPECT_EQ(h.stack.index(), steps) << raw;
        EXPECT_NEAR(h.lengthOf(line), 25.0, 1e-9) << raw;
    }
    EXPECT_EQ(h.session().setDimension(dimension, "30"), "");
    EXPECT_NEAR(h.lengthOf(line), 30.0, 1e-9);
}

// ---- The wrong kind of unit ------------------------------------------------------------

// OpenShape3D: testLengthUnitCannotResizeArcSweepAndAllowsImmediateRecovery,
// testAngleUnitCannotResizeLengthAndAllowsImmediateRecovery,
// testMixedLengthSourceConversionRecoveryAndHistory. Before, "10 mm" on an
// angle was taken as 10 degrees.
TEST(TypedValues, AnAngleRefusesALengthUnitAndALengthAnAngleUnit)
{
    Harness h;
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Line);
    h.click({0, 0});
    h.click({20, 0});
    h.click({20, 15});
    h.click({0, 0}); // closes the triangle
    h.controller.setSketchTool(SketchTool::Select);
    h.click({10, 0});
    ASSERT_TRUE(h.session().triggerAction("length").ok()); // the base: 20
    sketch::EntityId base = sketch::kNoEntity, hypotenuse = sketch::kNoEntity;
    for (const auto& [id, l] : h.sketch().lines()) {
        const Vec2 a = h.sketch().point(l.start)->position, b = h.sketch().point(l.end)->position;
        if (std::abs(a.y) < 1e-9 && std::abs(b.y) < 1e-9)
            base = id;
        else if (std::abs(a.x - b.x) > 1e-9 && std::abs(a.y - b.y) > 1e-9)
            hypotenuse = id;
    }
    ASSERT_NE(hypotenuse, sketch::kNoEntity);
    h.session().select(base, false);
    h.session().select(hypotenuse, true);
    ASSERT_TRUE(h.session().triggerAction("angle").ok());
    sketch::EntityId angle = sketch::kNoEntity, length = sketch::kNoEntity;
    for (const auto& [id, c] : h.sketch().constraints()) {
        if (c.kind == sketch::ConstraintKind::Angle)
            angle = id;
        if (c.kind == sketch::ConstraintKind::Distance)
            length = id;
    }
    ASSERT_NE(angle, sketch::kNoEntity);
    ASSERT_NE(length, sketch::kNoEntity);
    const double angleBefore = h.sketch().constraint(angle)->value;
    const std::size_t steps = h.stack.index();
    for (const char* draft : {"10 mm", "2 cm", "1 m", "(10+5) mm", "1 in", "1 ft", "45 + 1 mm"}) {
        EXPECT_EQ(h.session().setDimension(angle, draft).rfind("This is an angle: type it in degrees", 0), 0u) << draft;
        EXPECT_DOUBLE_EQ(h.sketch().constraint(angle)->value, angleBefore) << draft;
        EXPECT_EQ(h.stack.index(), steps) << draft;
    }
    for (const char* draft : {"1 deg", "10+5 deg", "= 2 deg", "30\xC2\xB0", "1 cm + 2 deg"}) {
        EXPECT_EQ(h.session().setDimension(length, draft), "This is a length: type it in mm, cm, m, in or ft, not in degrees.")
            << draft;
        EXPECT_NEAR(h.sketch().constraint(length)->value, 20.0, 1e-9) << draft;
        EXPECT_EQ(h.stack.index(), steps) << draft;
    }
    // Corrected at once.
    EXPECT_EQ(h.session().setDimension(angle, "45 deg"), "");
    EXPECT_EQ(h.labelText(SketchLabel::Kind::Dimension, angle), "45\xC2\xB0");
    EXPECT_EQ(h.session().setDimension(length, "2 cm - 5 mm"), "");
    EXPECT_NEAR(h.sketch().constraint(length)->value, 15.0, 1e-9);
    EXPECT_EQ(h.stack.index(), steps + 2);
}

// ---- Counts ------------------------------------------------------------------------

// OpenShape3D: testPolygonCountEditsPreserveGeometryReferencesAndHistory.
// Our polygons are lines and constraints once drawn, so the count is typed
// while drawing (the "sides" live value), not edited afterwards.
TEST(TypedValues, APolygonsCountIsAWholeNumberInItsRange)
{
    Harness h;
    h.document.setDisplayUnit(LengthUnit::Inch); // a count never converts
    ASSERT_TRUE(h.controller.startSketch().ok());
    h.controller.skipAnimation();
    h.controller.setSketchTool(SketchTool::Polygon);
    h.click({0, 0});
    h.move({12, 0.2});
    ASSERT_TRUE(h.session().focusInput("sides"));
    std::string shown;
    for (const auto& label : h.session().labels(h.controller.camera()))
        if (label.kind == SketchLabel::Kind::Input && label.key == "sides")
            shown = label.text;
    EXPECT_EQ(shown, "6") << "a count shows no unit, whatever the display unit";
    // Refused, the count stays: too few, too many (ours stop at 64), a
    // fraction (OpenShape3D rounds 3.5 down to 3; a fraction typed for a
    // count is a slip, better shown than silently dropped), arithmetic that
    // divides by zero, a unit.
    for (const char* bad : {"2", "65", "10001", "3.5", "1/0", "5 mm", "0"}) {
        EXPECT_EQ(h.session().setInput("sides", bad), "The number of sides must be a whole number from 3 to 64.") << bad;
        EXPECT_EQ(h.session().polygonSides(), 6) << bad;
    }
    EXPECT_EQ(h.session().setInput("sides", "5"), "");
    EXPECT_EQ(h.session().polygonSides(), 5);
    EXPECT_EQ(h.session().setInput("sides", "64"), "");
    EXPECT_EQ(h.session().polygonSides(), 64);
    EXPECT_EQ(h.session().setInput("sides", "3"), "");
    ASSERT_TRUE(h.controller.keyPress(Key::Enter));
    EXPECT_EQ(h.sketch().lines().size(), 3u) << "a triangle";
    // The keypad's count: whole numbers from 1 (the field judges its own range).
    for (const char* bad : {"", "3.5", "0", "-2", "5mm"})
        EXPECT_FALSE(keypadValue(bad, KeypadMode::Count, LengthUnit::Inch).millimeters.has_value()) << bad;
    EXPECT_DOUBLE_EQ(*keypadValue("65", KeypadMode::Count, LengthUnit::Inch).millimeters, 65.0);
}

// ---- Reopening a stored dimension --------------------------------------------------

// OpenShape3D: testPaletteReopensStoredDimensionWithoutDuplicatingIt. The
// dimension's own label edits it; the size label goes once a dimension sets
// the size, so a second one cannot be added over it.
TEST(TypedValues, AStoredDimensionReopensWithoutBeingDuplicated)
{
    Harness h;
    const sketch::EntityId line = h.line();
    EXPECT_EQ(h.labelText(SketchLabel::Kind::Size, line), "40");
    ASSERT_EQ(h.session().setDimension(line, "20"), "");
    ASSERT_EQ(h.dimensions(), 1u);
    const sketch::EntityId stored = h.onlyDimension();
    h.session().select(line, false);
    EXPECT_EQ(h.labelText(SketchLabel::Kind::Size, line), "(none)") << "no size label over the dimension";
    EXPECT_EQ(h.labelText(SketchLabel::Kind::Dimension, stored), "20");
    EXPECT_EQ(h.session().setDimension(line, "30"), "Other dimensions already set this size: change one of them.");
    EXPECT_EQ(h.dimensions(), 1u);
    // Reopened: the same dimension takes the new value.
    EXPECT_EQ(h.session().setDimension(stored, "30"), "");
    EXPECT_EQ(h.dimensions(), 1u);
    EXPECT_EQ(h.onlyDimension(), stored);
    EXPECT_NEAR(h.lengthOf(line), 30.0, 1e-9);
    EXPECT_EQ(h.stack.undoLabel(), "Edit dimension");
    EXPECT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.lengthOf(line), 20.0, 1e-9);
    EXPECT_EQ(h.dimensions(), 1u);
    // Reopened and confirmed as shown: no change, no step.
    const std::size_t steps = h.stack.index();
    EXPECT_EQ(h.session().keepDimension(stored), "");
    EXPECT_EQ(h.stack.index(), steps);
    EXPECT_EQ(h.dimensions(), 1u);
}

// ---- Seeded text ---------------------------------------------------------------------

// OpenShape3D: testLengthSeedPrecisionMatchesLabelAndPaletteWithoutChangingMeasurement,
// testUntouchedRoundedImperialSeedLocksExactMeasurementAndUndoes,
// testUntouchedTinyLengthAndRoundedArcSweepPreserveGeometry. The editor
// opens with the label's text (three decimals at most); confirmed as it
// is, the size is kept exactly. Before, the rounded text was typed back:
// 40 mm shown as 1.575 in became 40.005 mm.
TEST(TypedValues, TheSeededTextMatchesTheLabelAndConfirmedAsItIsKeepsTheExactSize)
{
    {
        Harness h;
        const sketch::EntityId line = h.line(); // drawn in mm: exactly 40
        h.document.setDisplayUnit(LengthUnit::Inch);
        const std::string seed = h.labelText(SketchLabel::Kind::Size, line);
        EXPECT_EQ(seed, "1.575");
        EXPECT_NEAR(*parseLength(seed, LengthUnit::Inch).millimeters, 40.005, 1e-9) << "the rounding the old path typed back";
        const std::size_t steps = h.stack.index();
        EXPECT_EQ(h.session().keepDimension(line), "");
        EXPECT_EQ(h.stack.index(), steps + 1) << "the dimension is added: one step";
        EXPECT_EQ(h.stack.undoLabel(), "Length");
        ASSERT_EQ(h.dimensions(), 1u);
        const sketch::EntityId dimension = h.onlyDimension();
        EXPECT_NEAR(h.sketch().constraint(dimension)->value, 40.0, 1e-9);
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        EXPECT_EQ(h.labelText(SketchLabel::Kind::Dimension, dimension), seed) << "the label reads the same";
        // Reopened and confirmed: still exactly 40, no step.
        EXPECT_EQ(h.session().keepDimension(dimension), "");
        EXPECT_EQ(h.stack.index(), steps + 1);
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        // An edit that types the rounded text is taken as typed.
        EXPECT_EQ(h.session().setDimension(dimension, seed), "");
        EXPECT_NEAR(h.lengthOf(line), 40.005, 1e-9);
        // Undo and redo.
        EXPECT_TRUE(h.controller.undo());
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        EXPECT_TRUE(h.controller.undo());
        EXPECT_EQ(h.dimensions(), 0u);
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
        EXPECT_TRUE(h.controller.redo());
        EXPECT_EQ(h.dimensions(), 1u);
        EXPECT_NEAR(h.lengthOf(line), 40.0, 1e-9);
    }
    {
        // Millimeters: a length with more decimals than the label shows.
        Harness h;
        const sketch::EntityId line = h.line();
        ASSERT_EQ(h.session().setDimension(line, "1.982234"), "");
        const sketch::EntityId dimension = h.onlyDimension();
        EXPECT_EQ(h.labelText(SketchLabel::Kind::Dimension, dimension), "1.982");
        const std::size_t steps = h.stack.index();
        EXPECT_EQ(h.session().keepDimension(dimension), "");
        EXPECT_EQ(h.stack.index(), steps);
        EXPECT_NEAR(h.lengthOf(line), 1.982234, 1e-9);
        EXPECT_NEAR(h.sketch().constraint(dimension)->value, 1.982234, 1e-12);
    }
    {
        // A circle's diameter.
        Harness h;
        ASSERT_TRUE(h.controller.startSketch().ok());
        h.controller.skipAnimation();
        h.controller.setSketchTool(SketchTool::Circle);
        h.click({0, 0});
        h.click({10, 0});
        h.controller.setSketchTool(SketchTool::Select);
        ASSERT_EQ(h.sketch().circles().size(), 1u);
        const sketch::EntityId circle = h.sketch().circles().begin()->first;
        h.session().select(circle, false);
        h.document.setDisplayUnit(LengthUnit::Inch);
        EXPECT_EQ(h.labelText(SketchLabel::Kind::Size, circle), "\xC3\x98" "0.787");
        EXPECT_EQ(h.session().keepDimension(circle), "");
        EXPECT_NEAR(h.sketch().circle(circle)->radius, 10.0, 1e-9);
        EXPECT_EQ(h.stack.undoLabel(), "Diameter");
    }
    // A dimension that is gone: said plainly.
    Harness h;
    h.line();
    EXPECT_EQ(h.session().keepDimension(9999), "That dimension no longer exists.");
}

// ---- The value box (InteractionController::setValueText) ----------------------------------

namespace {

struct BoxHarness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};

    BoxHarness()
    {
        controller.setViewportSize({1200, 800});
        EXPECT_TRUE(controller.createBox(20).ok()); // (-10,-10,0) .. (10,10,20)
        controller.fitAll(false);
    }
    double height() const { return geom::boundingBox(document.bodies().front()->shape()).size().z; }
    const Operation* selectTop()
    {
        PointerEvent e;
        e.position = controller.camera().project({0, 0, 20});
        e.button = PointerButton::Left;
        controller.pointerPress(e);
        controller.pointerRelease(e);
        const Operation* op = controller.operation();
        EXPECT_NE(op, nullptr);
        return op;
    }
};

} // namespace

TEST(TypedValues, TheValueBoxTakesUnitsArithmeticAndTheKeypadsSigns)
{
    struct Case {
        const char* text;
        double height;
    };
    for (const Case& c : {Case{"3 cm", 30.0}, Case{"1\"", 25.4}, Case{"0.1 ft", 30.48}, Case{"10\xC3\x97" "3", 30.0},
                          Case{"90\xC3\xB7" "3", 30.0}, Case{"40\xE2\x88\x92" "10", 30.0}, Case{"= 15 * 2", 30.0}}) {
        SCOPED_TRACE(c.text);
        BoxHarness h;
        const Operation* op = h.selectTop();
        ASSERT_NE(op, nullptr);
        EXPECT_EQ(h.controller.setValueText(c.text), "");
        ASSERT_TRUE(h.controller.commitOperation().ok());
        EXPECT_NEAR(h.height(), c.height, 1e-9);
        EXPECT_NEAR(geom::volume(h.document.bodies().front()->shape()), 20.0 * 20.0 * c.height, 1e-6);
    }
    // The wrong kind of unit and bad input: refused, the box as it was.
    for (const char* bad : {"10 deg", "45\xC2\xB0", "2+", "1/0", "1' 2 cm"}) {
        SCOPED_TRACE(bad);
        BoxHarness h;
        const Operation* op = h.selectTop();
        ASSERT_NE(op, nullptr);
        EXPECT_NE(h.controller.setValueText(bad), "");
        EXPECT_DOUBLE_EQ(op->value(), 20.0);
        // ... and corrected.
        EXPECT_EQ(h.controller.setValueText("25"), "");
        ASSERT_TRUE(h.controller.commitOperation().ok());
        EXPECT_NEAR(h.height(), 25.0, 1e-9);
    }
    // A sign in front changes the measured thickness by that much, the minus
    // sign as the hyphen.
    for (const Case& c : {Case{"+5", 25.0}, Case{"-5", 15.0}, Case{"\xE2\x88\x92" "5", 15.0}}) {
        SCOPED_TRACE(c.text);
        BoxHarness h;
        ASSERT_NE(h.selectTop(), nullptr);
        EXPECT_EQ(h.controller.setValueText(c.text), "");
        ASSERT_TRUE(h.controller.commitOperation().ok());
        EXPECT_NEAR(h.height(), c.height, 1e-9);
    }
    // The keypad on the value shown: times 2 doubles it (keypad text, as the
    // value box takes it).
    BoxHarness h;
    const Operation* op = h.selectTop();
    ASSERT_NE(op, nullptr);
    KeypadState state{"20.00 mm", true};
    for (const char* key : {"*", "2"})
        state = pressKeypadKey(state, key, KeypadMode::Length).state;
    EXPECT_EQ(state.text, "20.00 mm*2");
    EXPECT_EQ(h.controller.setValueText(state.text), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.height(), 40.0, 1e-9);
}
