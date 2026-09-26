// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The model tree and history editing, driven through the controller.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"

#include <filesystem>

using namespace os;
using namespace os::interact;

namespace {

// MVP part: 60 x 40 sketch rectangle, extruded 20 mm, 4 vertical edges filleted 3 mm.
std::unique_ptr<doc::Document> mvpPart(Uuid* extrudeOut = nullptr, Uuid* filletOut = nullptr)
{
    auto d = std::make_unique<doc::Document>();
    auto sk = std::make_unique<sketch::Sketch>();
    sk->setName("Sketch 1");
    const auto r = sketch::addRectangle(*sk, {0, 0}, {60, 40}, sketch::kOriginId);
    sk->addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60});
    sk->addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40});
    sketch::solve(*sk);
    const Uuid sketchId = sk->id();
    d->addSketch(std::move(sk));

    auto regions = doc::sketchRegions(*d->sketch(sketchId));
    auto extrude = std::make_unique<doc::ExtrudeFeature>();
    extrude->sketchId = sketchId;
    extrude->profiles = {doc::makeProfileRef(regions.value().front(), *d->sketch(sketchId))};
    extrude->distance = 20;
    if (extrudeOut)
        *extrudeOut = extrude->id();
    auto body = std::make_unique<doc::Body>();
    body->setName("Body 1");
    body->insertFeature(std::move(extrude), 0);
    doc::Body& b = d->addBody(std::move(body));
    auto fillet = test::filletVertical(b.shape(), 3.0);
    if (filletOut)
        *filletOut = fillet->id();
    d->insertFeature(b.id(), std::move(fillet));
    return d;
}

const HistoryRow* row(const std::vector<HistoryRow>& rows, const Uuid& id)
{
    for (const auto& r : rows)
        if (r.id == id)
            return &r;
    return nullptr;
}

} // namespace

TEST(History, RowsDescribeTheModel)
{
    auto d = mvpPart();
    cmd::UndoStack stack;
    InteractionController c(*d, stack);
    const auto rows = c.historyRows();
    ASSERT_EQ(rows.size(), 4u); // sketch, body, extrude, fillet
    EXPECT_EQ(rows[0].kind, HistoryRow::Kind::Sketch);
    EXPECT_EQ(rows[0].detail, "Fully defined");
    EXPECT_FALSE(rows[0].canDelete) << "used by the extrusion";
    EXPECT_EQ(rows[1].kind, HistoryRow::Kind::Body);
    EXPECT_EQ(rows[2].name, "Extrude");
    EXPECT_NE(rows[2].detail.find("20.00 mm"), std::string::npos);
    EXPECT_NE(rows[2].detail.find("New body"), std::string::npos);
    EXPECT_FALSE(rows[2].canDelete) << "first step of the body";
    ASSERT_EQ(rows[2].parameters.size(), 1u);
    EXPECT_EQ(rows[2].parameters[0].valueText, "20.00 mm");
    EXPECT_EQ(rows[3].name, "Fillet");
    EXPECT_NE(rows[3].detail.find("4 edges"), std::string::npos);
    EXPECT_EQ(rows[3].status, HistoryRow::Status::Ok);
}

