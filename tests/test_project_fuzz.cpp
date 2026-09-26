// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Project files are untrusted input. Valid projects are corrupted in many
// seeded ways (truncated, bytes flipped, bad JSON, wrong types, huge,
// negative and non-finite numbers, missing or duplicated references, unsafe
// or oversized entries); loading must either fail with a plain message or
// produce a usable document. Never crash, throw or hang.
#include "StressHarness.h"
#include "TestHelpers.h"

#include "io/ProjectFile.h"

#include <zip.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>

using namespace os;
using namespace os::test;
using nlohmann::json;

namespace {

std::filesystem::path tempFile(const std::string& stem)
{
    static std::mt19937_64 rng(std::random_device{}());
    return std::filesystem::temp_directory_path() / ("openshape_fuzz_" + stem + "_" + std::to_string(rng()) + ".openshape");
}

std::string readBytes(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

void writeBytes(const std::filesystem::path& p, const std::string& bytes)
{
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), std::streamsize(bytes.size()));
}

// A zip with arbitrary entries, in memory.
std::string zipOf(const std::vector<std::pair<std::string, std::string>>& entries)
{
    const auto path = tempFile("zip");
    int err = 0;
    zip_t* z = zip_open(path.string().c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    EXPECT_NE(z, nullptr);
    if (!z)
        return {};
    for (const auto& [name, data] : entries) {
        zip_source_t* s = zip_source_buffer(z, data.data(), data.size(), 0);
        EXPECT_GE(zip_file_add(z, name.c_str(), s, 0), 0) << name;
    }
    EXPECT_EQ(zip_close(z), 0);
    std::string bytes = readBytes(path);
    std::filesystem::remove(path);
    return bytes;
}

// Plain-language check: a message a user can read, without internals.
::testing::AssertionResult plainMessage(const std::string& text)
{
    if (text.empty())
        return ::testing::AssertionFailure() << "empty message";
    for (const char* jargon : {"zip_", "json", "nlohmann", "Standard_", "BRep", "OCC", "exception", "null", "0x"})
        if (text.find(jargon) != std::string::npos)
            return ::testing::AssertionFailure() << "technical message: " << text;
    return ::testing::AssertionSuccess();
}

// Loads `bytes` as a project. It must fail with a plain message or give a
// document that recomputes and saves.
struct Outcome {
    bool loaded = false;
    std::string message;
};

Outcome loadChecked(const std::string& bytes, const std::string& what)
{
    Outcome outcome;
    const auto path = tempFile("case");
    writeBytes(path, bytes);
    const auto start = std::chrono::steady_clock::now();
    try {
        auto result = io::loadProject(path);
        if (result) {
            outcome.loaded = true;
            doc::Document& d = *result.value();
            d.recomputeAll();
            for (const auto& body : d.bodies()) {
                EXPECT_FALSE(body->features().empty()) << what;
                EXPECT_TRUE(body->features().front()->isBaseFeature()) << what;
                for (std::size_t i = 0; i < body->features().size(); ++i) {
                    const auto& state = body->state(int(i));
                    if (state.status == doc::FeatureStatus::Failed) {
                        EXPECT_TRUE(plainMessage(state.userMessage)) << what;
                    }
                }
            }
            // It can be written again and read back.
            const json again = io::documentToJson(d);
            auto reread = io::documentFromJson(again);
            EXPECT_TRUE(reread.ok()) << what << ": " << reread.developerMessage();
        } else {
            outcome.message = result.userMessage();
            EXPECT_TRUE(plainMessage(result.userMessage())) << what << " (" << result.developerMessage() << ")";
        }
    } catch (const std::exception& e) {
        ADD_FAILURE() << what << ": exception escaped loadProject: " << e.what();
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    EXPECT_LT(seconds, 10.0) << what << " took " << seconds << " s";
    std::filesystem::remove(path);
    return outcome;
}

// ---- Valid projects ------------------------------------------------------------

// A project using every reference kind: sketches on a face (attached),
// extrusions (new body, join, cut through all), a combine with a hidden
// tool body, fillets, a pattern, a mirror, a rotated move, a hole.
std::unique_ptr<doc::Document> richDocument()
{
    auto d = std::make_unique<doc::Document>();
    cmd::UndoStack stack;
    auto push = [&](std::unique_ptr<cmd::Command> c) {
        const Status s = stack.push(std::move(c), *d);
        EXPECT_TRUE(s.ok()) << s.developerMessage();
    };
    auto box = std::make_unique<doc::BoxFeature>();
    box->size = {40, 30, 20};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(box));
    const Uuid plate = create->bodyId();
    push(std::move(create));
    const doc::Body& body = *d->body(plate);

    // Sketch on the top face: a rectangle (boss) and a circle (hole).
    const int top = faceWithNormal(body.shape(), {0, 0, 1});
    sketch::Sketch s(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 20}, {0, 0, 1}));
    s.setName("Sketch 1");
    s.setHostBody(plate);
    s.setAttachment(doc::makeAttachment(body, body.features()[0]->id(), top));
    sketch::addRectangle(s, {5, 5}, {15, 12});
    s.addCircle(s.addPoint({28, 15}), 4);
    EXPECT_TRUE(sketch::solve(s).ok);
    const Uuid sketchId = s.id();
    push(std::make_unique<cmd::CreateSketchCommand>(s));
    auto regions = doc::sketchRegions(*d->sketch(sketchId));
    EXPECT_TRUE(regions.ok() && regions.value().size() == 2);
    auto refFor = [&](Vec2 p) {
        for (const auto& r : regions.value())
            if (geom::regionContains(r.face, d->sketch(sketchId)->plane().toWorld(p)))
                return doc::makeProfileRef(r, *d->sketch(sketchId));
        return doc::ProfileRef{};
    };
    auto join = std::make_unique<doc::ExtrudeFeature>();
    join->sketchId = sketchId;
    join->profiles = {refFor({10, 8})};
    join->distance = 6;
    join->mode = doc::ExtrudeMode::Join;
    push(std::make_unique<cmd::AddFeatureCommand>(plate, std::move(join)));
    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = sketchId;
    cut->profiles = {refFor({28, 15})};
    cut->distance = -5;
    cut->mode = doc::ExtrudeMode::Cut;
    cut->throughAll = true;
    push(std::make_unique<cmd::AddFeatureCommand>(plate, std::move(cut)));
    push(std::make_unique<cmd::AddFeatureCommand>(plate, filletVertical(d->body(plate)->shape(), 2.0)));

    // A second body from an XY sketch, rotated, then subtracted from the plate.
    sketch::Sketch s2(Uuid::generate(), sketch::Plane::xy());
    s2.setName("Sketch 2");
    s2.addCircle(s2.addPoint({8, 22}), 3);
    const Uuid sketch2 = s2.id();
    push(std::make_unique<cmd::CreateSketchCommand>(s2));
    auto pin = std::make_unique<doc::ExtrudeFeature>();
    pin->sketchId = sketch2;
    pin->profiles = {doc::makeProfileRef(doc::sketchRegions(*d->sketch(sketch2)).value().front(), *d->sketch(sketch2))};
    pin->distance = 30;
    auto createPin = std::make_unique<cmd::CreateBodyCommand>("Pin", std::move(pin));
    const Uuid pinId = createPin->bodyId();
    push(std::move(createPin));
    auto turn = std::make_unique<doc::MoveFeature>();
    turn->rotates = true;
    turn->rotationCenter = {8, 22, 15};
    turn->rotationAxis = {0, 0, 1};
    turn->rotationAngle = 0.3;
    push(std::make_unique<cmd::AddFeatureCommand>(pinId, std::move(turn)));
    auto combine = std::make_unique<doc::CombineFeature>();
    combine->toolBody = pinId;
    combine->mode = doc::CombineMode::Subtract;
    std::vector<std::unique_ptr<cmd::Command>> steps;
    steps.push_back(std::make_unique<cmd::AddFeatureCommand>(plate, std::move(combine)));
    steps.push_back(std::make_unique<cmd::SetBodyVisibilityCommand>(pinId, false));
    push(std::make_unique<cmd::CompositeCommand>("Subtract", std::move(steps)));

    auto pattern = std::make_unique<doc::PatternFeature>();
    pattern->count = 2;
    pattern->direction = {1, 0, 0};
    pattern->spacing = 45;
    push(std::make_unique<cmd::AddFeatureCommand>(plate, std::move(pattern)));
    auto mirror = std::make_unique<doc::MirrorFeature>();
    mirror->planeOrigin = {0, 0, 0};
    mirror->planeNormal = {0, 1, 0};
    push(std::make_unique<cmd::AddFeatureCommand>(plate, std::move(mirror)));
    return d;
}

