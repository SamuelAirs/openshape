// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Robustness: seeded random modeling sessions (boxes, push/pull, fillets,
// chamfers, shells, sketches, extrusions, moves, rotations, mirrors,
// patterns, booleans, history edits, suppression, deletions), then
// - undo everything: the document equals the start; redo everything: it
//   equals the end;
// - random interleavings of undo, redo and new edits: every state reached by
//   undo or redo equals the state first recorded at that point;
// - save and open at random points: the reopened document equals the live
//   one, and a full recompute after loading changes nothing.
#include "StressHarness.h"

#include "io/ProjectFile.h"

#include <chrono>
#include <filesystem>

using namespace os;
using namespace os::test;

namespace {

std::string joined(const std::vector<std::string>& log)
{
    std::string out;
    for (const auto& s : log)
        out += (out.empty() ? "" : ", ") + s;
    return out;
}

std::filesystem::path uniqueTempPath(const std::string& stem)
{
    static std::mt19937_64 rng(std::random_device{}());
    return std::filesystem::temp_directory_path()
         / ("openshape_" + stem + "_" + std::to_string(rng()) + ".openshape");
}

} // namespace

// A body that combines with a body that itself combines with a third one:
// editing the third must update both (dependents are followed transitively),
// and a saved copy must reopen with the same shapes whatever the body order.
TEST(Dependencies, ChainedCombinesFollowAnUpstreamEdit)
{
    doc::Document d;
    cmd::UndoStack stack;
    auto box = [&](Vec3 origin, double size) {
        auto f = std::make_unique<doc::BoxFeature>();
        f->origin = origin;
        f->size = {size, size, size};
        auto command = std::make_unique<cmd::CreateBodyCommand>(d.nextBodyName(), std::move(f));
        const Uuid id = command->bodyId();
        EXPECT_TRUE(stack.push(std::move(command), d).ok());
        return id;
    };
    auto combine = [&](const Uuid& target, const Uuid& tool) {
        auto f = std::make_unique<doc::CombineFeature>();
        f->toolBody = tool;
        f->mode = doc::CombineMode::Union;
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddFeatureCommand>(target, std::move(f)), d).ok());
    };
    const Uuid a = box({0, 0, 0}, 10);
    const Uuid b = box({10, 0, 0}, 10);
    const Uuid c = box({20, 0, 0}, 10);
    combine(b, c); // B = B + C
    combine(a, b); // A = A + (B + C)
    EXPECT_NEAR(geom::volume(d.body(a)->shape()), 3000.0, 1e-6);

    // C grows to 20 mm: B and, through it, A follow.
    const Uuid cBox = d.body(c)->features()[0]->id();
    ASSERT_TRUE(stack.push(std::make_unique<cmd::SetParameterCommand>(cBox, "width", 20.0, false), d).ok());
    EXPECT_NEAR(geom::volume(d.body(b)->shape()), 1000.0 + 2000.0, 1e-6);
    EXPECT_NEAR(geom::volume(d.body(a)->shape()), 1000.0 + 1000.0 + 2000.0, 1e-6);

    // Undo returns all three.
    ASSERT_TRUE(stack.undo(d));
    EXPECT_NEAR(geom::volume(d.body(a)->shape()), 3000.0, 1e-6);

    // Reopened (A is listed first, before the bodies it uses): same shapes.
    ASSERT_TRUE(stack.redo(d).ok());
    auto loaded = io::documentFromJson(io::documentToJson(d));
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    EXPECT_NEAR(geom::volume(loaded.value()->body(a)->shape()), 4000.0, 1e-6);
    EXPECT_FALSE(loaded.value()->body(a)->hasFailures());
}

class UndoRedoStress : public ::testing::TestWithParam<unsigned> {};

