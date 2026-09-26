// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Imported bodies in documents and project files: the Imported step keeps
// the exact geometry, which projects store under imports/ (the source of
// truth, also in recovery copies) and check when they are opened.

#include "TestHelpers.h"

#include "core/Uuid.h"
#include "geometry/Exchange.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>

using namespace os;
using namespace os::test;

namespace {

// Unique per run: several builds may run these tests at the same time.
std::filesystem::path temp(const std::string& name)
{
    return std::filesystem::temp_directory_path() / ("openshape_import_" + Uuid::generate().toString() + "_" + name);
}

// A 60 x 40 x 20 block with its vertical edges rounded (R 3), written to
// STEP and read back: geometry as an import delivers it.
geom::Shape importedPlate()
{
    auto body = std::make_unique<doc::Body>();
    body->insertFeature(boxFeature(60, 40, 20), 0);
    doc::Document d;
    const Uuid id = body->id();
    d.addBody(std::move(body));
    d.insertFeature(id, filletVertical(d.body(id)->shape(), 3.0));
    const auto path = temp("plate.step");
    EXPECT_TRUE(geom::exportStep({{"Plate", d.body(id)->shape()}}, path).ok());
    auto imported = geom::importStep(path);
    std::filesystem::remove(path);
    EXPECT_TRUE(imported.ok()) << imported.developerMessage();
    EXPECT_EQ(imported.value().size(), 1u);
    return imported.value().front().shape;
}

const double kPlateVolume = 60.0 * 40.0 * 20.0 - 4 * (9.0 - kPi * 9.0 / 4.0) * 20.0;
const double kPlateTopArea = 60.0 * 40.0 - 4 * (9.0 - kPi * 9.0 / 4.0);

std::unique_ptr<cmd::CreateBodyCommand> importCommand(const std::string& name, const geom::Shape& shape)
{
    auto feature = std::make_unique<doc::ImportedFeature>();
    feature->setShape(shape);
    feature->source = "plate.step";
    return std::make_unique<cmd::CreateBodyCommand>(name, std::move(feature));
}

// The imported plate with its top pushed up 5 mm.
std::unique_ptr<doc::Document> importedDocument(Uuid* bodyIdOut)
{
    auto d = std::make_unique<doc::Document>();
    auto command = importCommand("Plate", importedPlate());
    EXPECT_TRUE(command->execute(*d).ok());
    const Uuid bodyId = command->bodyId();
    d->insertFeature(bodyId, pushPull(d->body(bodyId)->shape(), {0, 0, 1}, 5.0));
    EXPECT_FALSE(d->body(bodyId)->hasFailures());
    *bodyIdOut = bodyId;
    return d;
}

using Entries = std::vector<std::pair<std::string, std::string>>;

Entries readZip(const std::filesystem::path& p)
{
    Entries out;
    int err = 0;
    zip_t* z = zip_open(p.string().c_str(), ZIP_RDONLY, &err);
    EXPECT_NE(z, nullptr);
    if (!z)
        return out;
    const zip_int64_t n = zip_get_num_entries(z, 0);
    for (zip_int64_t i = 0; i < n; ++i) {
        zip_stat_t st;
        zip_stat_index(z, zip_uint64_t(i), 0, &st);
        std::string data(std::size_t(st.size), '\0');
        zip_file_t* f = zip_fopen_index(z, zip_uint64_t(i), 0);
        zip_fread(f, data.data(), st.size);
        zip_fclose(f);
        out.emplace_back(st.name, data);
    }
    zip_close(z);
    return out;
}

void writeZip(const std::filesystem::path& p, const Entries& entries)
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

// The BRep text with its first surface moved by (7, 7, 7): the location is
// the three numbers after the surface's type.
std::string shiftFirstSurface(const std::string& brep)
{
    const auto section = brep.find("Surfaces ");
    EXPECT_NE(section, std::string::npos);
    const auto start = brep.find('\n', section) + 1;
    const auto end = brep.find('\n', start);
    std::istringstream line(brep.substr(start, end - start));
    std::vector<std::string> tokens;
    for (std::string t; line >> t;)
        tokens.push_back(t);
    EXPECT_GE(tokens.size(), 4u);
    for (std::size_t k = 1; k <= 3 && k < tokens.size(); ++k)
        tokens[k] = std::to_string(std::stod(tokens[k]) + 7.0);
    std::string moved;
    for (const auto& t : tokens)
        moved += t + " ";
    return brep.substr(0, start) + moved + brep.substr(end);
}

const std::string& entryData(const Entries& entries, const std::string& name)
{
    static const std::string none;
    for (const auto& [n, data] : entries)
        if (n == name)
            return data;
    return none;
}

std::string importEntryOf(const Entries& entries)
{
    for (const auto& [name, data] : entries)
        if (name.rfind("imports/", 0) == 0)
            return name;
    return {};
}

} // namespace

