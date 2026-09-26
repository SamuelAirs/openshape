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
