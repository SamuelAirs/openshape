// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference photos through the interaction layer: adding one frames it, the
// Model panel's rows and value edits (one undo step each), Set scale, the
// render scene (drawn, depth order data, hidden), clicks going through to
// what is behind, and the point under the pointer on its plane.

#include "commands/DocumentCommands.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

using namespace os;
using namespace os::interact;

namespace {

std::string pngSignature()
{
    return std::string("\x89PNG\r\n\x1A\n", 8) + std::string(64, '\0');
}

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
    // A 400 x 300 px picture, 200 mm wide (150 high), on the ground.
    Uuid addPhoto()
    {
        doc::ReferenceImage r;
        r.name = document.nextReferenceName();
        r.image = doc::makeReferenceImageData(pngSignature(), 400, 300, "png").value();
        r.width = 200;
        EXPECT_TRUE(controller.addReferenceImage(r).ok());
        controller.skipAnimation();
        return r.id();
    }
    const HistoryRow* row(const Uuid& id) const
    {
        static std::vector<HistoryRow> rows;
        rows = controller.historyRows();
        const auto it = std::find_if(rows.begin(), rows.end(), [&](const HistoryRow& r) { return r.id == id; });
        return it == rows.end() ? nullptr : &*it;
    }
};

} // namespace

TEST(ReferenceInteraction, AddFramesItInTopView)
{
    Harness h;
    const Uuid id = h.addPhoto();
    EXPECT_GT(h.controller.camera().backward().z, 0.999) << "looking down on the ground";
    // Its corners are on screen, and it fills a good part of the view.
    const auto corners = doc::referenceCorners(*h.document.reference(id), h.document);
    for (const Vec3& c : corners) {
        const Vec2 p = h.controller.camera().project(c);
        EXPECT_GE(p.x, -1);
        EXPECT_LE(p.x, 1201);
        EXPECT_GE(p.y, -1);
        EXPECT_LE(p.y, 801);
    }
    const Vec2 a = h.controller.camera().project(corners[0]), b = h.controller.camera().project(corners[1]);
    EXPECT_GT(std::abs(b.x - a.x), 400);
    EXPECT_EQ(h.stack.size(), 1u);
}

TEST(ReferenceInteraction, ModelPanelRowAndEdits)
{
    Harness h;
    const Uuid id = h.addPhoto();
    const HistoryRow* row = h.row(id);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->kind, HistoryRow::Kind::Reference);
    EXPECT_EQ(row->name, "Photo 1");
    std::vector<std::string> keys;
    for (const auto& p : row->parameters)
        keys.push_back(p.key);
    EXPECT_EQ(keys, (std::vector<std::string>{"width", "opacity", "rotation", "x", "y", "plane"}));
    EXPECT_EQ(row->parameters[1].valueText, "60 %");
    EXPECT_TRUE(row->parameters[1].isCount);
    EXPECT_TRUE(row->parameters[2].isAngle);
    EXPECT_EQ(row->parameters[5].choices, (std::vector<std::string>{"Ground", "Front", "Right"}));

    EXPECT_TRUE(h.controller.setFeatureParameter(id, "width", "300").ok());
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "opacity", "45 %").ok());
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "rotation", "30").ok());
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "x", "1 cm").ok());
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "y", "-5").ok());
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "plane", "Right").ok());
    const doc::ReferenceImage& r = *h.document.reference(id);
    EXPECT_DOUBLE_EQ(r.width, 300);
    EXPECT_DOUBLE_EQ(r.opacity, 0.45);
    EXPECT_NEAR(r.rotation, kPi / 6, 1e-12);
    EXPECT_DOUBLE_EQ(r.center.x, 10);
    EXPECT_DOUBLE_EQ(r.center.y, -5);
    EXPECT_EQ(r.plane, doc::ReferencePlane::YZ);
    EXPECT_EQ(h.stack.size(), 7u) << "one undo step each";

    EXPECT_FALSE(h.controller.setFeatureParameter(id, "opacity", "5").ok()) << "below 10 %";
    EXPECT_FALSE(h.controller.setFeatureParameter(id, "width", "0").ok());
    EXPECT_FALSE(h.controller.setFeatureParameter(id, "width", "wide").ok());
    EXPECT_FALSE(h.controller.setFeatureParameter(id, "plane", "Nowhere").ok());
    EXPECT_EQ(h.stack.size(), 7u) << "refused edits add nothing";

    h.controller.undo();
    EXPECT_EQ(h.document.reference(id)->plane, doc::ReferencePlane::XY);

    // A construction plane is offered by name, and the photo follows it.
    doc::Datum plane;
    plane.method = doc::DatumMethod::PlaneOffset;
    plane.originIndex = 2;
    plane.distance = 12;
    plane.setName("Plane 1");
    ASSERT_TRUE(h.stack.push(std::make_unique<cmd::AddDatumCommand>(plane), h.document).ok());
    h.controller.documentChanged();
    row = h.row(id);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->parameters[5].choices.back(), "Plane 1");
    EXPECT_TRUE(h.controller.setFeatureParameter(id, "plane", "Plane 1").ok());
    EXPECT_NEAR(doc::referenceCorners(*h.document.reference(id), h.document)[0].z, 12, 1e-9);
    // Deleting the plane: the photo goes to the ground.
    EXPECT_TRUE(h.controller.deleteDatum(plane.id()).ok());
    EXPECT_EQ(h.document.reference(id)->plane, doc::ReferencePlane::XY);
}

