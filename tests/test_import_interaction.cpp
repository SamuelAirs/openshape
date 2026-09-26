// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Imported bodies behave like any other: names, undo, push/pull, fillet,
// combine and export again, driven like the UI drives them.

#include "commands/Command.h"
#include "core/Uuid.h"
#include "document/Document.h"
#include "geometry/Exchange.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"

#include <gtest/gtest.h>

#include <filesystem>

using namespace os;
using namespace os::interact;

namespace {

std::filesystem::path temp(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_importui_" + Uuid::generate().toString() + "_" + name);
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

    Vec2 screen(const Vec3& p) const { return controller.camera().project(p); }
    void clickAt(Vec2 p)
    {
        PointerEvent e;
        e.position = p;
        e.button = PointerButton::Left;
        controller.pointerPress(e);
        controller.pointerRelease(e);
    }
    const doc::Body& body(std::size_t i) const { return *document.bodies()[i]; }
    double volume(std::size_t i) const { return geom::volume(body(i).shape()); }
};

// What the STEP importer delivers for a file with two named blocks and an
// unnamed one: exported by OpenShape's own writer and read back.
std::vector<geom::NamedShape> importedBlocks()
{
    const auto path = temp("blocks.step");
    EXPECT_TRUE(geom::exportStep({{"Block", geom::makeBox({0, 0, 0}, {20, 20, 10}).value()},
                                  {"", geom::makeBox({40, 0, 0}, {10, 10, 10}).value()},
                                  {"Block", geom::makeBox({0, 40, 0}, {10, 10, 30}).value()}},
                                 path)
                    .ok());
    auto imported = geom::importStep(path);
    std::filesystem::remove(path);
    EXPECT_TRUE(imported.ok()) << imported.developerMessage();
    return imported.ok() ? imported.value() : std::vector<geom::NamedShape>{};
}

} // namespace

TEST(ImportInteraction, BodiesAreNamedAndOneUndoStep)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok()); // "Body 1" already there
    const auto blocks = importedBlocks();
    ASSERT_EQ(blocks.size(), 3u);
    ASSERT_TRUE(h.controller.importBodies(blocks, "blocks.step").ok());
    ASSERT_EQ(h.document.bodies().size(), 4u);
    EXPECT_EQ(h.body(1).name(), "Block");
    EXPECT_EQ(h.body(2).name(), "Imported 1");
    EXPECT_EQ(h.body(3).name(), "Block 2");
    EXPECT_EQ(h.stack.undoLabel(), "Import 3 bodies");
    EXPECT_NEAR(h.volume(1), 4000.0, 1e-6);
    EXPECT_NEAR(h.volume(3), 3000.0, 1e-6);
    EXPECT_TRUE(h.controller.selection().empty());

    // The Model panel shows the import step with its file.
    bool found = false;
    for (const auto& row : h.controller.historyRows())
        if (row.kind == HistoryRow::Kind::Feature && row.name == "Import") {
            found = true;
            EXPECT_EQ(row.detail, "blocks.step");
        }
    EXPECT_TRUE(found);

    ASSERT_TRUE(h.controller.undo());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    ASSERT_TRUE(h.controller.redo());
    EXPECT_EQ(h.document.bodies().size(), 4u);

    // A second import of the same file keeps names unique.
    ASSERT_TRUE(h.controller.importBodies({blocks[0], blocks[1]}, "blocks.step").ok());
    EXPECT_EQ(h.body(4).name(), "Block 3");
    EXPECT_EQ(h.body(5).name(), "Imported 2");
    EXPECT_EQ(h.stack.undoLabel(), "Import 2 bodies");
    ASSERT_TRUE(h.controller.importBodies({blocks[2]}, "blocks.step").ok());
    EXPECT_EQ(h.stack.undoLabel(), "Import Block 4");

    EXPECT_FALSE(h.controller.importBodies({}, "none.step").ok());
}

