// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The Text tool through the interaction layer: offered on a flat face and in
// the palette, typing previews, clicks place (snapped), the arrow and the
// chip set depth / size / angle, one Text step; the words edited later from
// the Model panel.
#include "TestFonts.h"

#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Holes.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

using namespace os;
using namespace os::interact;

namespace {

struct TextHarness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    Uuid body;

    TextHarness()
    {
        controller.onMessage = [this](const std::string& m) { messages.push_back(m); };
        controller.setViewportSize({1200, 800});
    }
    static PointerEvent at(Vec2 p)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        e.device = PointerDevice::Mouse;
        return e;
    }
    void clickAt(Vec2 p)
    {
        controller.pointerPress(at(p));
        controller.pointerRelease(at(p));
    }
    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    double volume() const { return geom::volume(document.body(body)->shape()); }
    bool offers(const std::string& id) const
    {
        for (const auto& a : controller.contextActions())
            if (a.id == id)
                return true;
        return false;
    }
    const TextOperation* tool() const { return dynamic_cast<const TextOperation*>(controller.operation()); }

    // A 60 x 30 x 5 plate from the origin, its top face selected.
    void plateWithTopSelected()
    {
        auto box = std::make_unique<doc::BoxFeature>();
        box->size = {60, 30, 5};
        auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(box));
        body = create->bodyId();
        ASSERT_TRUE(stack.push(std::move(create), document).ok());
        controller.documentChanged();
        controller.fitAll(false);
        clickAt(screen({40, 20, 5}));
        ASSERT_EQ(controller.selection().size(), 1u);
        ASSERT_EQ(controller.selection().items()[0].kind, sel::SelectionKind::Face);
    }
};

double letterArea(const std::string& words, double size, const char* font = doc::kTextFontRegular)
{
    return geom::surfaceArea(geom::textFaces({words, font, size}).value());
}

} // namespace