std::string savedBytes(const doc::Document& d)
{
    const auto path = tempFile("valid");
    EXPECT_TRUE(io::saveProject(d, path).ok());
    std::string bytes = readBytes(path);
    std::filesystem::remove(path);
    return bytes;
}

// The valid inputs: the rich project and two random modeling sessions.
struct Corpus {
    std::vector<std::string> documents; // document.json text
    std::vector<std::string> archives;  // whole .openshape files
};

const Corpus& corpus()
{
    static const Corpus c = [] {
        Corpus out;
        auto rich = richDocument();
        out.documents.push_back(io::documentToJson(*rich).dump(2));
        out.archives.push_back(savedBytes(*rich));
        for (unsigned seed : {101u, 202u}) {
            StressSession s(seed);
            s.build(18);
            out.documents.push_back(io::documentToJson(s.document).dump(2));
            out.archives.push_back(savedBytes(s.document));
        }
        return out;
    }();
    return c;
}

std::string archiveWithDocument(const std::string& documentJson)
{
    return zipOf({{"document.json", documentJson}, {"metadata.json", R"({"format":"OpenShape","version":1})"}});
}

// Every node of a JSON tree, as pointers.
void collectPointers(const json& node, const json::json_pointer& at, std::vector<json::json_pointer>& out)
{
    out.push_back(at);
    if (node.is_object())
        for (auto it = node.begin(); it != node.end(); ++it)
            collectPointers(it.value(), at / it.key(), out);
    else if (node.is_array())
        for (std::size_t i = 0; i < node.size(); ++i)
            collectPointers(node[i], at / i, out);
}

} // namespace