TEST(ImportInteraction, PushPullFilletCombineAndExportAgain)
{
    Harness h;
    const auto blocks = importedBlocks();
    ASSERT_TRUE(h.controller.importBodies({blocks[0], blocks[1]}, "blocks.step").ok());
    h.controller.setStandardView(StandardView::Isometric, false);
    h.controller.fitAll(false);

    // Push/pull the top of "Block" (20 x 20 x 10) up to 15 mm.
    h.clickAt(h.screen({10, 10, 10}));
    ASSERT_NE(h.controller.operation(), nullptr);
    EXPECT_EQ(h.controller.operation()->title(), "Push/Pull");
    EXPECT_EQ(h.controller.setValueText("15"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    EXPECT_NEAR(h.volume(0), 20.0 * 20.0 * 15.0, 1e-6);
    EXPECT_EQ(h.body(0).features().size(), 2u);

    // Round its front vertical edge (x = 20, y = 0) with R 2.
    h.controller.cancelOperation();
    h.clickAt(h.screen({20, 0, 7.5}));
    ASSERT_NE(h.controller.operation(), nullptr);
    ASSERT_EQ(h.controller.operation()->title(), "Fillet");
    EXPECT_EQ(h.controller.setValueText("2"), "");
    ASSERT_TRUE(h.controller.commitOperation().ok());
    const double filleted = 20.0 * 20.0 * 15.0 - (4.0 - kPi) * 15.0;
    EXPECT_NEAR(h.volume(0), filleted, 1e-6);

    // Union with the other imported block (they do not touch: two pieces).
    h.controller.cancelOperation();
    ASSERT_TRUE(h.controller.selectBody(h.body(0).id(), false).ok());
    ASSERT_TRUE(h.controller.selectBody(h.body(1).id(), true).ok());
    ASSERT_TRUE(h.controller.triggerAction("union").ok());
    EXPECT_FALSE(h.body(1).isVisible());
    EXPECT_NEAR(h.volume(0), filleted + 1000.0, 1e-6);

    // Export again and import the result: the same solids.
    const auto path = temp("again.step");
    ASSERT_TRUE(geom::exportStep({{h.body(0).name(), h.body(0).shape()}}, path).ok());
    auto again = geom::importStep(path);
    std::filesystem::remove(path);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    ASSERT_EQ(again.value().size(), 2u) << "two separate pieces, two solids";
    double total = 0;
    for (const auto& s : again.value()) {
        total += geom::volume(s.shape);
        EXPECT_EQ(s.name.rfind("Block", 0), 0u) << s.name;
    }
    EXPECT_NEAR(total, filleted + 1000.0, 1e-5);

    // Undo everything back to the import, then the import itself.
    while (h.stack.canUndo())
        ASSERT_TRUE(h.controller.undo());
    EXPECT_TRUE(h.document.bodies().empty());
}

TEST(ImportInteraction, PartsTooLargeForAProjectAreRefused)
{
    Harness h;
    const auto blocks = importedBlocks();
    ASSERT_EQ(blocks.size(), 3u);
    // What one block's geometry takes in a project.
    doc::ImportedFeature probe;
    probe.setShape(blocks[0].shape);
    const std::uint64_t one = probe.brepText().size();
    ASSERT_GT(one, 100u);

    EXPECT_FALSE(h.controller.importBodies({blocks[0]}, "blocks.step", one - 1).ok());
    EXPECT_TRUE(h.document.bodies().empty()) << "nothing imported";
    EXPECT_FALSE(h.stack.canUndo());
    ASSERT_FALSE(h.messages.empty());
    EXPECT_NE(h.messages.back().find("too large to keep in a project"), std::string::npos) << h.messages.back();

    // Within the budget it comes in; the next import counts what is there already.
    ASSERT_TRUE(h.controller.importBodies({blocks[0]}, "blocks.step", one + one / 2).ok());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    EXPECT_FALSE(h.controller.importBodies({blocks[0]}, "blocks.step", one + one / 2).ok());
    EXPECT_EQ(h.document.bodies().size(), 1u);
    EXPECT_EQ(h.stack.undoLabel(), "Import Block");
    ASSERT_TRUE(h.controller.importBodies({blocks[0]}, "blocks.step", 2 * one).ok());
    EXPECT_EQ(h.document.bodies().size(), 2u);
}