TEST(TextInteraction, TypePlaceAndRaiseText)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    h.plateWithTopSelected();
    EXPECT_TRUE(h.offers("text")) << "a flat face offers Text";
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    const TextOperation* tool = h.tool();
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->title(), "Text");
    EXPECT_TRUE(h.controller.operationTakesText());
    EXPECT_FALSE(tool->prompt().empty()) << "asks for the text";
    EXPECT_FALSE(tool->canCommit()) << "nothing typed yet";
    EXPECT_NEAR((tool->position() - Vec2{30, 15}).length(), 0, 1e-9) << "starts at the face's center";
    EXPECT_EQ(tool->valueLabel(), "Depth");
    EXPECT_DOUBLE_EQ(tool->depth(), 1.0) << "raised 1 mm by default";
    EXPECT_DOUBLE_EQ(tool->size(), 10.0);
    EXPECT_TRUE(h.controller.valueLabelPosition().has_value()) << "the chip is at the arrow";
    const Status nothing = h.controller.commitOperation();
    EXPECT_FALSE(nothing);
    EXPECT_EQ(nothing.userMessage(), "Type the text first.");
    EXPECT_EQ(h.controller.setOperationText("   "), "Type the text first.");
    EXPECT_FALSE(tool->canCommit());

    EXPECT_EQ(h.controller.setOperationText("Hi"), "");
    EXPECT_EQ(h.controller.operationText(), "Hi");
    EXPECT_TRUE(tool->prompt().empty());
    EXPECT_TRUE(tool->hasPreview());
    EXPECT_TRUE(tool->canCommit());
    // A character the font does not have: said at once, not applied.
    EXPECT_EQ(h.controller.setOperationText("Hi \xF0\x9F\x98\x80"), "This font has no \"\xF0\x9F\x98\x80\": use another character.");
    EXPECT_FALSE(tool->canCommit());
    EXPECT_EQ(h.controller.setOperationText("OK"), "");

    // A click just off the face's center line (y = 15) lines up with it.
    const double px = h.controller.camera().pixelSize({15, 15, 5});
    h.clickAt(h.screen({15.3, 15 + 3 * px, 5}));
    EXPECT_NEAR(tool->position().y, 15.0, 1e-9) << "lined up with the center";
    EXPECT_NEAR(tool->position().x, 15.3, 0.1);
    // Near the center: onto it.
    h.clickAt(h.screen({30 + 4 * px, 15 - 3 * px, 5}));
    EXPECT_NEAR((tool->position() - Vec2{30, 15}).length(), 0, 1e-9);

    // Size (the capitals' height), then back to the depth, typed.
    ASSERT_TRUE(h.controller.triggerAction("field:size").ok());
    EXPECT_EQ(tool->valueLabel(), "Size");
    EXPECT_EQ(h.controller.setValueText("8"), "");
    ASSERT_TRUE(h.controller.triggerAction("field:depth").ok());
    EXPECT_EQ(h.controller.setValueText("1.5"), "");
    EXPECT_DOUBLE_EQ(tool->size(), 8.0);
    EXPECT_DOUBLE_EQ(tool->depth(), 1.5);

    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.controller.operation(), nullptr);
    const double area = letterArea("OK", 8);
    EXPECT_NEAR(h.volume() - 9000, area * 1.5, 1e-5 * area);
    EXPECT_NEAR(geom::boundingBox(h.document.body(h.body)->shape()).max.z, 6.5, 1e-6);
    const auto* step = dynamic_cast<const doc::TextFeature*>(h.document.body(h.body)->features().back().get());
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->text, "OK");
    EXPECT_DOUBLE_EQ(step->size, 8.0);
    EXPECT_DOUBLE_EQ(step->depth, 1.5);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u) << "one step";

    // The Model panel lists it, with the words as an editable string.
    const HistoryRow* row = nullptr;
    const auto rows = h.controller.historyRows();
    for (const auto& r : rows)
        if (r.kind == HistoryRow::Kind::Feature && r.name == "Text")
            row = &r;
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->detail, "\xE2\x80\x9C" "OK\xE2\x80\x9D \xC2\xB7 8.00 mm \xC2\xB7 raised 1.50 mm");
    ASSERT_FALSE(row->parameters.empty());
    EXPECT_EQ(row->parameters[0].key, "text");
    EXPECT_TRUE(row->parameters[0].isText);
    EXPECT_EQ(row->parameters[0].valueText, "OK");
    ASSERT_TRUE(h.controller.setFeatureParameter(step->id(), "text", "OKAY").ok());
    EXPECT_EQ(static_cast<const doc::TextFeature*>(h.document.body(h.body)->features().back().get())->text, "OKAY");
    EXPECT_NEAR(h.volume() - 9000, letterArea("OKAY", 8) * 1.5, 1e-5 * letterArea("OKAY", 8));
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume() - 9000, area * 1.5, 1e-5 * area);
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(), 9000, 1e-9);
}