TEST(ProjectFuzz, CorpusIsValid)
{
    const Corpus& c = corpus();
    ASSERT_EQ(c.archives.size(), 3u);
    for (std::size_t i = 0; i < c.archives.size(); ++i) {
        const Outcome o = loadChecked(c.archives[i], "valid project " + std::to_string(i));
        EXPECT_TRUE(o.loaded) << o.message;
    }
    // The rich project really uses sketches, attachments and a combine.
    const json rich = json::parse(c.documents[0]);
    EXPECT_EQ(rich["sketches"].size(), 2u);
    EXPECT_TRUE(rich["sketches"][0].contains("attachment"));
    EXPECT_EQ(rich["bodies"].size(), 2u);
}

TEST(ProjectFuzz, TruncatedAndBitFlippedArchives)
{
    std::mt19937 rng(1234);
    int loaded = 0, refused = 0;
    for (const std::string& valid : corpus().archives) {
        // Truncation at the edges and inside every structure.
        for (std::size_t length : {std::size_t(0), std::size_t(1), std::size_t(4), std::size_t(22), valid.size() / 3,
                                   valid.size() / 2, valid.size() - 22, valid.size() - 1})
            (loadChecked(valid.substr(0, length), "truncated to " + std::to_string(length)).loaded ? loaded : refused)++;
        // Random byte flips (1 to 8 bytes).
        for (int round = 0; round < 24; ++round) {
            std::string bytes = valid;
            const int flips = std::uniform_int_distribution<int>(1, 8)(rng);
            for (int f = 0; f < flips; ++f) {
                const std::size_t at = std::uniform_int_distribution<std::size_t>(0, bytes.size() - 1)(rng);
                bytes[at] = char(bytes[at] ^ (1 << std::uniform_int_distribution<int>(0, 7)(rng)));
            }
            (loadChecked(bytes, "flipped " + std::to_string(flips) + " bytes, round " + std::to_string(round)).loaded ? loaded
                                                                                                                  : refused)++;
        }
        // Garbage appended, zeroed blocks.
        (loadChecked(valid + std::string(1000, '\x5a'), "garbage appended").loaded ? loaded : refused)++;
        std::string zeroed = valid;
        std::fill(zeroed.begin() + std::ptrdiff_t(zeroed.size() / 2), zeroed.begin() + std::ptrdiff_t(zeroed.size() / 2 + 64), '\0');
        (loadChecked(zeroed, "zeroed block").loaded ? loaded : refused)++;
    }
    EXPECT_GT(refused, 0);
}

