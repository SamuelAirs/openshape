// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketches attached to faces follow those faces; through-all cuts stay through.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "io/ProjectFile.h"

#include <filesystem>

using namespace os;

namespace {

struct Bracket {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid plateSketch, holeSketch, body, plateExtrude, cut;

    explicit Bracket(bool throughAll)
    {
        // 60 x 30 plate, 5 mm.
        auto base = std::make_unique<sketch::Sketch>();
        const auto r = sketch::addRectangle(*base, {0, 0}, {60, 30}, sketch::kOriginId);
        base->addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60});
        base->addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 30});
        sketch::solve(*base);
        plateSketch = base->id();
        document.addSketch(std::move(base));
        auto extrude = std::make_unique<doc::ExtrudeFeature>();
        extrude->sketchId = plateSketch;
        extrude->profiles = {doc::makeProfileRef(doc::sketchRegions(*document.sketch(plateSketch)).value().front(),
                                                 *document.sketch(plateSketch))};
        extrude->distance = 5;
        plateExtrude = extrude->id();
        auto createBody = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(extrude));
        body = createBody->bodyId();
        EXPECT_TRUE(stack.push(std::move(createBody), document).ok());

        // Holes sketched on the top face, attached to it.
        const doc::Body& b = *document.body(body);
        const int top = test::faceWithNormal(b.shape(), {0, 0, 1});
        auto holes = std::make_unique<sketch::Sketch>(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 5}, {0, 0, 1}));
        holes->setHostBody(body);
        holes->setAttachment(doc::makeAttachment(b, plateExtrude, top));
        EXPECT_TRUE(holes->attachment().has_value());
        for (double x : {10.0, 50.0}) {
            const auto c = holes->addPoint({x, 15});
            holes->addCircle(c, 3);
        }
        holeSketch = holes->id();
        document.addSketch(std::move(holes));
        const sketch::Sketch& hs = *document.sketch(holeSketch);
        auto regions = doc::sketchRegions(hs).value();
        auto cutFeature = std::make_unique<doc::ExtrudeFeature>();
        cutFeature->sketchId = holeSketch;
        for (const auto& region : regions)
            cutFeature->profiles.push_back(doc::makeProfileRef(region, hs));
        cutFeature->distance = -5;
        cutFeature->mode = doc::ExtrudeMode::Cut;
        cutFeature->throughAll = throughAll;
        cut = cutFeature->id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(cutFeature)), document).ok());
    }

    double volume() const { return geom::volume(document.body(body)->shape()); }
    double thickness() const { return geom::boundingBox(document.body(body)->shape()).size().z; }
};

double expectedVolume(double thickness)
{
    return (1800.0 - 2 * kPi * 9.0) * thickness;
}

} // namespace

TEST(Attachment, ThickerPlateKeepsThroughHoles)
{
    Bracket b(true);
    EXPECT_NEAR(b.volume(), expectedVolume(5), 1e-3);

    ASSERT_TRUE(b.stack.push(std::make_unique<cmd::SetParameterCommand>(b.plateExtrude, "distance", 8.0, false), b.document).ok());
    EXPECT_FALSE(b.document.body(b.body)->hasFailures());
    EXPECT_NEAR(b.thickness(), 8.0, 1e-6);
    EXPECT_NEAR(b.volume(), expectedVolume(8), 1e-3) << "holes must still go all the way through";
    // The sketch moved up with the top face.
    EXPECT_NEAR(b.document.sketch(b.holeSketch)->plane().origin.z, 8.0, 1e-9);

    b.stack.undo(b.document);
    EXPECT_NEAR(b.volume(), expectedVolume(5), 1e-3);
    EXPECT_NEAR(b.document.sketch(b.holeSketch)->plane().origin.z, 5.0, 1e-9);
}

TEST(Attachment, FixedDepthCutFollowsTheFace)
{
    // Without "through all" the 5 mm cut starts at the (moved) top face.
    Bracket b(false);
    ASSERT_TRUE(b.stack.push(std::make_unique<cmd::SetParameterCommand>(b.plateExtrude, "distance", 8.0, false), b.document).ok());
    const double blind = 1800.0 * 8 - 2 * kPi * 9.0 * 5;
    EXPECT_NEAR(b.volume(), blind, 1e-3);
}

TEST(Attachment, ThinnerPlateStillValid)
{
    Bracket b(true);
    ASSERT_TRUE(b.stack.push(std::make_unique<cmd::SetParameterCommand>(b.plateExtrude, "distance", 3.0, false), b.document).ok());
    EXPECT_FALSE(b.document.body(b.body)->hasFailures());
    EXPECT_NEAR(b.volume(), expectedVolume(3), 1e-3);
    EXPECT_TRUE(geom::isValid(b.document.body(b.body)->shape()));
}

TEST(Attachment, SurvivesSaveAndReload)
{
    Bracket b(true);
    const auto path = std::filesystem::temp_directory_path() / "openshape_attach.openshape";
    ASSERT_TRUE(io::saveProject(b.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_TRUE(d.sketch(b.holeSketch)->attachment().has_value());
    auto* extrude = d.body(b.body)->feature(b.plateExtrude);
    ASSERT_TRUE(extrude->setParameter("distance", 12.0).ok());
    d.featureChanged(b.plateExtrude);
    EXPECT_NEAR(geom::volume(d.body(b.body)->shape()), expectedVolume(12), 1e-3);
}