// Deboss, a quarter turn, and the tool remembers its settings for the next face.
TEST(TextInteraction, DebossTurnedAndRemembered)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    h.plateWithTopSelected();
    ASSERT_TRUE(h.controller.runTool("text").ok()) << "the palette entry";
    const TextOperation* tool = h.tool();
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(h.controller.setOperationText("Z"), "");
    ASSERT_TRUE(h.offers("deboss"));
    ASSERT_TRUE(h.controller.triggerAction("deboss").ok());
    EXPECT_DOUBLE_EQ(tool->depth(), -1.0);
    ASSERT_TRUE(h.controller.triggerAction("angle:90").ok());
    EXPECT_DOUBLE_EQ(tool->angleDegrees(), 90.0);
    ASSERT_TRUE(h.controller.triggerAction("field:angle").ok());
    EXPECT_EQ(tool->valueLabel(), "Angle");
    EXPECT_TRUE(tool->isAngle());
    EXPECT_EQ(h.controller.setValueText("45"), "");
    EXPECT_DOUBLE_EQ(tool->angleDegrees(), 45.0);
    ASSERT_TRUE(h.controller.triggerAction("angle:90").ok());
    EXPECT_DOUBLE_EQ(tool->angleDegrees(), 90.0);
    EXPECT_TRUE(tool->hasPreview()) << tool->error();
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double area = letterArea("Z", 10);
    EXPECT_NEAR(9000 - h.volume(), area * 1.0, 1e-5 * area);
    const auto* step = dynamic_cast<const doc::TextFeature*>(h.document.body(h.body)->features().back().get());
    ASSERT_NE(step, nullptr);
    EXPECT_NEAR(step->angle, kPi / 2, 1e-12);
    // The letter's cut is 10 mm (the capital height) along X once turned.
    const auto removed = geom::booleanOp(geom::makeBox({0, 0, 0}, {60, 30, 5}).value(), h.document.body(h.body)->shape(),
                                         geom::BooleanKind::Subtract);
    ASSERT_TRUE(removed);
    EXPECT_NEAR(geom::boundingBox(removed.value()).size().x, 10.0, 1e-5);

    // The next face starts with the same settings.
    h.clickAt(h.screen({40, 20, 5}));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_NE(h.tool(), nullptr);
    EXPECT_EQ(h.tool()->text(), "Z");
    EXPECT_DOUBLE_EQ(h.tool()->depth(), -1.0);
    EXPECT_DOUBLE_EQ(h.tool()->angleDegrees(), 90.0);
    // Esc leaves the tool without adding anything.
    h.controller.keyPress(Key::Escape);
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u);
}

// A typed angle is a direction: out of range it is refused with a message
// (not saved into a file it could no longer open); in range it is stored in
// [0, 2 pi), and the project saves and reopens with it.
TEST(TextInteraction, TypedAnglesAreCheckedAndStoredAsDirections)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    h.plateWithTopSelected();
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_EQ(h.controller.setOperationText("R"), "");
    ASSERT_TRUE(h.controller.triggerAction("field:angle").ok());
    EXPECT_EQ(h.controller.setValueText("-100000"), "The angle must be between -360\xC2\xB0 and 360\xC2\xB0.");
    EXPECT_FALSE(h.tool()->canCommit());
    EXPECT_FALSE(h.controller.commitOperation());
    EXPECT_EQ(h.document.body(h.body)->features().size(), 1u) << "nothing added";
    // Remembered while refused (an option clicked meanwhile): the next use starts at 0.
    ASSERT_TRUE(h.controller.triggerAction("deboss").ok());
    h.controller.keyPress(Key::Escape);
    ASSERT_EQ(h.controller.operation(), nullptr);
    h.clickAt(h.screen({40, 20, 5}));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_NE(h.tool(), nullptr);
    EXPECT_DOUBLE_EQ(h.tool()->angleDegrees(), 0.0);
    EXPECT_TRUE(h.tool()->hasPreview()) << h.tool()->error();

    // -90 degrees: a quarter turn clockwise, stored as 270.
    ASSERT_TRUE(h.controller.triggerAction("field:angle").ok());
    EXPECT_EQ(h.controller.setValueText("-90"), "");
    EXPECT_TRUE(h.tool()->canCommit()) << h.tool()->error();
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const auto* step = dynamic_cast<const doc::TextFeature*>(h.document.body(h.body)->features().back().get());
    ASSERT_NE(step, nullptr);
    EXPECT_NEAR(step->angle, 3 * kPi / 2, 1e-12);
    const double volume = h.volume();
    const double area = letterArea("R", 10);
    EXPECT_NEAR(9000 - volume, area, 1e-5 * area);

    // Saved and reopened as it is; a file holding many turns (written before
    // the tool checked its angle) opens too, at the same direction.
    auto json = io::documentToJson(h.document);
    auto reopened = io::documentFromJson(json);
    ASSERT_TRUE(reopened.ok()) << reopened.developerMessage();
    const auto& again = static_cast<const doc::TextFeature&>(*reopened.value()->body(h.body)->features().back());
    EXPECT_DOUBLE_EQ(again.angle, step->angle);
    EXPECT_NEAR(geom::volume(reopened.value()->body(h.body)->shape()), volume, 1e-6);
    for (auto& feature : json["bodies"][0]["features"])
        if (feature["type"] == "Text")
            feature["params"]["angle"] = -100000 * kPi / 180; // -1745.3: 80 degrees after 278 turns
    auto turned = io::documentFromJson(json);
    ASSERT_TRUE(turned.ok()) << turned.developerMessage();
    const auto& many = static_cast<const doc::TextFeature&>(*turned.value()->body(h.body)->features().back());
    EXPECT_NEAR(many.angle, 80 * kPi / 180, 1e-9);
    EXPECT_FALSE(turned.value()->body(h.body)->hasFailures());
    EXPECT_NEAR(geom::volume(turned.value()->body(h.body)->shape()), volume, 1e-6);
}