TEST(ProjectFuzz, BrokenJsonText)
{
    std::mt19937 rng(99);
    const std::string& text = corpus().documents[0];
    std::vector<std::pair<std::string, std::string>> cases{
        {"empty", ""},
        {"not json", "hello"},
        {"cut in half", text.substr(0, text.size() / 2)},
        {"NaN literal", std::string(R"({"format":"OpenShape","version":1,"lengthUnit":"mm","bodies":[{"x":NaN}]})")},
        {"infinite number", std::string(R"({"format":"OpenShape","version":1e999,"lengthUnit":"mm","bodies":[]})")},
        {"invalid UTF-8", std::string("{\"format\":\"OpenShape\xff\xfe\",\"version\":1}")},
        {"nul bytes", std::string("{\"format\":\"Open\0Shape\"}", sizeof("{\"format\":\"Open\0Shape\"}") - 1)},
        {"deeply nested arrays", std::string(200000, '[') + std::string(200000, ']')},
        {"deeply nested objects", [] {
             std::string s;
             for (int i = 0; i < 100000; ++i)
                 s += "{\"a\":";
             s += "1";
             s += std::string(100000, '}');
             return s;
         }()},
        {"deep nesting inside params", [&] {
             std::string s = text;
             const auto at = s.find("\"params\": {");
             return s.substr(0, at) + "\"params\": " + std::string(100000, '[') + std::string(100000, ']') + ", \"x\": {"
                  + s.substr(at + 11);
         }()},
    };
    for (int round = 0; round < 30; ++round) {
        std::string s = text;
        const std::size_t at = std::uniform_int_distribution<std::size_t>(0, s.size() - 1)(rng);
        const char junk[] = {'{', '}', '[', ']', ',', ':', '"', '\\', '0', 'e', '-'};
        s.insert(s.begin() + std::ptrdiff_t(at), junk[std::uniform_int_distribution<int>(0, 10)(rng)]);
        cases.push_back({"junk character at " + std::to_string(at), s});
    }
    for (const auto& [what, documentJson] : cases)
        loadChecked(archiveWithDocument(documentJson), what);
}