TEST_P(UndoRedoStress, UndoAllRestoresStartRedoAllRestoresEnd)
{
    StressSession s(GetParam());
    const Snapshot start = s.snapshot();
    std::vector<std::string> log;
    const int applied = s.build(90, &log);
    ASSERT_GE(applied, 50) << joined(log);
    const Snapshot end = s.snapshot();
    ASSERT_EQ(s.stack.index(), std::size_t(applied));

    while (s.controller.undo()) {
    }
    EXPECT_EQ(s.stack.index(), 0u);
    EXPECT_TRUE(sameState(start, s.snapshot())) << "after undoing: " << joined(log);
    EXPECT_TRUE(s.document.bodies().empty());

    while (s.controller.redo()) {
    }
    EXPECT_EQ(s.stack.index(), s.stack.size()) << "a step that once worked failed to redo";
    EXPECT_TRUE(sameState(end, s.snapshot())) << "after redoing: " << joined(log);
}

INSTANTIATE_TEST_SUITE_P(Seeds, UndoRedoStress, ::testing::Values(11u, 22u, 33u));

class InterleavedStress : public ::testing::TestWithParam<unsigned> {};

// New edits, undos and redos in random order. snapshots[i] is the state with
// i steps applied; a new edit discards the redo branch as the stack does.
TEST_P(InterleavedStress, EveryUndoRedoStateMatchesTheRecordedOne)
{
    StressSession s(GetParam());
    std::mt19937 rng(GetParam() * 7919u);
    std::vector<Snapshot> snapshots{s.snapshot()};
    std::vector<std::string> log;
    int undos = 0, redos = 0, edits = 0;
    for (int step = 0; step < 150; ++step) {
        const double roll = std::uniform_real_distribution<double>(0, 1)(rng);
        if (roll < 0.25 && s.stack.canUndo()) {
            ASSERT_TRUE(s.controller.undo());
            ++undos;
            log.push_back("undo");
            ASSERT_TRUE(sameState(snapshots[s.stack.index()], s.snapshot())) << joined(log);
        } else if (roll < 0.45 && s.stack.canRedo()) {
            ASSERT_TRUE(s.controller.redo()) << "redo failed: " << joined(log);
            ++redos;
            log.push_back("redo");
            ASSERT_TRUE(sameState(snapshots[s.stack.index()], s.snapshot())) << joined(log);
        } else {
            const std::size_t before = s.stack.index();
            std::string what;
            if (s.modeler.edit(&what)) {
                ++edits;
                log.push_back(what);
                snapshots.resize(before + 1);
                snapshots.push_back(s.snapshot());
                ASSERT_EQ(s.stack.index(), snapshots.size() - 1);
                ASSERT_EQ(s.stack.size(), s.stack.index()) << "a new edit must discard the redo branch";
            }
        }
    }
    EXPECT_GE(edits, 50);
    EXPECT_GE(undos, 10);
    EXPECT_GE(redos, 5);
}

INSTANTIATE_TEST_SUITE_P(Seeds, InterleavedStress, ::testing::Values(5u, 6u));

class SaveOpenStress : public ::testing::TestWithParam<unsigned> {};

// Saves at random points, reopens, and compares with the live document.
TEST_P(SaveOpenStress, ReopenedDocumentEqualsLiveOne)
{
    StressSession s(GetParam());
    std::mt19937 rng(GetParam() * 104729u);
    std::vector<std::string> log;
    int saves = 0;
    for (int round = 0; round < 6; ++round) {
        s.build(std::uniform_int_distribution<int>(6, 14)(rng), &log);
        const Snapshot live = s.snapshot();
        const auto path = uniqueTempPath("stress");
        ASSERT_TRUE(io::saveProject(s.document, path).ok());
        auto loaded = io::loadProject(path);
        std::filesystem::remove(path);
        ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
        ++saves;
        doc::Document& reopened = *loaded.value();
        MetricsCache cache;
        ASSERT_TRUE(sameState(live, snapshotOf(reopened, cache))) << "after " << joined(log);
        // A full recompute of the reopened model gives the same geometry.
        reopened.recomputeAll();
        ASSERT_TRUE(sameState(live, snapshotOf(reopened, cache))) << "recompute after " << joined(log);
        // And it saves to the same document JSON.
        EXPECT_EQ(io::documentToJson(reopened), io::documentToJson(s.document));
    }
    EXPECT_EQ(saves, 6);
}

INSTANTIATE_TEST_SUITE_P(Seeds, SaveOpenStress, ::testing::Values(3u, 4u));
