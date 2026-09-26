// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrude steps with a draft: exact frustum volumes, symmetric drafts, cuts,
// refusals, the Model panel parameter and the project file.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

using namespace os;

namespace {

double degrees(double d)
{
    return d * kPi / 180.0;
}

double frustumVolume(double a1, double a2, double h)
{
    return h / 3 * (a1 + a2 + std::sqrt(a1 * a2));
}

// A sketch on the XY plane with a 20 x 20 square centered on the origin
// (or a circle of radius 6), and an Extrude step of it.
struct Drafted {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid sketch;

    explicit Drafted(bool round = false)
    {
        auto s = std::make_unique<sketch::Sketch>();
        if (round)
            s->addCircle(sketch::kOriginId, 6);
        else
            (void)sketch::addRectangle(*s, {-10, -10}, {10, 10});
        sketch = s->id();
        document.addSketch(std::move(s));
    }

    std::unique_ptr<doc::ExtrudeFeature> extrude(double distance, double draft) const
    {
        const sketch::Sketch& s = *document.sketch(sketch);
        auto f = std::make_unique<doc::ExtrudeFeature>();
        f->sketchId = sketch;
        f->profiles = {doc::makeProfileRef(doc::sketchRegions(s).value().front(), s)};
        f->distance = distance;
        f->draftAngle = draft;
        return f;
    }

    Result<geom::Shape> preview(const doc::Feature& f, const Uuid& body = Uuid()) const { return document.preview(body, f); }
};

} // namespace

TEST(ExtrudeDraft, SquareAndCircleFrustums)
{
    Drafted square;
    const double t = 15 * std::tan(degrees(10));
    auto pyramid = square.preview(*square.extrude(15, degrees(10)));
    ASSERT_TRUE(pyramid.ok()) << pyramid.developerMessage();
    EXPECT_NEAR(geom::volume(pyramid.value()), frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), 15), 1e-6);
    const auto box = geom::boundingBox(pyramid.value());
    EXPECT_NEAR(box.max.z, 15, 1e-7);
    EXPECT_NEAR(box.size().x, 20, 1e-7);

    Drafted round(true);
    const double r2 = 6 - 8 * std::tan(degrees(20));
    auto cone = round.preview(*round.extrude(8, degrees(20)));
    ASSERT_TRUE(cone.ok()) << cone.developerMessage();
    EXPECT_NEAR(geom::volume(cone.value()), kPi * 8 / 3 * (36 + 6 * r2 + r2 * r2), 1e-6);

    // No draft: the plain extrusion, exactly as before.
    auto straight = square.preview(*square.extrude(15, 0));
    ASSERT_TRUE(straight.ok());
    EXPECT_NEAR(geom::volume(straight.value()), 400 * 15, 1e-6);
}

// Symmetric: both halves narrow away from the sketch plane.
TEST(ExtrudeDraft, SymmetricNarrowsBothWays)
{
    Drafted square;
    auto f = square.extrude(10, degrees(5));
    f->symmetric = true;
    auto shape = square.preview(*f);
    ASSERT_TRUE(shape.ok()) << shape.developerMessage();
    const double t = 5 * std::tan(degrees(5));
    EXPECT_NEAR(geom::volume(shape.value()), 2 * frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), 5), 1e-6);
    const auto box = geom::boundingBox(shape.value());
    EXPECT_NEAR(box.min.z, -5, 1e-7);
    EXPECT_NEAR(box.max.z, 5, 1e-7);
}

// A drafted cut into a block: a tapered pocket (narrowing as it goes in).
TEST(ExtrudeDraft, DraftedCutMakesATaperedPocket)
{
    Drafted square;
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {-20, -20, -10};
    block->size = {40, 40, 10};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(square.stack.push(std::move(create), square.document).ok());
    auto cut = square.extrude(-4, degrees(15));
    cut->mode = doc::ExtrudeMode::Cut;
    auto result = square.preview(*cut, body);
    ASSERT_TRUE(result.ok()) << result.developerMessage();
    const double t = 4 * std::tan(degrees(15));
    EXPECT_NEAR(16000 - geom::volume(result.value()), frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), 4), 1e-6);
    // Through all has no length to draft over.
    cut->throughAll = true;
    const auto refused = square.preview(*cut, body);
    EXPECT_FALSE(refused.ok());
    EXPECT_EQ(refused.userMessage(), "A draft needs a distance: turn off Through all, or set the draft to 0.");
}

TEST(ExtrudeDraft, DraftsThatCloseTheShapeAreRefused)
{
    Drafted square;
    // 20 mm up at 30 degrees: each wall moves in by 11.5 mm, more than half the width.
    const auto closed = square.preview(*square.extrude(20, degrees(30)));
    EXPECT_FALSE(closed.ok());
    EXPECT_EQ(closed.userMessage(), "The draft closes the shape before the full height. Use a smaller angle or a shorter extrusion.");
    Drafted round(true);
    EXPECT_FALSE(round.preview(*round.extrude(10, degrees(35))).ok()) << "6 - 10 tan 35 < 0";
    // 6 - 10 tan 29 = 0.46 mm at the top still works. (At 30 degrees, 0.23 mm,
    // OCCT's draft result fails the validity check: refused too.)
    EXPECT_TRUE(round.preview(*round.extrude(10, degrees(29))).ok());
    EXPECT_FALSE(round.preview(*round.extrude(10, degrees(30))).ok());
}

// "draft" is an editable parameter; files keep it, older files have none.
TEST(ExtrudeDraft, ParameterAndRoundTrip)
{
    Drafted square;
    auto f = square.extrude(10, 0);
    const Uuid featureId = f->id();
    auto create = std::make_unique<cmd::CreateBodyCommand>("Tapered", std::move(f));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(square.stack.push(std::move(create), square.document).ok());
    doc::Feature* feature = square.document.body(body)->feature(featureId);
    ASSERT_TRUE(feature->parameter("draft").has_value());
    EXPECT_DOUBLE_EQ(*feature->parameter("draft"), 0.0);
    nlohmann::json plain;
    feature->writeParams(plain);
    EXPECT_FALSE(plain.contains("draft")) << "no draft: written as before";

    ASSERT_TRUE(square.stack.push(std::make_unique<cmd::SetParameterCommand>(featureId, "draft", degrees(3)), square.document).ok());
    const double t = 10 * std::tan(degrees(3));
    const double expected = frustumVolume(400, (20 - 2 * t) * (20 - 2 * t), 10);
    EXPECT_NEAR(geom::volume(square.document.body(body)->shape()), expected, 1e-6);
    EXPECT_FALSE(feature->setParameter("draft", degrees(95)).ok());

    const auto json = io::documentToJson(square.document);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    EXPECT_NEAR(geom::volume(again.value()->body(body)->shape()), expected, 1e-6);
    EXPECT_EQ(io::documentToJson(*again.value()), json);
    auto broken = json;
    for (auto& b : broken["bodies"])
        for (auto& step : b["features"])
            step["params"]["draft"] = 3.0; // 172 degrees
    EXPECT_FALSE(io::documentFromJson(broken).ok());
}
