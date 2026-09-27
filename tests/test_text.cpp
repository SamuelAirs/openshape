// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Text steps (emboss / deboss) in the document: computed on a plate's face,
// undone and redone, following their face upstream, edited (the words too),
// saved and read back; bad params are refused. Checked by exact volumes
// against the letters' area.
#include "TestFonts.h"
#include "TestHelpers.h"

#include "commands/DocumentCommands.h"
#include "geometry/Holes.h"
#include "geometry/Text.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <cmath>

using namespace os;

namespace {

// A 60 x 30 plate from the origin, `thickness` thick (top face at z = thickness).
struct TextPlate {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid body;
    Uuid box;

    explicit TextPlate(double thickness = 5)
    {
        auto boxFeature = test::boxFeature(60, 30, thickness);
        box = boxFeature->id();
        auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(boxFeature));
        body = create->bodyId();
        EXPECT_TRUE(stack.push(std::move(create), document).ok());
    }
    const geom::Shape& shape() const { return document.body(body)->shape(); }
    double volume() const { return geom::volume(shape()); }

    std::unique_ptr<doc::TextFeature> text(const std::string& words, double depth, Vec2 position = {30, 15},
                                           double size = 8) const
    {
        auto f = std::make_unique<doc::TextFeature>();
        const int top = test::faceWithNormal(shape(), {0, 0, 1});
        EXPECT_GE(top, 0);
        f->face = {top, *geom::captureFaceSignature(shape(), top)};
        f->position = position;
        f->text = words;
        f->size = size;
        f->depth = depth;
        return f;
    }
};

// The letters' area (flat, in the XY plane) for `words` at `size`.
double letterArea(const std::string& words, double size, const std::string& font = doc::kTextFontRegular)
{
    auto faces = geom::textFaces({words, font, size});
    EXPECT_TRUE(faces) << faces.developerMessage();
    return faces ? geom::surfaceArea(faces.value()) : 0.0;
}

} // namespace

TEST(Text, EmbossAndDebossStepsWithUndoRedo)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate;
    const double v0 = plate.volume();
    const double area = letterArea("Hello OB", 8);
    ASSERT_GT(area, 20);

    auto raised = plate.text("Hello OB", 1.0);
    const Uuid raisedId = raised->id();
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, std::move(raised)), plate.document).ok());
    EXPECT_NEAR(plate.volume() - v0, area * 1.0, 1e-5 * area) << "raised by area x 1 mm";
    EXPECT_NEAR(geom::boundingBox(plate.shape()).max.z, 6.0, 1e-6);
    EXPECT_EQ(plate.shape().solidCount(), 1);
    EXPECT_EQ(plate.document.body(plate.body)->features().back()->id(), raisedId);

    ASSERT_TRUE(plate.stack.undo(plate.document));
    EXPECT_NEAR(plate.volume(), v0, 1e-9);
    ASSERT_TRUE(plate.stack.redo(plate.document));
    EXPECT_NEAR(plate.volume() - v0, area, 1e-5 * area);
    ASSERT_TRUE(plate.stack.undo(plate.document));

    // Cut 0.6 mm in instead.
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, plate.text("Hello OB", -0.6)),
                                 plate.document).ok());
    EXPECT_NEAR(v0 - plate.volume(), area * 0.6, 1e-5 * area) << "cut in by area x 0.6 mm";
    EXPECT_NEAR(geom::boundingBox(plate.shape()).max.z, 5.0, 1e-6);
    ASSERT_TRUE(plate.stack.undo(plate.document));
    EXPECT_NEAR(plate.volume(), v0, 1e-9);

    // A lone O cut in: its middle keeps the full 5 mm of plate, its ring is 0.6 mm lower.
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, plate.text("O", -0.6, {30, 15}, 20)),
                                 plate.document).ok());
    const auto middle = geom::materialDepth(plate.shape(), {30, 15, 5}, {0, 0, -1});
    ASSERT_TRUE(middle.has_value());
    EXPECT_NEAR(*middle, 5.0, 1e-6) << "the counter is not cut";
    EXPECT_NEAR(v0 - plate.volume(), letterArea("O", 20) * 0.6, 1e-5 * letterArea("O", 20));
}

