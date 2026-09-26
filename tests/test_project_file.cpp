// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "TestHelpers.h"

#include "core/Version.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <filesystem>
#include <fstream>

using namespace os;
using namespace os::test;

namespace {

std::filesystem::path tempPath(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_test_" + name);
}

void writeFile(const std::filesystem::path& p, const std::string& content)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << content;
}

// Builds a zip with arbitrary entries (for malformed-file tests).
void writeZip(const std::filesystem::path& p, const std::vector<std::pair<std::string, std::string>>& entries)
{
    std::filesystem::remove(p);
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    ASSERT_NE(z, nullptr);
    for (const auto& [name, data] : entries) {
        zip_source_t* s = zip_source_buffer(z, data.data(), data.size(), 0);
        ASSERT_GE(zip_file_add(z, name.c_str(), s, ZIP_FL_OVERWRITE), 0);
    }
    ASSERT_EQ(zip_close(z), 0);
}

// Box 60x40x20, fillet the 4 vertical edges at 3 mm (the MVP model).
std::unique_ptr<doc::Document> mvpDocument(Uuid* bodyIdOut = nullptr)
{
    auto d = std::make_unique<doc::Document>();
    auto body = std::make_unique<doc::Body>();
    body->setName("Plate");
    const Uuid bodyId = body->id();
    body->insertFeature(boxFeature(60, 40, 20), 0);
    d->addBody(std::move(body));
    d->insertFeature(bodyId, filletVertical(d->body(bodyId)->shape(), 3.0));
    if (bodyIdOut)
        *bodyIdOut = bodyId;
    return d;
}

} // namespace

TEST(ProjectFile, SaveAndReopenPreservesModelAndIdentity)
{
    Uuid bodyId;
    auto original = mvpDocument(&bodyId);
    original->setDisplayUnit(LengthUnit::Inch);
    const double volume = geom::volume(original->body(bodyId)->shape());
    const Uuid filletId = original->body(bodyId)->features()[1]->id();

    const auto path = tempPath("mvp.openshape");
    ASSERT_TRUE(io::saveProject(*original, path).ok());

    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    EXPECT_EQ(d.id(), original->id());
    EXPECT_EQ(d.displayUnit(), LengthUnit::Inch);
    ASSERT_NE(d.body(bodyId), nullptr);
    EXPECT_EQ(d.body(bodyId)->name(), "Plate");
    EXPECT_EQ(d.body(bodyId)->features()[1]->id(), filletId);
    EXPECT_FALSE(d.body(bodyId)->hasFailures());
    EXPECT_NEAR(geom::volume(d.body(bodyId)->shape()), volume, 1e-6);

    // Model remains editable after reopening: change box height, fillet follows.
    const Uuid boxId = d.body(bodyId)->features()[0]->id();
    ASSERT_TRUE(d.body(bodyId)->feature(boxId)->setParameter("height", 30.0).ok());
    d.featureChanged(boxId);
    EXPECT_FALSE(d.body(bodyId)->hasFailures());
    EXPECT_NEAR(geom::boundingBox(d.body(bodyId)->shape()).size().z, 30.0, 1e-6);
}

TEST(ProjectFile, JsonRoundTripIsStable)
{
    auto d = mvpDocument();
    const auto first = io::documentToJson(*d);
    auto again = io::documentFromJson(first);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    EXPECT_EQ(io::documentToJson(*again.value()), first);
    EXPECT_EQ(first["format"], "OpenShape");
    EXPECT_EQ(first["version"], io::kProjectFormatVersion);
    EXPECT_EQ(first["lengthUnit"], "mm");
}

// A Move with a rotation (Rotate / Align steps) survives save and reopen.
TEST(ProjectFile, RotatedMoveRoundTrips)
{
    Uuid bodyId;
    auto d = mvpDocument(&bodyId);
    auto move = std::make_unique<doc::MoveFeature>();
    move->setName("Align");
    move->translation = {5, -3, 12};
    move->rotates = true;
    move->rotationCenter = {30, 20, 10};
    move->rotationAxis = {1, 1, 0};
    move->rotationAngle = 0.7;
    d->insertFeature(bodyId, std::move(move));
    ASSERT_FALSE(d->body(bodyId)->hasFailures());
    const auto before = geom::boundingBox(d->body(bodyId)->shape());

    const auto json = io::documentToJson(*d);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    const doc::Body& body = *again.value()->body(bodyId);
    EXPECT_EQ(body.features().back()->name(), "Align");
    const auto after = geom::boundingBox(body.shape());
    EXPECT_NEAR((after.min - before.min).length(), 0.0, 1e-6);
    EXPECT_NEAR((after.max - before.max).length(), 0.0, 1e-6);
    EXPECT_NEAR(*body.features().back()->parameter("angle"), 0.7, 1e-12);

    // A rotation without an axis is rejected, not silently dropped.
    auto broken = json;
    for (auto& b : broken["bodies"])
        for (auto& f : b["features"])
            if (f["type"] == "Move")
                f["params"]["rotation"].erase("axis");
    EXPECT_FALSE(io::documentFromJson(broken).ok());
}

TEST(ProjectFile, RejectsNewerVersion)
{
    auto j = io::documentToJson(*mvpDocument());
    j["version"] = io::kProjectFormatVersion + 1;
    auto r = io::documentFromJson(j);
    ASSERT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::FileVersionUnsupported);
}

