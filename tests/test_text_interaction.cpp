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

#include <gtest/gtest.h>

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
    EXPECT_EQ(h.messages.back(), "Click a flat face, then Text: type the words, click or tap where they go, and drag the arrow "
                                 "out to raise them or in to cut them.");
    EXPECT_FALSE(h.controller.operationTakesText());
    EXPECT_EQ(h.controller.setOperationText("Hi"), "Select a flat face, then Text.");
}
