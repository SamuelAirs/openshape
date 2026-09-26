// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrude with a draft through the interaction layer: the Draft action in the
// value chip, a typed angle, the arrow still setting the distance.
#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace os;
using namespace os::interact;

namespace {

PointerEvent at(Vec2 p)
{
    PointerEvent e;
    e.position = p;
    e.button = PointerButton::Left;
    e.device = PointerDevice::Mouse;
    return e;
}

double frustumVolume(double a1, double a2, double h)
{
    return h / 3 * (a1 + a2 + std::sqrt(a1 * a2));
}

} // namespace

TEST(DraftInteraction, DraftActionTypedAngleAndArrow)
{
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller(document, stack);
    controller.setViewportSize({1200, 800});
    // A 20 x 20 square sketch on the ground, then its profile selected.
    auto s = std::make_unique<sketch::Sketch>();
    (void)sketch::addRectangle(*s, {-10, -10}, {10, 10});
    document.addSketch(std::move(s));
    controller.documentChanged();
    controller.fitAll(false);
    const Vec2 inside = controller.camera().project({2, 3, 0});
    controller.pointerPress(at(inside));
    controller.pointerRelease(at(inside));
    const auto* extrude = dynamic_cast<const ExtrudeOperation*>(controller.operation());
    ASSERT_NE(extrude, nullptr);
    bool offered = false;
    for (const auto& a : controller.contextActions())
        offered = offered || (a.id == "draft" && a.label == "Draft" && !a.active);
    EXPECT_TRUE(offered);

    EXPECT_EQ(controller.setValueText("12"), "");
    ASSERT_TRUE(controller.triggerAction("draft").ok());
    EXPECT_TRUE(extrude->editingDraft());
    EXPECT_EQ(extrude->valueLabel(), "Draft");
    EXPECT_TRUE(extrude->isAngle());
    EXPECT_DOUBLE_EQ(extrude->value(), 0.0);
    EXPECT_TRUE(extrude->hasPreview()) << "no draft yet: the straight extrusion";
    EXPECT_TRUE(extrude->canCommit());
    EXPECT_EQ(controller.setValueText("50"), "The draft closes the shape before the full height. Use a smaller angle or a shorter extrusion.");
    EXPECT_FALSE(extrude->canCommit());
    EXPECT_EQ(controller.setValueText("4"), "");
    EXPECT_NEAR(extrude->distance(), 12, 1e-12);
    EXPECT_EQ(controller.operationValueText(), "4.0\xC2\xB0");
    bool shown = false;
    for (const auto& a : controller.contextActions())
        shown = shown || (a.id == "draft" && a.label == "Draft 4.0\xC2\xB0" && a.active);
    EXPECT_TRUE(shown);

    // Back to the distance: the draft is kept.
    ASSERT_TRUE(controller.triggerAction("draft").ok());
    EXPECT_FALSE(extrude->editingDraft());
    EXPECT_DOUBLE_EQ(extrude->value(), 12.0);
    EXPECT_DOUBLE_EQ(extrude->draftDegrees(), 4.0);
    EXPECT_EQ(controller.setValueText("10"), "");
    ASSERT_TRUE(controller.commitOperation().ok());
    ASSERT_EQ(document.bodies().size(), 1u);
    const double t = 10 * std::tan(4 * kPi / 180);
    EXPECT_NEAR(geom::volume(document.bodies().front()->shape()), frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), 10), 1e-6);
    bool listed = false;
    for (const auto& row : controller.historyRows())
        listed = listed || (row.name == "Extrude" && row.detail.find("Draft 4.0\xC2\xB0") != std::string::npos);
    EXPECT_TRUE(listed);
}