TEST(ProjectFile, RejectsMalformedDocuments)
{
    const auto good = io::documentToJson(*mvpDocument());
    auto expectFormatError = [](const nlohmann::json& j) {
        auto r = io::documentFromJson(j);
        EXPECT_FALSE(r.ok());
        EXPECT_FALSE(r.userMessage().empty());
    };
    expectFormatError(nlohmann::json::array());
    auto j = good; j.erase("format"); expectFormatError(j);
    j = good; j["format"] = "SomethingElse"; expectFormatError(j);
    j = good; j["version"] = "one"; expectFormatError(j);
    j = good; j["lengthUnit"] = "in"; expectFormatError(j);
    j = good; j["bodies"] = 5; expectFormatError(j);
    j = good; j["bodies"][0]["id"] = "not-a-uuid"; expectFormatError(j);
    j = good; j["bodies"][0]["features"][0]["type"] = "Teleport"; expectFormatError(j);
    j = good; j["bodies"][0]["features"][0]["params"]["size"] = nlohmann::json::array({1, 2}); expectFormatError(j);
    j = good; j["bodies"][0]["features"][0]["params"]["size"] = nlohmann::json::array({1, -2, 3}); expectFormatError(j);
    j = good; j["bodies"][0]["features"][1]["params"]["edges"][0]["indexHint"] = "x"; expectFormatError(j);
    j = good; j["bodies"][0]["features"][1]["id"] = j["bodies"][0]["features"][0]["id"]; expectFormatError(j);
    j = good; std::swap(j["bodies"][0]["features"][0], j["bodies"][0]["features"][1]); expectFormatError(j);
}

TEST(ProjectFile, RejectsNonZipAndMissingEntries)
{
    const auto garbage = tempPath("garbage.openshape");
    writeFile(garbage, "definitely not a zip file");
    auto r = io::loadProject(garbage);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::FileFormatError);

    EXPECT_EQ(io::loadProject(tempPath("missing.openshape")).error(), ErrorCode::FileNotFound);

    const auto noDoc = tempPath("nodoc.openshape");
    writeZip(noDoc, {{"metadata.json", "{}"}});
    EXPECT_EQ(io::loadProject(noDoc).error(), ErrorCode::FileFormatError);

    const auto badJson = tempPath("badjson.openshape");
    writeZip(badJson, {{"document.json", "{ this is not json"}});
    EXPECT_EQ(io::loadProject(badJson).error(), ErrorCode::FileFormatError);
}

TEST(ProjectFile, RejectsPathTraversalEntries)
{
    const auto evil = tempPath("evil.openshape");
    const std::string docJson = io::documentToJson(*mvpDocument()).dump();
    writeZip(evil, {{"document.json", docJson}, {"../../evil.txt", "x"}});
    auto r = io::loadProject(evil);
    EXPECT_FALSE(r.ok());
    EXPECT_EQ(r.error(), ErrorCode::FileFormatError);
}

TEST(ProjectFile, SafeEntryNames)
{
    EXPECT_TRUE(io::isSafeArchiveEntryName("document.json"));
    EXPECT_TRUE(io::isSafeArchiveEntryName("geometry/abc.brep"));
    EXPECT_FALSE(io::isSafeArchiveEntryName(""));
    EXPECT_FALSE(io::isSafeArchiveEntryName("/etc/passwd"));
    EXPECT_FALSE(io::isSafeArchiveEntryName("../x"));
    EXPECT_FALSE(io::isSafeArchiveEntryName("a/../../x"));
    EXPECT_FALSE(io::isSafeArchiveEntryName("C:/Windows/x"));
    EXPECT_FALSE(io::isSafeArchiveEntryName("a\\b"));
    EXPECT_FALSE(io::isSafeArchiveEntryName("./a"));
}

// metadata.json names the application version that wrote the file (CMake's
// PROJECT_VERSION via core/Version.h, not a hard-coded string).
TEST(ProjectFile, MetadataRecordsApplicationVersion)
{
    const auto path = tempPath("metadata.openshape");
    ASSERT_TRUE(io::saveProject(*mvpDocument(), path).ok());

    int err = 0;
    zip_t* z = zip_open(path.string().c_str(), ZIP_RDONLY, &err);
    ASSERT_NE(z, nullptr);
    zip_stat_t st;
    ASSERT_EQ(zip_stat(z, "metadata.json", 0, &st), 0);
    std::string text(static_cast<size_t>(st.size), '\0');
    zip_file_t* f = zip_fopen(z, "metadata.json", 0);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(zip_fread(f, text.data(), st.size), static_cast<zip_int64_t>(st.size));
    zip_fclose(f);
    zip_close(z);

    const auto metadata = nlohmann::json::parse(text);
    EXPECT_EQ(metadata["application"], "OpenShape");
    EXPECT_EQ(metadata["applicationVersion"], kAppVersion);
    EXPECT_EQ(metadata["version"], io::kProjectFormatVersion);
}

TEST(ProjectFile, SaveOverwritesExisting)
{
    const auto path = tempPath("overwrite.openshape");
    ASSERT_TRUE(io::saveProject(*mvpDocument(), path).ok());
    doc::Document empty;
    ASSERT_TRUE(io::saveProject(empty, path).ok());
    auto r = io::loadProject(path);
    ASSERT_TRUE(r.ok());
    EXPECT_TRUE(r.value()->bodies().empty());
    EXPECT_FALSE(std::filesystem::exists(path.string() + ".tmp"));
}