// The MVP promise: reopen a saved part, change a dimension, everything regenerates.
TEST(History, ReopenedModelRemainsEditable)
{
    Uuid extrudeId, filletId;
    auto original = mvpPart(&extrudeId, &filletId);
    const auto path = std::filesystem::temp_directory_path() / "openshape_history.openshape";
    ASSERT_TRUE(io::saveProject(*original, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    cmd::UndoStack stack;
    InteractionController c(d, stack);

    ASSERT_TRUE(c.setFeatureParameter(extrudeId, "distance", "30").ok());
    const doc::Body& body = *d.bodies().front();
    EXPECT_FALSE(body.hasFailures());
    EXPECT_NEAR(geom::boundingBox(body.shape()).size().z, 30.0, 1e-6);
    EXPECT_NEAR(geom::volume(body.shape()), (2400.0 - 4 * (9 - kPi * 9 / 4)) * 30, 1e-3);

    ASSERT_TRUE(c.setFeatureParameter(filletId, "size", "2mm").ok());
    EXPECT_NEAR(geom::volume(body.shape()), (2400.0 - 4 * (4 - kPi * 4 / 4)) * 30, 1e-3);

    c.undo();
    c.undo();
    EXPECT_NEAR(geom::boundingBox(body.shape()).size().z, 20.0, 1e-6);
}

TEST(History, BreakingEditIsKeptMarkedAndUndoable)
{
    Uuid extrudeId, filletId;
    auto d = mvpPart(&extrudeId, &filletId);
    cmd::UndoStack stack;
    InteractionController c(*d, stack);
    std::vector<std::string> messages;
    c.onMessage = [&](const std::string& m) { messages.push_back(m); };

    // A 25 mm fillet cannot fit on a 40 mm wide part.
    ASSERT_TRUE(c.setFeatureParameter(filletId, "size", "25").ok());
    const auto rows = c.historyRows();
    const HistoryRow* fillet = row(rows, filletId);
    ASSERT_NE(fillet, nullptr);
    EXPECT_EQ(fillet->status, HistoryRow::Status::Failed);
    // Recompute does not search for a size that works (previews do).
    EXPECT_EQ(fillet->message, "Unable to create this fillet. Try a smaller radius.");
    EXPECT_EQ(row(rows, d->bodies().front()->id())->status, HistoryRow::Status::Failed);
    EXPECT_FALSE(messages.empty());
    // The body shows the last good shape: the plain extrusion.
    EXPECT_NEAR(geom::volume(d->bodies().front()->shape()), 48000, 1e-4);

    c.undo();
    EXPECT_FALSE(d->bodies().front()->hasFailures());
    EXPECT_EQ(row(c.historyRows(), filletId)->status, HistoryRow::Status::Ok);
}

TEST(History, BadTextIsRejected)
{
    Uuid extrudeId;
    auto d = mvpPart(&extrudeId);
    cmd::UndoStack stack;
    InteractionController c(*d, stack);
    const Status s = c.setFeatureParameter(extrudeId, "distance", "twenty");
    EXPECT_FALSE(s.ok());
    EXPECT_FALSE(s.userMessage().empty());
    EXPECT_FALSE(c.setFeatureParameter(extrudeId, "distance", "0").ok());
    EXPECT_EQ(stack.size(), 0u);
}

TEST(History, SuppressAndDelete)
{
    Uuid extrudeId, filletId;
    auto d = mvpPart(&extrudeId, &filletId);
    cmd::UndoStack stack;
    InteractionController c(*d, stack);
    const doc::Body& body = *d->bodies().front();

    ASSERT_TRUE(c.setFeatureSuppressed(filletId, true).ok());
    EXPECT_NEAR(geom::volume(body.shape()), 48000, 1e-4);
    EXPECT_EQ(row(c.historyRows(), filletId)->status, HistoryRow::Status::Suppressed);
    ASSERT_TRUE(c.setFeatureSuppressed(filletId, false).ok());
    EXPECT_LT(geom::volume(body.shape()), 48000);

    EXPECT_FALSE(c.setFeatureSuppressed(extrudeId, true).ok()) << "base step cannot be suppressed";
    EXPECT_FALSE(c.deleteFeature(extrudeId).ok()) << "base step cannot be deleted";
    ASSERT_TRUE(c.deleteFeature(filletId).ok());
    EXPECT_EQ(body.features().size(), 1u);
    c.undo();
    EXPECT_EQ(body.features().size(), 2u);
}

TEST(History, VisibilityAndDeletion)
{
    auto d = mvpPart();
    cmd::UndoStack stack;
    InteractionController c(*d, stack);
    const Uuid bodyId = d->bodies().front()->id();
    const Uuid sketchId = d->sketches().front()->id();

    ASSERT_TRUE(c.setBodyVisible(bodyId, false).ok());
    EXPECT_TRUE(c.renderScene().bodies.empty());
    c.undo();
    EXPECT_EQ(c.renderScene().bodies.size(), 1u);

    ASSERT_TRUE(c.setSketchVisible(sketchId, false).ok());
    EXPECT_TRUE(c.renderScene().sketches.empty());
    c.undo();

    EXPECT_FALSE(c.deleteSketch(sketchId).ok()) << "used by the extrusion";
    ASSERT_TRUE(c.deleteBody(bodyId).ok());
    ASSERT_TRUE(c.deleteSketch(sketchId).ok());
    EXPECT_TRUE(d->bodies().empty());
    EXPECT_TRUE(d->sketches().empty());
    c.undo();
    c.undo();
    EXPECT_EQ(d->bodies().size(), 1u);
    EXPECT_EQ(d->sketches().size(), 1u);
}