TEST(Text, AStepThatCannotBeMadeIsRefused)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate;
    Status empty = plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, plate.text("", 1)), plate.document);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.userMessage(), "Type the text first.");
    Status spaces = plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, plate.text("   ", 1)), plate.document);
    ASSERT_FALSE(spaces);
    EXPECT_EQ(spaces.userMessage(), "Type the text first.");
    Status off = plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, plate.text("Hi", 1, {90, 15})),
                                  plate.document);
    ASSERT_FALSE(off);
    EXPECT_EQ(off.userMessage(), "The text no longer lies on its face.");
    auto unknownFont = plate.text("Hi", 1);
    unknownFont->font = "Comic Sans 2099";
    Status font = plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, std::move(unknownFont)), plate.document);
    ASSERT_FALSE(font);
    EXPECT_EQ(font.userMessage(), "The font \"Comic Sans 2099\" is not available in this version of OpenShape.");
    EXPECT_EQ(plate.document.body(plate.body)->features().size(), 1u) << "nothing was added";
}

// The text rides along with its face: a thicker plate lifts it; a plate cut
// narrower than its center fails the step with a message.
TEST(Text, TextFollowsItsFaceUpstream)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate;
    plate.document.insertFeature(plate.body, plate.text("Up", 2.0, {45, 15}, 6));
    doc::Body& body = *plate.document.body(plate.body);
    ASSERT_FALSE(body.hasFailures()) << body.state(1).userMessage;
    const double area = letterArea("Up", 6);
    ASSERT_TRUE(body.feature(plate.box)->setParameter("height", 8).ok());
    plate.document.featureChanged(plate.box);
    ASSERT_FALSE(body.hasFailures()) << body.state(1).userMessage;
    EXPECT_NEAR(geom::boundingBox(plate.shape()).max.z, 10.0, 1e-6);
    EXPECT_NEAR(plate.volume(), 60 * 30 * 8 + area * 2.0, 1e-5 * area);
    ASSERT_TRUE(body.feature(plate.box)->setParameter("width", 40).ok());
    plate.document.featureChanged(plate.box);
    ASSERT_TRUE(body.hasFailures());
    EXPECT_EQ(body.state(1).userMessage, "The text no longer lies on its face.");
}

