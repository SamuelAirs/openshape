// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "TestHelpers.h"

using namespace os;
using namespace os::test;

namespace {

struct Fixture {
    doc::Document document;
    cmd::UndoStack stack;

    Uuid createCube(double size)
    {
        auto command = std::make_unique<cmd::CreateBodyCommand>("Box", boxFeature(size, size, size));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(stack.push(std::move(command), document).ok());
        return id;
    }
};

} // namespace

TEST(Commands, CreateBodyExecuteUndoRedo)
{
    Fixture f;
    const Uuid id = f.createCube(20);
    ASSERT_NE(f.document.body(id), nullptr);
    EXPECT_TRUE(f.stack.canUndo());
    EXPECT_FALSE(f.stack.canRedo());

    EXPECT_TRUE(f.stack.undo(f.document));
    EXPECT_EQ(f.document.body(id), nullptr);
    EXPECT_TRUE(f.stack.canRedo());

    EXPECT_TRUE(f.stack.redo(f.document).ok());
    ASSERT_NE(f.document.body(id), nullptr) << "redo must recreate the same UUID";
    EXPECT_NEAR(geom::volume(f.document.body(id)->shape()), 8000.0, 1e-6);
}

TEST(Commands, NewCommandInvalidatesRedo)
{
    Fixture f;
    f.createCube(10);
    f.createCube(20);
    ASSERT_TRUE(f.stack.undo(f.document));
    EXPECT_TRUE(f.stack.canRedo());
    f.createCube(30);
    EXPECT_FALSE(f.stack.canRedo());
    EXPECT_EQ(f.stack.size(), 2u);
    EXPECT_EQ(f.document.bodies().size(), 2u);
}

TEST(Commands, FailedCommandLeavesNoTrace)
{
    Fixture f;
    const Uuid id = f.createCube(10);
    const auto revisionBefore = f.document.body(id)->shapeRevision();
    auto fillet = filletVertical(f.document.body(id)->shape(), 8.0); // too large
    auto status = f.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(fillet)), f.document);
    EXPECT_FALSE(status.ok());
    EXPECT_EQ(status.error(), ErrorCode::FilletRadiusTooLarge);
    EXPECT_EQ(status.userMessage(), "Unable to create this fillet. Try a smaller radius.");
    EXPECT_EQ(f.stack.size(), 1u);
    EXPECT_EQ(f.document.body(id)->features().size(), 1u);
    EXPECT_NEAR(geom::volume(f.document.body(id)->shape()), 1000.0, 1e-6);
    EXPECT_FALSE(f.document.body(id)->hasFailures());
    (void)revisionBefore;
}

// Milestone 0 acceptance, geometry verification for steps 12, 14 and 16:
// 20 mm cube, push top face by 15 -> 35 mm; undo -> 20 mm; redo -> 35 mm.
TEST(Commands, Milestone0PushPullUndoRedo)
{
    Fixture f;
    const Uuid id = f.createCube(20);
    doc::Body* body = f.document.body(id);
    ASSERT_NE(body, nullptr);
    EXPECT_NEAR(height(*body), 20.0, 1e-6);

    auto feature = pushPull(body->shape(), {0, 0, 1}, 15.0);
    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(feature)), f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 35.0, 1e-6);   // step 12
    EXPECT_NEAR(geom::volume(f.document.body(id)->shape()), 20 * 20 * 35.0, 1e-4);

    ASSERT_TRUE(f.stack.undo(f.document));
    EXPECT_NEAR(height(*f.document.body(id)), 20.0, 1e-6);   // step 14

    ASSERT_TRUE(f.stack.redo(f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 35.0, 1e-6);   // step 16
}

TEST(Commands, SetParameterUndoRedo)
{
    Fixture f;
    const Uuid id = f.createCube(20);
    const Uuid boxId = f.document.body(id)->features().front()->id();
    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "height", 32.0), f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 32.0, 1e-6);
    f.stack.undo(f.document);
    EXPECT_NEAR(height(*f.document.body(id)), 20.0, 1e-6);
    ASSERT_TRUE(f.stack.redo(f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 32.0, 1e-6);
}

TEST(Commands, SetParameterRejectsInvalidValue)
{
    Fixture f;
    const Uuid id = f.createCube(20);
    const Uuid boxId = f.document.body(id)->features().front()->id();
    EXPECT_FALSE(f.stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "height", -3.0), f.document).ok());
    EXPECT_FALSE(f.stack.push(std::make_unique<cmd::SetParameterCommand>(boxId, "nonsense", 3.0), f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 20.0, 1e-6);
    EXPECT_EQ(f.stack.size(), 1u);
}

TEST(Commands, DeleteBodyAndFeatureUndo)
{
    Fixture f;
    const Uuid id = f.createCube(20);
    auto feature = pushPull(f.document.body(id)->shape(), {0, 0, 1}, 5.0);
    const Uuid featureId = feature->id();
    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(feature)), f.document).ok());

    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::DeleteFeatureCommand>(featureId), f.document).ok());
    EXPECT_NEAR(height(*f.document.body(id)), 20.0, 1e-6);
    f.stack.undo(f.document);
    EXPECT_NEAR(height(*f.document.body(id)), 25.0, 1e-6);
    EXPECT_EQ(f.document.body(id)->features()[1]->id(), featureId);

    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::DeleteBodyCommand>(id), f.document).ok());
    EXPECT_EQ(f.document.body(id), nullptr);
    f.stack.undo(f.document);
    ASSERT_NE(f.document.body(id), nullptr);
    EXPECT_NEAR(height(*f.document.body(id)), 25.0, 1e-6);
}

TEST(Commands, VisibilityUndo)
{
    Fixture f;
    const Uuid id = f.createCube(5);
    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::SetBodyVisibilityCommand>(id, false), f.document).ok());
    EXPECT_FALSE(f.document.body(id)->isVisible());
    f.stack.undo(f.document);
    EXPECT_TRUE(f.document.body(id)->isVisible());
}

TEST(Commands, CleanStateTracking)
{
    Fixture f;
    EXPECT_TRUE(f.stack.isClean());
    f.createCube(5);
    EXPECT_FALSE(f.stack.isClean());
    f.stack.setClean();
    EXPECT_TRUE(f.stack.isClean());
    f.stack.undo(f.document);
    EXPECT_FALSE(f.stack.isClean());
    ASSERT_TRUE(f.stack.redo(f.document).ok());
    EXPECT_TRUE(f.stack.isClean());
    f.stack.undo(f.document);
    f.createCube(7); // clean state was in the discarded redo branch
    EXPECT_FALSE(f.stack.isClean());
}

TEST(Commands, MaxDepthDropsOldest)
{
    doc::Document document;
    cmd::UndoStack stack(3);
    for (int i = 0; i < 5; ++i)
        ASSERT_TRUE(stack.push(std::make_unique<cmd::CreateBodyCommand>("Box", boxFeature(1, 1, 1)), document).ok());
    EXPECT_EQ(stack.size(), 3u);
    int undos = 0;
    while (stack.undo(document))
        ++undos;
    EXPECT_EQ(undos, 3);
    EXPECT_EQ(document.bodies().size(), 2u);
}
