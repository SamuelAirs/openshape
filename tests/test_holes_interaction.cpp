// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Holes for screws through the interaction layer: counterbore and countersink
// on a hole's rim.
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

using namespace os;
using namespace os::interact;

namespace {

struct HoleHarness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    std::vector<std::string> messages;
    Uuid body;

    HoleHarness()
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

    // A 40 x 40 x 10 block (centered on the origin) with a round hole of
    // `diameter` through its middle, cut from a sketch.
    void blockWithHole(double diameter)
    {
        auto block = std::make_unique<doc::BoxFeature>();
        block->origin = {-20, -20, 0};
        block->size = {40, 40, 10};
        auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
        body = create->bodyId();
        ASSERT_TRUE(stack.push(std::move(create), document).ok());
        auto sk = std::make_unique<sketch::Sketch>(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
        sk->addCircle(sketch::kOriginId, diameter / 2);
        const Uuid sketchId = sk->id();
        document.addSketch(std::move(sk));
        auto regions = doc::sketchRegions(*document.sketch(sketchId)).value();
        auto cut = std::make_unique<doc::ExtrudeFeature>();
        cut->sketchId = sketchId;
        cut->profiles = {doc::makeProfileRef(regions.front(), *document.sketch(sketchId))};
        cut->distance = -10;
        cut->mode = doc::ExtrudeMode::Cut;
        cut->throughAll = true;
        ASSERT_TRUE(stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(cut)), document).ok());
        controller.documentChanged();
        controller.fitAll(false);
    }

    bool offers(const std::string& id) const
    {
        for (const auto& a : controller.contextActions())
            if (a.id == id)
                return true;
        return false;
    }

    // Presses and releases on the middle of an operation handle's arrow.
    void clickHandle(int index)
    {
        const Operation* op = controller.operation();
        const LinearManipulator h = op->handle(index);
        const Vec3 a = h.anchor(op->handleOffset(index));
        const Vec3 mid = a + h.direction() * (40 * controller.camera().pixelSize(a));
        clickAt(screen(mid));
    }
};

double frustum(double r1, double r2, double h)
{
    return kPi * h / 3 * (r1 * r1 + r1 * r2 + r2 * r2);
}

} // namespace

TEST(HoleInteraction, CounterboreOnAHoleRimWithPresetsAndTypedSizes)
{
    HoleHarness h;
    h.blockWithHole(3.0);
    const double holed = h.volume();
    ASSERT_NEAR(holed, 16000.0 - kPi * 2.25 * 10, 1e-3);

    h.clickAt(h.screen({1.5, 0, 10})); // the top rim
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Edge);
    EXPECT_TRUE(h.offers("insert"));
    EXPECT_TRUE(h.offers("counterbore"));
    EXPECT_TRUE(h.offers("countersink"));

    ASSERT_TRUE(h.controller.triggerAction("counterbore").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Counterbore M3");
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Diameter");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 6.5) << "M3 socket head, previewed right away";
    EXPECT_TRUE(h.controller.operation()->hasPreview());
    EXPECT_EQ(h.controller.operation()->handleCount(), 2) << "diameter and depth arrows";
    for (const char* size : {"preset:0", "preset:1", "preset:2", "preset:3", "preset:4", "preset:5"})
        EXPECT_TRUE(h.offers(size)) << size;

    // M5, then a typed diameter: no longer the preset.
    ASSERT_TRUE(h.controller.triggerAction("preset:4").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Counterbore M5");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 10.0);
    EXPECT_EQ(h.controller.setValueText("2"), "The counterbore must be wider than the hole (3.00 mm).");
    EXPECT_FALSE(h.controller.operation()->canCommit());
    EXPECT_EQ(h.controller.setValueText("7"), "");
    EXPECT_EQ(h.controller.operation()->title(), "Counterbore");

    // The arrow into the hole sets the depth (M5: 5.4 mm); type 4.
    h.clickHandle(1);
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->valueLabel(), "Depth");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 5.4);
    EXPECT_EQ(h.controller.setValueText("12"), "The counterbore would reach through the part: it is 10.00 mm thick here.");
    EXPECT_EQ(h.controller.setValueText("4"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double bored = holed - kPi * (3.5 * 3.5 - 1.5 * 1.5) * 4;
    EXPECT_NEAR(h.volume(), bored, 1e-6);

    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || (row.name == "Counterbore" && row.detail == "\xC3\x98" "7.00 mm \xC3\x97 4.00 mm");
    EXPECT_TRUE(listed);

    // The bottom rim: a countersink (M3, 90 degrees).
    h.controller.setStandardView(StandardView::Bottom, false);
    h.controller.fitAll(false);
    h.clickAt(h.screen({1.5, 0, 0}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_TRUE(h.controller.triggerAction("countersink").ok());
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Countersink M5") << "the screw size chosen last";
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 11.2);
    ASSERT_TRUE(h.controller.triggerAction("preset:2").ok());
    EXPECT_EQ(h.controller.operation()->title(), "Countersink M3");
    EXPECT_DOUBLE_EQ(h.controller.operation()->value(), 6.72);
    EXPECT_EQ(h.controller.operation()->handleCount(), 1);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double R = 3.36, r = 1.5, depth = R - r;
    EXPECT_NEAR(h.volume(), bored - (frustum(R, r, depth) - kPi * r * r * depth), 1e-6);
    bool sunk = false;
    for (const auto& row : h.controller.historyRows())
        sunk = sunk || (row.name == "Countersink" && row.detail.find("M3") != std::string::npos);
    EXPECT_TRUE(sunk);

    // Undo takes the countersink away again.
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(), bored, 1e-6);
}

namespace {

// A 60 x 30 x 5 plate from the origin, its top face selected.
void plateWithTopSelected(HoleHarness& h)
{
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {60, 30, 5};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(box));
    h.body = create->bodyId();
    ASSERT_TRUE(h.stack.push(std::move(create), h.document).ok());
    h.controller.documentChanged();
    h.controller.fitAll(false);
    h.clickAt(h.screen({40, 20, 5}));
    ASSERT_EQ(h.controller.selection().size(), 1u);
    ASSERT_EQ(h.controller.selection().items()[0].kind, sel::SelectionKind::Face);
}

const HoleOperation* holeTool(const HoleHarness& h)
{
    return dynamic_cast<const HoleOperation*>(h.controller.operation());
}

} // namespace