TEST(ImportedBody, StepGeometryIsExact)
{
    const geom::Shape plate = importedPlate();
    EXPECT_NEAR(geom::volume(plate), kPlateVolume, 1e-6 * kPlateVolume);
    const auto box = geom::boundingBox(plate);
    EXPECT_NEAR(box.size().x, 60.0, 1e-5);
    EXPECT_NEAR(box.size().y, 40.0, 1e-5);
    EXPECT_NEAR(box.size().z, 20.0, 1e-5);
}

TEST(ImportedBody, SaveAndOpenKeepTheGeometryAndItRecomputes)
{
    Uuid bodyId;
    auto original = importedDocument(&bodyId);
    const auto& features = original->body(bodyId)->features();
    ASSERT_EQ(features.size(), 2u);
    ASSERT_EQ(features[0]->kind(), doc::FeatureKind::Imported);
    const Uuid importId = features[0]->id();
    const Uuid pushId = features[1]->id();
    const double pushed = geom::volume(original->body(bodyId)->shape());
    EXPECT_NEAR(pushed, kPlateVolume + kPlateTopArea * 5.0, 1e-3);

    const auto path = temp("imported.openshape");
    ASSERT_TRUE(io::saveProject(*original, path).ok());
    const Entries entries = readZip(path);
    EXPECT_EQ(importEntryOf(entries), "imports/" + importId.toString() + ".brep");
    const auto json = nlohmann::json::parse(entryData(entries, "document.json"));
    const auto& params = json["bodies"][0]["features"][0]["params"];
    EXPECT_EQ(json["bodies"][0]["features"][0]["type"], "Imported");
    EXPECT_EQ(params["geometry"], "imports/" + importId.toString() + ".brep");
    EXPECT_EQ(params["source"], "plate.step");
    EXPECT_NEAR(params["volume"].get<double>(), kPlateVolume, 1e-6 * kPlateVolume);

    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    const doc::Body& body = *d.body(bodyId);
    EXPECT_FALSE(body.hasFailures());
    const auto* imported = dynamic_cast<const doc::ImportedFeature*>(body.features()[0].get());
    ASSERT_NE(imported, nullptr);
    EXPECT_EQ(imported->source, "plate.step");
    EXPECT_NEAR(geom::volume(imported->shape), kPlateVolume, 1e-9 * kPlateVolume) << "stored exactly";
    EXPECT_NEAR(geom::volume(body.shape()), pushed, 1e-9 * pushed);
    EXPECT_EQ(io::documentToJson(d), io::documentToJson(*original));

    // A history edit recomputes on top of the stored geometry.
    ASSERT_TRUE(d.body(bodyId)->feature(pushId)->setParameter("distance", 10.0).ok());
    d.featureChanged(pushId);
    EXPECT_FALSE(d.body(bodyId)->hasFailures());
    EXPECT_NEAR(geom::volume(d.body(bodyId)->shape()), kPlateVolume + kPlateTopArea * 10.0, 1e-3);
    EXPECT_NEAR(geom::boundingBox(d.body(bodyId)->shape()).size().z, 30.0, 1e-5);
    std::filesystem::remove(path);
}