// Words remembered from the last use preview when the tool opens, but a
// stray click elsewhere does not apply them (the tool closes and the click
// selects as usual), nor does picking a body in the Model panel; once
// anything is typed, placed or changed a click elsewhere does, and Enter /
// Apply always do.
TEST(TextInteraction, AStrayClickDoesNotApplyRememberedText)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    h.plateWithTopSelected();
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_EQ(h.controller.setOperationText("OK"), "");
    EXPECT_TRUE(h.tool()->edited());
    // Off the face's center (where the tool opens next time): "OK" at 10 mm
    // is about 19 x 10 mm, so these letters span x 2.5-21.5, y 2-12.
    h.clickAt(h.screen({12, 7, 5}));
    ASSERT_NEAR((h.tool()->position() - Vec2{12, 7}).length(), 0, 0.5);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    ASSERT_EQ(h.document.body(h.body)->features().size(), 2u);
    const double withOne = h.volume();
    const Vec3 top{52, 5, 5}; // the top face, away from any letters

    // Opened again: "OK" previews at the center, nothing is done yet.
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_NE(h.tool(), nullptr);
    EXPECT_EQ(h.tool()->text(), "OK");
    EXPECT_TRUE(h.tool()->canCommit()) << h.tool()->error();
    EXPECT_FALSE(h.tool()->edited());
    // A tap on empty space leaves the tool without a step.
    h.clickAt({5, 5});
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_TRUE(h.controller.selection().empty());
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u);
    EXPECT_NEAR(h.volume(), withOne, 1e-9);
    // A click on another face selects it (its usual tools), still without a step.
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    h.clickAt(h.screen({30, 0, 2.5})); // the plate's front face
    EXPECT_EQ(h.tool(), nullptr);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u);
    ASSERT_EQ(h.controller.selection().size(), 1u);
    const auto front = geom::faceInfo(h.document.body(h.body)->shape(), h.controller.selection().items()[0].index);
    ASSERT_TRUE(front.has_value());
    EXPECT_NEAR(front->normal.y, -1.0, 1e-9) << "the front face is selected";
    // Picking the body in the Model panel: selected, no step either.
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_TRUE(h.controller.selectBody(h.body, false).ok());
    EXPECT_EQ(h.tool(), nullptr);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u);
    EXPECT_TRUE(h.controller.selection().allOfKind(sel::SelectionKind::Body));

    // Changed (Deboss): a click elsewhere applies it.
    h.clickAt({5, 5});
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_TRUE(h.controller.triggerAction("deboss").ok());
    EXPECT_TRUE(h.tool()->edited());
    h.clickAt({5, 5});
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 3u);
    // Placed by a click on the face: applied likewise.
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    h.clickAt(h.screen({45, 25, 5}));
    EXPECT_TRUE(h.tool()->edited());
    EXPECT_TRUE(h.tool()->canCommit()) << h.tool()->error();
    h.clickAt({5, 5});
    EXPECT_EQ(h.document.body(h.body)->features().size(), 4u);

    // Untouched, but applied on purpose (Enter or the chip's Apply). The
    // two cut-in steps undone first: the remembered words go to the free center.
    ASSERT_TRUE(h.stack.undo(h.document));
    ASSERT_TRUE(h.stack.undo(h.document));
    h.controller.documentChanged();
    ASSERT_EQ(h.document.body(h.body)->features().size(), 2u);
    h.clickAt(h.screen(top));
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_NE(h.tool(), nullptr);
    EXPECT_FALSE(h.tool()->edited());
    EXPECT_LT(h.tool()->depth(), 0) << "remembered: cut in";
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_EQ(h.document.body(h.body)->features().size(), 3u);
    const double area = letterArea("OK", 10);
    EXPECT_NEAR(withOne - h.volume(), area, 1e-5 * area) << "cut in 1 mm at the center";
}