// The Hole tool: clicks place holes (snapped to the face's center, in line
// with it and with each other), typed X / Y set exact positions, presets set
// the diameter; one step for the set.
TEST(HoleInteraction, HoleToolPlacesSnapsAndTypesPositions)
{
    HoleHarness h;
    plateWithTopSelected(h);
    EXPECT_TRUE(h.offers("hole"));
    ASSERT_TRUE(h.controller.triggerAction("hole").ok());
    const HoleOperation* tool = holeTool(h);
    ASSERT_NE(tool, nullptr);
    EXPECT_EQ(tool->title(), "Hole");
    EXPECT_FALSE(tool->prompt().empty()) << "asks for the first click";
    EXPECT_DOUBLE_EQ(tool->value(), 3.4) << "M3 normal fit (ISO 273) to start with";
    EXPECT_FALSE(tool->canCommit());
    EXPECT_TRUE(h.controller.valueLabelPosition().has_value()) << "the diameter can be typed before the first hole";
    EXPECT_EQ(h.controller.setValueText("3.2"), "") << "no error before the first hole";
    EXPECT_FALSE(h.offers("field:x")) << "no hole to move yet";

    // Hovering shows where a click would go; clicking near the center snaps to it.
    const double px = h.controller.camera().pixelSize({30, 15, 5});
    h.controller.pointerMove(HoleHarness::at(h.screen({30 + 4 * px, 15 - 3 * px, 5})));
    ASSERT_TRUE(tool->hover().has_value());
    EXPECT_NEAR((*tool->hover() - Vec2{30, 15}).length(), 0, 1e-9);
    h.clickAt(h.screen({30 + 4 * px, 15 - 3 * px, 5}));
    ASSERT_EQ(tool->positions().size(), 1u);
    EXPECT_NEAR((tool->positions()[0] - Vec2{30, 15}).length(), 0, 1e-9);
    EXPECT_TRUE(tool->prompt().empty());
    EXPECT_TRUE(tool->canCommit());
    // Far from any snap point but in line with the center (Y), then typed X / Y.
    h.clickAt(h.screen({10.37, 15 + 3 * px, 5}));
    ASSERT_EQ(tool->positions().size(), 2u);
    EXPECT_NEAR(tool->positions()[1].y, 15.0, 1e-9) << "lined up with the center";
    EXPECT_NEAR(tool->positions()[1].x, 10.37, 0.05);
    ASSERT_TRUE(h.controller.triggerAction("field:x").ok());
    EXPECT_EQ(tool->valueLabel(), "X from corner");
    EXPECT_NEAR(tool->value(), tool->positions()[1].x, 1e-9) << "the face's corner is at the origin";
    EXPECT_EQ(h.controller.setValueText("10"), "");
    ASSERT_TRUE(h.controller.triggerAction("nextField").ok()) << "Tab";
    EXPECT_EQ(tool->valueLabel(), "Y from corner");
    EXPECT_EQ(h.controller.setValueText("5"), "");
    // A third hole, measured from the last one.
    h.clickAt(h.screen({48, 22, 5}));
    ASSERT_EQ(tool->positions().size(), 3u);
    EXPECT_NEAR((tool->positions()[1] - Vec2{10, 5}).length(), 0, 1e-9) << "typed position kept";
    ASSERT_TRUE(h.controller.triggerAction("fromLast").ok());
    EXPECT_EQ(tool->valueLabel(), "Y from last hole");
    EXPECT_EQ(h.controller.setValueText("0"), "");
    ASSERT_TRUE(h.controller.triggerAction("field:x").ok());
    EXPECT_EQ(h.controller.setValueText("40"), "");
    // Clicking a placed hole makes it the current one again.
    h.clickAt(h.screen({30, 15, 5}));
    EXPECT_EQ(tool->positions().size(), 3u);
    EXPECT_EQ(tool->current(), 0);
    EXPECT_NEAR((tool->positions()[2] - Vec2{50, 5}).length(), 0, 1e-9);

    // M4 close fit (ISO 273: 4.3), with a counterbore for the head (8 x 4.4).
    ASSERT_TRUE(h.controller.triggerAction("size:3").ok());
    ASSERT_TRUE(h.controller.triggerAction("fit:close").ok());
    EXPECT_DOUBLE_EQ(tool->diameter(), 4.3);
    ASSERT_TRUE(h.controller.triggerAction("head:counterbore").ok());
    EXPECT_TRUE(tool->hasPreview()) << tool->error();
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double oneHole = kPi * 2.15 * 2.15 * 5 + kPi * (16 - 2.15 * 2.15) * 4.4;
    EXPECT_NEAR(h.volume(), 9000 - 3 * oneHole, 1e-6);
    ASSERT_EQ(h.document.body(h.body)->features().size(), 2u) << "one step for the set";
    bool listed = false;
    for (const auto& row : h.controller.historyRows())
        listed = listed || (row.name == "Holes" && row.detail.find("3 \xC3\x97 \xC3\x98" "4.30 mm") == 0
                            && row.detail.find("Counterbore") != std::string::npos
                            && row.detail.find("M4 close fit") != std::string::npos);
    EXPECT_TRUE(listed);
    ASSERT_TRUE(h.controller.undo());
    EXPECT_NEAR(h.volume(), 9000, 1e-6);
}