// Every node, one at a time, replaced by a value of the wrong type or a
// hostile number; keys removed; arrays emptied.
TEST(ProjectFuzz, WrongTypesAndHostileValues)
{
    std::mt19937 rng(4242);
    const std::vector<json> replacements{
        json(), json(true), json("text"), json::array(), json::object(), json::array({1, 2}), json(0), json(-1),
        json(1e308), json(-1e308), json(1e-300), json(std::uint64_t(1) << 63), json(-2147483648LL), json(4294967296LL),
        json(0.5), json(12345678901234.0), json("00000000-0000-0000-0000-000000000000"),
        json("../../../etc/passwd")};
    int cases = 0, loaded = 0;
    for (std::size_t doc = 0; doc < corpus().documents.size(); ++doc) {
        const json valid = json::parse(corpus().documents[doc]);
        std::vector<json::json_pointer> pointers;
        collectPointers(valid, json::json_pointer(), pointers);
        // The rich document gets more rounds: it has every reference kind.
        const int rounds = doc == 0 ? 120 : 40;
        for (int round = 0; round < rounds; ++round) {
            json j = valid;
            const auto& target = pointers[std::uniform_int_distribution<std::size_t>(1, pointers.size() - 1)(rng)];
            std::string what = target.to_string();
            // Pattern counts only get small values: a valid 500-copy pattern
            // is slow, not wrong.
            const bool isCount = what.ends_with("/count");
            const int kind = std::uniform_int_distribution<int>(0, 9)(rng);
            if (kind == 0) {
                // Remove the key or element.
                const auto parent = target.parent_pointer();
                if (j.at(parent).is_object())
                    j.at(parent).erase(target.back());
                else
                    j.at(parent).erase(std::stoul(target.back()));
                what += " removed";
            } else {
                const json& value = replacements[std::uniform_int_distribution<std::size_t>(0, replacements.size() - 1)(rng)];
                if (isCount && value.is_number() && std::abs(value.get<double>()) > 600)
                    continue;
                j.at(target) = value;
                what += " = " + value.dump();
            }
            ++cases;
            try {
                auto r = io::documentFromJson(j);
                if (r) {
                    ++loaded;
                    r.value()->recomputeAll();
                    for (const auto& body : r.value()->bodies())
                        for (std::size_t i = 0; i < body->features().size(); ++i)
                            if (body->state(int(i)).status == doc::FeatureStatus::Failed) {
                                EXPECT_TRUE(plainMessage(body->state(int(i)).userMessage)) << what;
                            }
                } else {
                    EXPECT_TRUE(plainMessage(r.userMessage())) << what << " (" << r.developerMessage() << ")";
                }
            } catch (const std::exception& e) {
                ADD_FAILURE() << what << ": exception escaped documentFromJson: " << e.what();
            }
        }
    }
    EXPECT_GT(cases, 150);
    EXPECT_GT(loaded, 0); // some edits are harmless (names, hints)
}

