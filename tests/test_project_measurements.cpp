// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The measurements kept in a project (docs/MEASURE.md, "Measurements in the
// project"): the document's list, its one undoable command, the project
// file's "measurements" and the document's resolver for the typed-value
// parsers.

#include "TestHelpers.h"

#include "commands/DocumentCommands.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <filesystem>

using namespace os;
using namespace os::test;

namespace {

doc::ProjectMeasurement length(const std::string& name, double mm, const std::string& note = {})
{
    return doc::ProjectMeasurement{name, ValueKind::Length, mm, note};
}

doc::ProjectMeasurement angle(const std::string& name, double degrees)
{
    return doc::ProjectMeasurement{name, ValueKind::Angle, degrees, {}};
}

std::unique_ptr<doc::Document> boxDocument()
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    body->insertFeature(boxFeature(20, 20, 20), 0);
    d->addBody(std::move(body));
    return d;
}

} // namespace

TEST(ProjectMeasurements, TheDocumentFindsThemIgnoringCase)
{
    doc::Document d;
    EXPECT_TRUE(d.measurements().empty());
    EXPECT_FALSE(d.valueNames()) << "no names while there are none (stray letters read as before)";
    const auto revision = d.revision();
    d.setMeasurements({length("shelf_depth", 243), angle("corner", 90)});
    EXPECT_GT(d.revision(), revision);
    ASSERT_NE(d.measurement("Shelf_Depth"), nullptr);
    EXPECT_DOUBLE_EQ(d.measurement("SHELF_DEPTH")->value, 243.0);
    EXPECT_EQ(d.measurement("shelf"), nullptr);
    const NameResolver names = d.valueNames();
    ASSERT_TRUE(names);
    EXPECT_DOUBLE_EQ(*parseLength("shelf_depth - 2", LengthUnit::Millimeter, names).millimeters, 241.0);
    EXPECT_NEAR(*parseAngle("corner / 2", names).millimeters, 3.14159265358979323846 / 4, 1e-12);
    EXPECT_EQ(parseLength("depth", LengthUnit::Millimeter, names).error, "No measurement called 'depth' in this project");
    // A snapshot (previews on the worker) carries them.
    EXPECT_EQ(d.snapshot()->measurements(), d.measurements());
}

TEST(ProjectMeasurements, ListsAreChecked)
{
    EXPECT_EQ(doc::checkProjectMeasurements({length("a", 1), length("b", 2)}), "");
    EXPECT_EQ(doc::checkProjectMeasurements({length("wall", 1), length("Wall", 2)}), "There is already a measurement called wall.");
    EXPECT_EQ(doc::checkProjectMeasurements({length("in", 1)}), "'in' is a unit: choose another name.");
    EXPECT_NE(doc::checkProjectMeasurements({length("shelf depth", 1)}), "");
    EXPECT_NE(doc::checkProjectMeasurements({length("x", std::nan(""))}), "");
    EXPECT_NE(doc::checkProjectMeasurements({length("x", 2e7)}), "");
    EXPECT_NE(doc::checkProjectMeasurements({length("x", 1, std::string(300, 'n'))}), "");
    EXPECT_EQ(doc::uniqueMeasurementName({length("wall", 1), length("Wall_2", 1)}, "wall"), "wall_3");
    EXPECT_EQ(doc::uniqueMeasurementName({length("wall", 1)}, "shelf"), "shelf");
}

TEST(ProjectMeasurements, OneCommandAddsRenamesChangesAndDeletesWithUndo)
{
    doc::Document d;
    cmd::UndoStack stack;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::SetProjectMeasurementsCommand>(
                               std::vector<doc::ProjectMeasurement>{length("shelf_depth", 243, "Ruler")}, "Add measurement"),
                           d)
                    .ok());
    EXPECT_EQ(stack.undoLabel(), "Add measurement");
    auto renamed = d.measurements();
    renamed[0].name = "depth";
    ASSERT_TRUE(stack.push(std::make_unique<cmd::SetProjectMeasurementsCommand>(renamed, "Rename measurement"), d).ok());
    auto changed = d.measurements();
    changed[0].value = 250;
    ASSERT_TRUE(stack.push(std::make_unique<cmd::SetProjectMeasurementsCommand>(changed, "Change measurement"), d).ok());
    ASSERT_TRUE(stack.push(std::make_unique<cmd::SetProjectMeasurementsCommand>(std::vector<doc::ProjectMeasurement>{}, "Delete measurement"), d)
                    .ok());
    EXPECT_TRUE(d.measurements().empty());
    // A list the file could not read back is refused and changes nothing.
    const std::size_t steps = stack.index();
    EXPECT_FALSE(stack.push(std::make_unique<cmd::SetProjectMeasurementsCommand>(
                                std::vector<doc::ProjectMeasurement>{length("a", 1), length("A", 2)}, "Add measurement"),
                            d)
                     .ok());
    EXPECT_EQ(stack.index(), steps);
    // Undo walks back through each state, redo forward.
    ASSERT_TRUE(stack.undo(d));
    ASSERT_EQ(d.measurements().size(), 1u);
    EXPECT_DOUBLE_EQ(d.measurements()[0].value, 250.0);
    ASSERT_TRUE(stack.undo(d));
    EXPECT_DOUBLE_EQ(d.measurements()[0].value, 243.0);
    EXPECT_EQ(d.measurements()[0].name, "depth");
    ASSERT_TRUE(stack.undo(d));
    EXPECT_EQ(d.measurements()[0].name, "shelf_depth");
    EXPECT_EQ(d.measurements()[0].note, "Ruler");
    ASSERT_TRUE(stack.undo(d));
    EXPECT_TRUE(d.measurements().empty());
    for (int i = 0; i < 3; ++i)
        ASSERT_TRUE(stack.redo(d).ok());
    ASSERT_EQ(d.measurements().size(), 1u);
    EXPECT_EQ(d.measurements()[0].name, "depth");
    EXPECT_DOUBLE_EQ(d.measurements()[0].value, 250.0);
}