TEST(ReferenceInteraction, SetScaleAndPointsOnThePlane)
{
    Harness h;
    const Uuid id = h.addPhoto();
    // The point under the screen point of a world point is that point.
    const Vec2 screen = h.controller.camera().project({30, -20, 0});
    const auto local = h.controller.referencePointAt(id, screen);
    ASSERT_TRUE(local.has_value());
    EXPECT_NEAR(local->x, 30, 1e-6);
    EXPECT_NEAR(local->y, -20, 1e-6);
    const auto back = h.controller.referenceScreenPoint(id, *local);
    ASSERT_TRUE(back.has_value());
    EXPECT_NEAR(back->x, screen.x, 1e-6);
    // 50 mm measured on it is really 100: twice as wide.
    EXPECT_TRUE(h.controller.scaleReference(id, 100.0 / 50.0).ok());
    EXPECT_DOUBLE_EQ(h.document.reference(id)->width, 400);
    EXPECT_FALSE(h.controller.scaleReference(id, 0).ok());
    h.controller.undo();
    EXPECT_DOUBLE_EQ(h.document.reference(id)->width, 200);
}

TEST(ReferenceInteraction, DrawnHiddenAndClicksGoThrough)
{
    Harness h;
    const Uuid id = h.addPhoto();
    RenderScene scene = h.controller.renderScene();
    ASSERT_EQ(scene.references.size(), 1u);
    EXPECT_EQ(scene.references[0].id, id);
    EXPECT_NEAR(scene.references[0].normal.z, 1, 1e-12);
    EXPECT_DOUBLE_EQ(scene.references[0].opacity, 0.6);
    EXPECT_FALSE(scene.references[0].highlighted);
    // A click on the photo selects nothing (it is not geometry).
    const Vec2 p = h.controller.camera().project({0, 0, 0});
    PointerEvent e;
    e.position = p;
    e.button = PointerButton::Left;
    h.controller.pointerPress(e);
    h.controller.pointerRelease(e);
    EXPECT_TRUE(h.controller.selection().empty());
    // Hidden: not drawn, unless its row is hovered (then faint and outlined).
    EXPECT_TRUE(h.controller.setReferenceVisible(id, false).ok());
    EXPECT_TRUE(h.controller.renderScene().references.empty());
    h.controller.setHistoryHighlight(id);
    scene = h.controller.renderScene();
    ASSERT_EQ(scene.references.size(), 1u);
    EXPECT_TRUE(scene.references[0].highlighted);
    EXPECT_LT(scene.references[0].opacity, 0.6);
    h.controller.setHistoryHighlight(std::nullopt);
    EXPECT_TRUE(h.controller.deleteReference(id).ok());
    EXPECT_TRUE(h.controller.historyRows().empty());
    h.controller.undo();
    EXPECT_NE(h.document.reference(id), nullptr);
}