TEST(ImportedBody, RecoveryCopiesWithoutTheCacheStillHoldIt)
{
    Uuid bodyId;
    auto original = importedDocument(&bodyId);
    const auto path = temp("copy.openshape");
    io::SaveOptions options;
    options.includeGeometryCache = false; // as recovery copies are written
    ASSERT_TRUE(io::saveProject(*original, path, options).ok());
    const Entries entries = readZip(path);
    EXPECT_FALSE(importEntryOf(entries).empty());
    for (const auto& [name, data] : entries)
        EXPECT_NE(name.rfind("geometry/", 0), 0u) << "no cache in a copy: " << name;
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    EXPECT_NEAR(geom::volume(loaded.value()->body(bodyId)->shape()), geom::volume(original->body(bodyId)->shape()), 1e-6);
    std::filesystem::remove(path);
}

TEST(ImportedBody, BrokenImportEntriesFailCleanly)
{
    Uuid bodyId;
    auto original = importedDocument(&bodyId);
    const auto path = temp("good.openshape");
    ASSERT_TRUE(io::saveProject(*original, path).ok());
    const Entries good = readZip(path);
    const std::string entry = importEntryOf(good);
    ASSERT_FALSE(entry.empty());
    const std::string brep = entryData(good, entry);

    auto expectDamaged = [&](const Entries& entries, const std::string& what) {
        const auto broken = temp("broken.openshape");
        writeZip(broken, entries);
        auto r = io::loadProject(broken);
        EXPECT_FALSE(r.ok()) << what;
        EXPECT_EQ(r.error(), ErrorCode::FileFormatError) << what;
        // "This project file is damaged ...", or "... an invalid imported body." for bad params.
        EXPECT_TRUE(r.userMessage() == "This project file is damaged or not an OpenShape project."
                    || r.userMessage() == "The file contains an invalid imported body.")
            << what << ": " << r.userMessage();
        std::filesystem::remove(broken);
    };
    auto withEntry = [&](const std::string& replacement) {
        Entries e = good;
        for (auto& [name, data] : e)
            if (name == entry)
                data = replacement;
        return e;
    };
    auto withoutEntry = [&] {
        Entries e;
        for (const auto& item : good)
            if (item.first != entry)
                e.push_back(item);
        return e;
    };
    auto withParams = [&](const std::function<void(nlohmann::json&)>& change) {
        Entries e = good;
        for (auto& [name, data] : e)
            if (name == "document.json") {
                auto json = nlohmann::json::parse(data);
                change(json["bodies"][0]["features"][0]["params"]);
                data = json.dump();
            }
        return e;
    };

    expectDamaged(withoutEntry(), "entry missing");
    expectDamaged(withEntry(""), "empty entry");
    expectDamaged(withEntry("this is not a BRep file"), "garbage");
    for (double cut : {0.1, 0.5, 0.9, 0.99})
        expectDamaged(withEntry(brep.substr(0, std::size_t(double(brep.size()) * cut))), "truncated");
    // A surface moved: it still parses, but it is not the same solid.
    expectDamaged(withEntry(shiftFirstSurface(brep)), "moved surface");
    expectDamaged(withEntry(geom::toBrepString(geom::makeBox({0, 0, 0}, {10, 10, 10}).value(), false)), "another solid");
    // Byte flips all through the entry (a deterministic "fuzz").
    for (std::size_t at = 0; at < brep.size(); at += brep.size() / 23 + 1) {
        std::string flipped = brep;
        flipped[at] = static_cast<char>(flipped[at] ^ 0x5A);
        expectDamaged(withEntry(flipped), "flipped byte at " + std::to_string(at));
    }
    // Damage that also fixed the hash up gets as far as the kernel: the
    // parse, validity and volume checks still refuse it.
    auto withEntryAndHash = [&](const std::string& replacement) {
        Entries e = withEntry(replacement);
        for (auto& [name, data] : e)
            if (name == "document.json") {
                auto json = nlohmann::json::parse(data);
                json["bodies"][0]["features"][0]["params"]["hash"] = doc::ImportedFeature::hashOf(replacement);
                data = json.dump();
            }
        return e;
    };
    expectDamaged(withEntryAndHash("this is not a BRep file"), "garbage, hash matching");
    expectDamaged(withEntryAndHash(brep.substr(0, brep.size() / 2)), "truncated, hash matching");
    expectDamaged(withEntryAndHash(shiftFirstSurface(brep)), "moved surface, hash matching");
    expectDamaged(withEntryAndHash(geom::toBrepString(geom::makeBox({0, 0, 0}, {10, 10, 10}).value(), false)),
                  "another solid, hash matching");
    expectDamaged(withParams([](nlohmann::json& p) { p["hash"] = "0123456789abcdef"; }), "wrong hash");
    expectDamaged(withParams([](nlohmann::json& p) { p["hash"] = "XYZ"; }), "bad hash");
    expectDamaged(withParams([](nlohmann::json& p) { p["volume"] = -1.0; }), "negative volume");
    expectDamaged(withParams([](nlohmann::json& p) { p["volume"] = 1234.5; }), "volume mismatch");
    expectDamaged(withParams([](nlohmann::json& p) { p["geometry"] = "geometry/other.brep"; }), "outside imports/");
    expectDamaged(withParams([](nlohmann::json& p) { p["geometry"] = "imports/missing.brep"; }), "names no entry");
    expectDamaged(withParams([](nlohmann::json& p) { p.erase("geometry"); }), "no geometry");
    expectDamaged(withParams([](nlohmann::json& p) { p["source"] = 5; }), "bad source");

    // Without the archive there is nothing to read the geometry from.
    EXPECT_FALSE(io::documentFromJson(io::documentToJson(*original)).ok());
    std::filesystem::remove(path);
}