TEST(ProjectMeasurements, SavedInTheProjectAndReadBack)
{
    auto d = boxDocument();
    d->setMeasurements({length("shelf_depth", 243.25, "Photo"), angle("corner_angle", 37.5), length("wall", 3)});
    const auto json = io::documentToJson(*d);
    ASSERT_TRUE(json.contains("measurements"));
    ASSERT_EQ(json["measurements"].size(), 3u);
    EXPECT_EQ(json["measurements"][0]["name"], "shelf_depth");
    EXPECT_EQ(json["measurements"][0]["quantity"], "length");
    EXPECT_DOUBLE_EQ(json["measurements"][0]["value"].get<double>(), 243.25);
    EXPECT_EQ(json["measurements"][0]["note"], "Photo");
    EXPECT_FALSE(json["measurements"][2].contains("note")) << "no empty note written";
    EXPECT_EQ(json["measurements"][1]["quantity"], "angle");
    EXPECT_NEAR(json["measurements"][1]["value"].get<double>(), 37.5 * 3.14159265358979323846 / 180.0, 1e-15) << "radians, as every angle in the file";
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.userMessage();
    EXPECT_EQ(again.value()->measurements(), d->measurements()) << "37.5 degrees comes back exactly";
    EXPECT_EQ(io::documentToJson(*again.value()), json);

    // Through a file on disk too.
    const auto path = std::filesystem::temp_directory_path() / "openshape_test_measurements.openshape";
    ASSERT_TRUE(io::saveProject(*d, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok());
    EXPECT_EQ(loaded.value()->measurements(), d->measurements());
    std::filesystem::remove(path);
}

TEST(ProjectMeasurements, OldFilesLoadAndFilesWithoutThemAreWrittenAsBefore)
{
    auto d = boxDocument();
    const auto json = io::documentToJson(*d);
    EXPECT_FALSE(json.contains("measurements")) << "no key without measurements: the file stays as 0.3 wrote it";
    EXPECT_EQ(json["version"], 1);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok());
    EXPECT_TRUE(again.value()->measurements().empty());
}

TEST(ProjectMeasurements, BadEntriesAreLeftOutNotFatal)
{
    auto json = io::documentToJson(*boxDocument());
    json["measurements"] = nlohmann::json::array({
        {{"name", "good"}, {"quantity", "length"}, {"value", 12.0}},
        {{"name", "Good"}, {"quantity", "length"}, {"value", 13.0}},  // taken (case)
        {{"name", "mm"}, {"quantity", "length"}, {"value", 1.0}},     // a unit
        {{"name", "x y"}, {"quantity", "length"}, {"value", 1.0}},    // not a name
        {{"name", "big"}, {"quantity", "length"}, {"value", 1e30}},   // too large
        {{"name", "what"}, {"quantity", "volume"}, {"value", 1.0}},   // unknown quantity
        {{"name", "text"}, {"quantity", "length"}, {"value", "12"}},  // not a number
        42,
        {{"name", "turn"}, {"quantity", "angle"}, {"value", 3.14159265358979323846 / 2}, {"note", "Fold"}},
    });
    auto loaded = io::documentFromJson(json);
    ASSERT_TRUE(loaded.ok()) << loaded.userMessage();
    const auto& kept = loaded.value()->measurements();
    ASSERT_EQ(kept.size(), 2u);
    EXPECT_EQ(kept[0], length("good", 12.0));
    EXPECT_EQ(kept[1], (doc::ProjectMeasurement{"turn", ValueKind::Angle, 90.0, "Fold"}));
    // Not a list at all: a damaged file.
    json["measurements"] = "shelf";
    EXPECT_FALSE(io::documentFromJson(json).ok());
}