// References to things that are not there, identities used twice, cycles.
TEST(ProjectFuzz, BrokenReferences)
{
    const json valid = json::parse(corpus().documents[0]);
    const std::string plate = valid["bodies"][0]["id"];
    const std::string pin = valid["bodies"][1]["id"];
    const std::string sketch1 = valid["sketches"][0]["id"];
    std::vector<std::pair<std::string, json>> cases;
    auto variant = [&](const std::string& what, const std::function<void(json&)>& edit) {
        json j = valid;
        edit(j);
        cases.push_back({what, j});
    };
    auto featuresOf = [](json& j, std::size_t body) -> json& { return j["bodies"][body]["features"]; };
    variant("sketch used by extrusions removed", [&](json& j) { j["sketches"].erase(0); });
    variant("all sketches removed", [&](json& j) { j.erase("sketches"); });
    variant("tool body of the combine removed", [&](json& j) { j["bodies"].erase(1); });
    variant("bodies listed in reverse order", [&](json& j) { std::reverse(j["bodies"].begin(), j["bodies"].end()); });
    variant("body id used twice", [&](json& j) { j["bodies"][1]["id"] = plate; });
    variant("sketch id equals a body id", [&](json& j) { j["sketches"][0]["id"] = plate; });
    variant("feature id equals the sketch id", [&](json& j) { featuresOf(j, 0)[1]["id"] = sketch1; });
    variant("feature id repeated in another body", [&](json& j) { featuresOf(j, 1)[1]["id"] = featuresOf(j, 0)[1]["id"]; });
    variant("combine with itself", [&](json& j) {
        for (auto& f : featuresOf(j, 0))
            if (f["type"] == "Combine")
                f["params"]["tool"] = plate;
    });
    variant("combine cycle", [&](json& j) {
        featuresOf(j, 1).push_back({{"id", Uuid::generate().toString()},
                                    {"type", "Combine"},
                                    {"name", ""},
                                    {"suppressed", false},
                                    {"params", {{"tool", plate}, {"mode", "Union"}}}});
    });
    variant("attachment to a missing feature", [&](json& j) { j["sketches"][0]["attachment"]["feature"] = Uuid::generate().toString(); });
    variant("attachment to a missing body", [&](json& j) { j["sketches"][0]["attachment"]["body"] = Uuid::generate().toString(); });
    variant("attachment face hint far out of range", [&](json& j) { j["sketches"][0]["attachment"]["faceHint"] = 1 << 30; });
    variant("face hints out of range", [&](json& j) {
        for (auto& f : featuresOf(j, 0))
            if (f["params"].contains("edges"))
                for (auto& e : f["params"]["edges"])
                    e["indexHint"] = -7;
    });
    variant("host body missing", [&](json& j) { j["sketches"][0]["hostBody"] = Uuid::generate().toString(); });
    variant("sketch line to a missing point", [&](json& j) { j["sketches"][0]["lines"][0]["end"] = 999; });
    variant("sketch constraint on a missing entity", [&](json& j) { j["sketches"][0]["constraints"][0]["a"] = 999; });
    variant("sketch nextId below its entities", [&](json& j) { j["sketches"][0]["nextId"] = 2; });
    variant("sketch nextId at the maximum", [&](json& j) { j["sketches"][0]["nextId"] = 4294967295u; });
    variant("sketch plane not orthonormal", [&](json& j) { j["sketches"][0]["plane"]["xAxis"] = json::array({1, 1, 0}); });
    variant("point flag of the wrong type", [&](json& j) { j["sketches"][0]["points"][0]["fixed"] = "yes"; });
    variant("construction flag of the wrong type", [&](json& j) { j["sketches"][0]["lines"][0]["construction"] = 1; });
    variant("circle construction flag of the wrong type", [&](json& j) { j["sketches"][0]["circles"][0]["construction"] = json::array(); });
    variant("first feature not a base feature", [&](json& j) { featuresOf(j, 0).erase(0); });
    variant("no features", [&](json& j) { featuresOf(j, 0) = json::array(); });
    variant("thousands of empty bodies", [&](json& j) {
        for (int i = 0; i < 3000; ++i)
            j["bodies"].push_back({{"id", Uuid::generate().toString()}, {"features", json::array()}});
    });
    variant("pattern with 500 copies of a tiny box", [&](json& j) {
        json box = featuresOf(j, 1)[0];
        j["bodies"].push_back({{"id", Uuid::generate().toString()},
                               {"features",
                                {{{"id", Uuid::generate().toString()},
                                  {"type", "Box"},
                                  {"params", {{"origin", {0, 0, 0}}, {"size", {1, 1, 1}}}}},
                                 {{"id", Uuid::generate().toString()},
                                  {"type", "Pattern"},
                                  {"params", {{"layout", "Linear"}, {"count", 500}, {"direction", {1, 0, 0}}, {"spacing", 0.0}}}}}}});
    });
    variant("pattern spacing astronomically large", [&](json& j) {
        for (auto& f : featuresOf(j, 0))
            if (f["type"] == "Pattern")
                f["params"]["spacing"] = 1e300;
    });
    variant("box of astronomical size", [&](json& j) { featuresOf(j, 0)[0]["params"]["size"] = json::array({1e300, 1e300, 1e300}); });
    variant("box of microscopic size", [&](json& j) { featuresOf(j, 0)[0]["params"]["size"] = json::array({1e-300, 1, 1}); });
    variant("mirror plane with a tiny normal", [&](json& j) {
        for (auto& f : featuresOf(j, 0))
            if (f["type"] == "Mirror")
                f["params"]["normal"] = json::array({1e-8, 0, 0});
    });
    variant("hole with a huge depth", [&](json& j) {
        featuresOf(j, 0).push_back({{"id", Uuid::generate().toString()},
                                    {"type", "Hole"},
                                    {"params",
                                     {{"rim", featuresOf(j, 0)[3]["params"]["edges"][0]},
                                      {"diameter", 3.0},
                                      {"depth", 1e200}}}});
    });
    for (const auto& [what, j] : cases) {
        try {
            const auto start = std::chrono::steady_clock::now();
            auto r = io::documentFromJson(j);
            if (r) {
                r.value()->recomputeAll();
                for (const auto& body : r.value()->bodies())
                    for (std::size_t i = 0; i < body->features().size(); ++i)
                        if (body->state(int(i)).status == doc::FeatureStatus::Failed) {
                            EXPECT_TRUE(plainMessage(body->state(int(i)).userMessage)) << what;
                        }
            } else {
                EXPECT_TRUE(plainMessage(r.userMessage())) << what << " (" << r.developerMessage() << ")";
            }
            const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            EXPECT_LT(seconds, 10.0) << what;
        } catch (const std::exception& e) {
            ADD_FAILURE() << what << ": exception escaped: " << e.what();
        }
    }
}