TEST(ImportedBody, UndoRedoDuplicateAndSave)
{
    doc::Document d;
    cmd::UndoStack stack;
    const geom::Shape plate = importedPlate();
    auto moved = geom::translated(plate, {100, 0, 0});
    ASSERT_TRUE(moved.ok());
    std::vector<std::unique_ptr<cmd::Command>> steps;
    steps.push_back(importCommand("Plate", plate));
    steps.push_back(importCommand("Plate 2", moved.value()));
    ASSERT_TRUE(stack.push(std::make_unique<cmd::CompositeCommand>("Import 2 bodies", std::move(steps)), d).ok());
    ASSERT_EQ(d.bodies().size(), 2u);
    EXPECT_EQ(stack.undoLabel(), "Import 2 bodies");
    ASSERT_TRUE(stack.undo(d));
    EXPECT_TRUE(d.bodies().empty());
    ASSERT_TRUE(stack.redo(d).ok());
    ASSERT_EQ(d.bodies().size(), 2u);
    EXPECT_NEAR(geom::boundingBox(d.bodies()[1]->shape()).min.x, 100.0, 1e-6);

    // A duplicate has its own Imported step (own id, own entry in the file).
    auto duplicate = std::make_unique<cmd::DuplicateBodyCommand>(d.bodies()[0]->id());
    const Uuid copyId = duplicate->copyId();
    ASSERT_TRUE(stack.push(std::move(duplicate), d).ok());
    const doc::Body& copy = *d.body(copyId);
    ASSERT_EQ(copy.features()[0]->kind(), doc::FeatureKind::Imported);
    EXPECT_NE(copy.features()[0]->id(), d.bodies()[0]->features()[0]->id());

    const auto path = temp("three.openshape");
    ASSERT_TRUE(io::saveProject(d, path).ok());
    int imports = 0;
    for (const auto& [name, data] : readZip(path))
        imports += name.rfind("imports/", 0) == 0 ? 1 : 0;
    EXPECT_EQ(imports, 3);
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    ASSERT_EQ(loaded.value()->bodies().size(), 3u);
    for (const auto& body : loaded.value()->bodies())
        EXPECT_NEAR(geom::volume(body->shape()), kPlateVolume, 1e-6 * kPlateVolume) << body->name();
    std::filesystem::remove(path);
}