// Size, depth and angle are numbers in the Model panel; the words are a
// string parameter, changed through an undoable command.
TEST(Text, ParametersAndTheWordsCanBeEdited)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate;
    auto f = plate.text("AB", 1.0);
    const Uuid id = f->id();
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, std::move(f)), plate.document).ok());
    const double v0 = 60 * 30 * 5;
    doc::Feature& step = *plate.document.body(plate.body)->feature(id);
    ASSERT_EQ(step.textParameters().size(), 1u);
    EXPECT_EQ(*step.textParameter("text"), "AB");
    ASSERT_EQ(step.parameters().size(), 3u);
    EXPECT_EQ(step.parameters()[0].key, "size");
    EXPECT_EQ(step.parameters()[2].kind, doc::ParameterKind::Angle);

    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::SetTextParameterCommand>(id, "text", "ABBA"), plate.document).ok());
    EXPECT_EQ(*step.textParameter("text"), "ABBA");
    EXPECT_NEAR(plate.volume() - v0, letterArea("ABBA", 8), 1e-5 * letterArea("ABBA", 8));
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "depth", -0.5), plate.document).ok());
    EXPECT_NEAR(v0 - plate.volume(), letterArea("ABBA", 8) * 0.5, 1e-5 * letterArea("ABBA", 8));
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "angle", kPi / 2), plate.document).ok());
    // Flat-topped capitals span exactly the capital height (round and pointed
    // ones overshoot it a little in most fonts, Noto Sans included).
    const auto box = geom::boundingBox(geom::placedTextFaces({"HEH", doc::kTextFontRegular, 8},
                                                             {{30, 15, 5}, {0, 1, 0}, {0, 0, 1}}).value());
    EXPECT_NEAR(box.size().x, 8.0, 1e-6) << "turned: the capitals run along Y";
    EXPECT_NEAR(v0 - plate.volume(), letterArea("ABBA", 8) * 0.5, 1e-5 * letterArea("ABBA", 8));

    // What could never be made is refused, and nothing changes.
    Status empty = plate.stack.push(std::make_unique<cmd::SetTextParameterCommand>(id, "text", ""), plate.document);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.userMessage(), "Type the text first.");
    EXPECT_FALSE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "depth", 0.0), plate.document));
    EXPECT_FALSE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "size", 0.1), plate.document));
    EXPECT_EQ(*step.textParameter("text"), "ABBA");

    // Undo walks back: angle, depth, then the words.
    ASSERT_TRUE(plate.stack.undo(plate.document));
    ASSERT_TRUE(plate.stack.undo(plate.document));
    EXPECT_NEAR(plate.volume() - v0, letterArea("ABBA", 8), 1e-5 * letterArea("ABBA", 8));
    ASSERT_TRUE(plate.stack.undo(plate.document));
    EXPECT_EQ(*step.textParameter("text"), "AB");
    EXPECT_NEAR(plate.volume() - v0, letterArea("AB", 8), 1e-5 * letterArea("AB", 8));
}

TEST(Text, TextStepsRoundTripThroughTheProjectFile)
{
    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate(4);
    auto f = plate.text("Caf\xC3\xA9 42", -0.8, {31.25, 14.5}, 7.5);
    f->angle = 0.25;
    plate.document.insertFeature(plate.body, std::move(f));
    ASSERT_FALSE(plate.document.body(plate.body)->hasFailures())
        << plate.document.body(plate.body)->state(1).userMessage;
    const double volume = plate.volume();
    const auto json = io::documentToJson(plate.document);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    const doc::Body& body = *again.value()->body(plate.body);
    EXPECT_FALSE(body.hasFailures());
    EXPECT_NEAR(geom::volume(body.shape()), volume, 1e-6);
    const auto& text = static_cast<const doc::TextFeature&>(*body.features()[1]);
    EXPECT_EQ(text.text, "Caf\xC3\xA9 42");
    EXPECT_DOUBLE_EQ(text.size, 7.5);
    EXPECT_DOUBLE_EQ(text.depth, -0.8);
    EXPECT_DOUBLE_EQ(text.angle, 0.25);
    EXPECT_DOUBLE_EQ(text.position.x, 31.25);
    EXPECT_DOUBLE_EQ(text.position.y, 14.5);
    EXPECT_EQ(text.font, doc::kTextFontRegular);
    EXPECT_EQ(io::documentToJson(*again.value()), json);

    // Broken params are refused with the file's usual message.
    for (const auto& [key, value] : std::vector<std::pair<std::string, nlohmann::json>>{
             {"text", ""}, {"text", 42}, {"depth", 0.0}, {"size", -1.0}, {"size", "big"}, {"font", ""},
             {"position", nlohmann::json::array({1.0})}, {"angle", "left"}}) {
        auto broken = json;
        for (auto& feature : broken["bodies"][0]["features"])
            if (feature["type"] == "Text")
                feature["params"][key] = value;
        auto refused = io::documentFromJson(broken);
        EXPECT_FALSE(refused.ok()) << key << " = " << value.dump();
        if (!refused.ok()) {
            EXPECT_EQ(refused.userMessage(), "The file contains invalid text.") << key;
        }
    }
    // A font this version does not have: the file opens, the step says why it failed.
    auto otherFont = json;
    for (auto& feature : otherFont["bodies"][0]["features"])
        if (feature["type"] == "Text")
            feature["params"]["font"] = "NotoSerif-Regular";
    auto opened = io::documentFromJson(otherFont);
    ASSERT_TRUE(opened.ok()) << opened.developerMessage();
    const doc::Body& withOtherFont = *opened.value()->body(plate.body);
    ASSERT_TRUE(withOtherFont.hasFailures());
    EXPECT_EQ(withOtherFont.state(1).userMessage, "The font \"NotoSerif-Regular\" is not available in this version of OpenShape.");
}