// With the bold font built in, Bold switches to it (stored as its font id).
TEST(TextInteraction, BoldWhenABoldFontIsThere)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    ASSERT_FALSE(test::registerTestBoldFont(doc::kTextFontBold).empty()) << "resources/fonts/NotoSans-Bold.ttf";
    TextHarness h;
    h.plateWithTopSelected();
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_EQ(h.controller.setOperationText("Bold"), "");
    ASSERT_TRUE(h.offers("bold"));
    ASSERT_TRUE(h.controller.triggerAction("bold").ok());
    EXPECT_TRUE(h.tool()->bold());
    EXPECT_TRUE(h.tool()->hasPreview()) << h.tool()->error();
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const auto* step = dynamic_cast<const doc::TextFeature*>(h.document.body(h.body)->features().back().get());
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(step->font, doc::kTextFontBold);
    const double bold = letterArea("Bold", 10, doc::kTextFontBold);
    EXPECT_GT(bold, letterArea("Bold", 10) * 1.1) << "bold strokes are wider";
    EXPECT_NEAR(h.volume() - 9000, bold * 1.0, 1e-5 * bold);
    const auto rows = h.controller.historyRows();
    bool listed = false;
    for (const auto& r : rows)
        listed = listed || (r.name == "Text" && r.detail.find("Bold") != std::string::npos);
    EXPECT_TRUE(listed);
}

// The arrow sets the depth: dragged out it raises the text, in it cuts.
TEST(TextInteraction, TheArrowSetsTheDepth)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    h.plateWithTopSelected();
    ASSERT_TRUE(h.controller.triggerAction("text").ok());
    ASSERT_EQ(h.controller.setOperationText("T"), "");
    ASSERT_TRUE(h.controller.triggerAction("field:size").ok());
    ASSERT_EQ(h.tool()->handleCount(), 1);
    // Grabbing the arrow goes back to the depth.
    const LinearManipulator arrow = h.tool()->handle(0);
    const Vec3 tip = arrow.anchor(h.tool()->handleOffset(0));
    const Vec3 grab = tip + arrow.direction() * (40 * h.controller.camera().pixelSize(tip));
    h.controller.pointerPress(TextHarness::at(h.screen(grab)));
    EXPECT_EQ(h.tool()->field(), TextOperation::Field::Depth);
    h.controller.pointerRelease(TextHarness::at(h.screen(grab)));
}

TEST(TextInteraction, TextNeedsAFlatFace)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextHarness h;
    EXPECT_FALSE(h.controller.runTool("text").ok());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_EQ(h.messages.back(), "Click a flat face, then Text: type the words, click where they go, and drag the arrow "
                                 "out to raise them or in to cut them.");
    // In the touch layout it says tap.
    h.controller.setTouchLayout(true);
    EXPECT_FALSE(h.controller.runTool("text").ok());
    EXPECT_EQ(h.messages.back(), "Tap a flat face, then Text: type the words, tap where they go, and drag the arrow "
                                 "out to raise them or in to cut them.");
    EXPECT_FALSE(h.controller.operationTakesText());
    EXPECT_EQ(h.controller.setOperationText("Hi"), "Select a flat face, then Text.");
}