TEST(ProjectFuzz, UnsafeAndOversizedEntries)
{
    const std::string documentJson = corpus().documents[1];
    std::vector<std::pair<std::string, std::string>> refused{
        {"traversal", zipOf({{"document.json", documentJson}, {"../../evil.txt", "x"}})},
        {"absolute", zipOf({{"document.json", documentJson}, {"/etc/passwd", "x"}})},
        {"drive letter", zipOf({{"document.json", documentJson}, {"C:/Windows/evil.dll", "x"}})},
        {"backslash", zipOf({{"document.json", documentJson}, {"geometry\\..\\..\\x", "x"}})},
        {"nested traversal", zipOf({{"document.json", documentJson}, {"geometry/../../x", "x"}})},
        {"document.json missing", zipOf({{"metadata.json", "{}"}})},
        {"document.json is binary", zipOf({{"document.json", std::string("\x89PNG\r\n\x1a\n\0\0", 10)}})},
        {"document.json is an array", zipOf({{"document.json", "[1,2,3]"}})},
    };
    // Too many entries.
    {
        std::vector<std::pair<std::string, std::string>> many{{"document.json", documentJson}};
        for (int i = 0; i < 10001; ++i)
            many.push_back({"geometry/x" + std::to_string(i), "x"});
        refused.push_back({"10001 entries", zipOf(many)});
    }
    // document.json claiming to be 4 GB (the size fields patched): refused
    // before anything is allocated.
    {
        std::string bytes = zipOf({{"document.json", documentJson}});
        int patched = 0;
        for (std::size_t i = 0; i + 46 <= bytes.size(); ++i) {
            if (std::memcmp(bytes.data() + i, "PK\x01\x02", 4) == 0) {
                // Central directory: uncompressed size at offset 24.
                std::memcpy(bytes.data() + i + 24, "\xf0\xff\xff\xff", 4);
                ++patched;
            }
            if (std::memcmp(bytes.data() + i, "PK\x03\x04", 4) == 0) {
                std::memcpy(bytes.data() + i + 22, "\xf0\xff\xff\xff", 4);
                ++patched;
            }
        }
        EXPECT_EQ(patched, 2);
        refused.push_back({"4 GB entry", bytes});
    }
    for (const auto& [what, bytes] : refused) {
        const Outcome o = loadChecked(bytes, what);
        EXPECT_FALSE(o.loaded) << what;
    }
    // Extra harmless entries are ignored; a stored copy of document.json under
    // another name does not replace it.
    const Outcome extra = loadChecked(
        zipOf({{"document.json", documentJson}, {"notes/readme.txt", "hello"}, {"geometry/unknown.brep", "garbage"}}), "extras");
    EXPECT_TRUE(extra.loaded) << extra.message;
}