TEST(ImportedBody, ImportedGeometryIsReadOnceAndWithinItsBudget)
{
    doc::Document d;
    cmd::UndoStack stack;
    const geom::Shape plate = importedPlate();
    auto moved = geom::translated(plate, {100, 0, 0});
    ASSERT_TRUE(moved.ok());
    std::vector<std::unique_ptr<cmd::Command>> steps;
    steps.push_back(importCommand("Plate", plate));
    steps.push_back(importCommand("Plate 2", moved.value()));
    ASSERT_TRUE(stack.push(std::make_unique<cmd::CompositeCommand>("Import 2 bodies", std::move(steps)), d).ok());
    const auto path = temp("two.openshape");
    ASSERT_TRUE(io::saveProject(d, path).ok());
    const Entries good = readZip(path);
    std::uint64_t total = 0;
    std::vector<std::string> names;
    for (const auto& [name, data] : good)
        if (name.rfind("imports/", 0) == 0) {
            total += data.size();
            names.push_back(name);
        }
    ASSERT_EQ(names.size(), 2u);

    // The loader reads no more imported geometry than its budget.
    io::LoadOptions budget;
    budget.maxImportedGeometryBytes = total;
    auto loaded = io::loadProject(path, budget);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    EXPECT_EQ(loaded.value()->bodies().size(), 2u);
    budget.maxImportedGeometryBytes = total - 1;
    auto over = io::loadProject(path, budget);
    ASSERT_FALSE(over.ok());
    EXPECT_EQ(over.error(), ErrorCode::FileFormatError);
    EXPECT_EQ(over.userMessage(), "This project file is damaged or not an OpenShape project.");

    // Saving refuses what the loader would not read (the file is not touched).
    const auto other = temp("too_large.openshape");
    io::SaveOptions small;
    small.maxImportedGeometryBytes = total - 1;
    const Status refused = io::saveProject(d, other, small);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::FileWriteError);
    EXPECT_NE(refused.userMessage().find("too large to keep in one project"), std::string::npos) << refused.userMessage();
    EXPECT_FALSE(std::filesystem::exists(other));
    small.maxImportedGeometryBytes = total;
    EXPECT_TRUE(io::saveProject(d, other, small).ok());

    // A crafted file whose steps all name one entry (each would read it and
    // keep a copy: many steps and a large entry exhaust memory) is refused.
    Entries crafted;
    for (const auto& [name, data] : good) {
        if (name == names[1])
            continue;
        if (name != "document.json") {
            crafted.emplace_back(name, data);
            continue;
        }
        auto json = nlohmann::json::parse(data);
        const nlohmann::json first = json["bodies"][0]["features"][0]["params"];
        for (int copy = 0; copy < 50; ++copy) {
            nlohmann::json body = json["bodies"][0];
            body["id"] = Uuid::generate().toString();
            body["features"][0]["id"] = Uuid::generate().toString();
            json["bodies"].push_back(body);
        }
        json["bodies"][1]["features"][0]["params"] = first;
        crafted.emplace_back(name, json.dump());
    }
    const auto sharing = temp("sharing.openshape");
    writeZip(sharing, crafted);
    auto shared = io::loadProject(sharing);
    ASSERT_FALSE(shared.ok());
    EXPECT_EQ(shared.userMessage(), "This project file is damaged or not an OpenShape project.");
    EXPECT_NE(shared.developerMessage().find("more than one step"), std::string::npos) << shared.developerMessage();
    for (const auto& p : {path, other, sharing})
        std::filesystem::remove(p);
}