// An angle is a direction: whatever is typed in the Model panel or read from
// a file (finite), the step keeps the same direction in [0, 2 pi), so a
// project never holds an angle it could not open again.
TEST(Text, AnglesAreKeptAsDirections)
{
    EXPECT_DOUBLE_EQ(doc::TextFeature::normalizedAngle(0.25), 0.25) << "in range: exactly as it is";
    EXPECT_NEAR(doc::TextFeature::normalizedAngle(-kPi / 2), 3 * kPi / 2, 1e-12);
    EXPECT_NEAR(doc::TextFeature::normalizedAngle(5 * kPi / 2), kPi / 2, 1e-12);
    EXPECT_EQ(doc::TextFeature::normalizedAngle(2 * kPi), 0.0);
    EXPECT_EQ(doc::TextFeature::normalizedAngle(-2 * kPi), 0.0);
    EXPECT_NEAR(doc::TextFeature::normalizedAngle(-100000 * kPi / 180), 80 * kPi / 180, 1e-9); // 278 turns back, then 80 degrees
    for (double a = -20; a <= 20; a += 0.37) {
        const double n = doc::TextFeature::normalizedAngle(a);
        EXPECT_GE(n, 0.0) << a;
        EXPECT_LT(n, 2 * kPi) << a;
        EXPECT_NEAR(std::cos(n), std::cos(a), 1e-12) << a;
        EXPECT_NEAR(std::sin(n), std::sin(a), 1e-12) << a;
    }

    OS_REQUIRE_TEST_FONT(doc::kTextFontRegular);
    TextPlate plate;
    auto f = plate.text("AB", 1.0);
    const Uuid id = f->id();
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::AddFeatureCommand>(plate.body, std::move(f)), plate.document).ok());
    const doc::Feature& step = *plate.document.body(plate.body)->feature(id);
    // -90 degrees in the Model panel: stored as 270.
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "angle", -kPi / 2), plate.document).ok());
    EXPECT_NEAR(*step.parameter("angle"), 3 * kPi / 2, 1e-12);
    const double turned = plate.volume();
    // 450 degrees: 90.
    ASSERT_TRUE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "angle", 5 * kPi / 2), plate.document).ok());
    EXPECT_NEAR(*step.parameter("angle"), kPi / 2, 1e-12);
    EXPECT_NEAR(plate.volume(), turned, 1e-6) << "a half turn more raises the same letters";
    EXPECT_FALSE(plate.stack.push(std::make_unique<cmd::SetParameterCommand>(id, "angle", std::nan("")), plate.document));

    // A file with an angle of many turns (or a negative one) opens at the same direction.
    const auto json = io::documentToJson(plate.document);
    for (const double stored : {-kPi / 2 - 200 * kPi, 3 * kPi / 2 + 2000 * kPi}) {
        auto many = json;
        for (auto& feature : many["bodies"][0]["features"])
            if (feature["type"] == "Text")
                feature["params"]["angle"] = stored;
        auto opened = io::documentFromJson(many);
        ASSERT_TRUE(opened.ok()) << opened.developerMessage();
        const doc::Body& body = *opened.value()->body(plate.body);
        EXPECT_FALSE(body.hasFailures());
        EXPECT_NEAR(*body.feature(id)->parameter("angle"), kPi / 2 + kPi, 1e-9) << stored;
    }
}