// A blind tap-drill hole: the depth becomes the field to type; the tool
// remembers its settings for the next face.
TEST(HoleInteraction, BlindTapHoleAndRememberedSettings)
{
    HoleHarness h;
    plateWithTopSelected(h);
    ASSERT_TRUE(h.controller.triggerAction("hole").ok());
    ASSERT_TRUE(h.controller.triggerAction("fit:tap").ok());
    EXPECT_DOUBLE_EQ(holeTool(h)->diameter(), 2.5) << "M3 tap";
    ASSERT_TRUE(h.controller.triggerAction("throughAll").ok());
    EXPECT_EQ(holeTool(h)->valueLabel(), "Depth");
    EXPECT_EQ(h.controller.setValueText("3"), "");
    h.clickAt(h.screen({20, 10, 5}));
    ASSERT_EQ(holeTool(h)->positions().size(), 1u);
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.volume(), 9000 - kPi * 1.25 * 1.25 * 3, 1e-6);

    h.clickAt(h.screen({40, 20, 5}));
    ASSERT_TRUE(h.controller.triggerAction("hole").ok());
    ASSERT_NE(holeTool(h), nullptr);
    EXPECT_DOUBLE_EQ(holeTool(h)->diameter(), 2.5);
    EXPECT_FALSE(holeTool(h)->settings().throughAll);
    EXPECT_DOUBLE_EQ(holeTool(h)->settings().depth, 3.0);
    // Esc leaves the tool without drilling anything.
    h.controller.keyPress(Key::Escape);
    EXPECT_EQ(h.controller.operation(), nullptr);
    EXPECT_EQ(h.document.body(h.body)->features().size(), 2u);
}

TEST(HoleInteraction, HoleToolNeedsAFlatFace)
{
    HoleHarness h;
    EXPECT_FALSE(h.controller.runTool("hole").ok());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_EQ(h.messages.back(), "Click a flat face, then Hole, then click or tap where each hole goes.");
    plateWithTopSelected(h);
    ASSERT_TRUE(h.controller.runTool("hole").ok());
    EXPECT_NE(holeTool(h), nullptr);
}
