// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Loft steps (doc::LoftFeature): profiles of sketches on the ground and on a
// construction plane above, joined into a new body, joined to or cut from a
// body; following sketch edits and the plane's distance; failing plainly
// when a profile is gone; suppress, delete, undo/redo, duplicate, save/open.
#include "TestHelpers.h"

#include "document/Datum.h"
#include "document/SketchProfiles.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <cmath>
#include <filesystem>

using namespace os;

namespace {

// h/3 (A1 + A2 + sqrt(A1 A2)): a pyramid or cone frustum.
double frustum(double a1, double a2, double h)
{
    return h / 3 * (a1 + a2 + std::sqrt(a1 * a2));
}

// A document with a construction plane `height` above the ground (offset
// from XY) and helpers to sketch on the ground or on that plane.
struct Scene {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid plane;

    explicit Scene(double height = 30)
    {
        doc::Datum datum;
        datum.method = doc::DatumMethod::PlaneOffset;
        datum.originIndex = 2;
        datum.distance = height;
        plane = datum.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddDatumCommand>(std::move(datum)), document).ok());
    }

    // A sketch on the ground (or, `onPlane`, on the construction plane) with
    // a square of `side` centered on the origin.
    Uuid square(double side, bool onPlane)
    {
        sketch::Sketch sk(Uuid::generate(), onPlane ? doc::sketchPlaneOn(document.datum(plane)->geometry()) : sketch::Plane::xy());
        if (onPlane)
            sk.setDatumPlane(plane);
        sketch::addRectangle(sk, {-side / 2, -side / 2}, {side / 2, side / 2});
        return add(std::move(sk));
    }
    Uuid circle(double radius, bool onPlane)
    {
        sketch::Sketch sk(Uuid::generate(), onPlane ? doc::sketchPlaneOn(document.datum(plane)->geometry()) : sketch::Plane::xy());
        if (onPlane)
            sk.setDatumPlane(plane);
        sk.addCircle(sk.addPoint({0, 0}), radius);
        return add(std::move(sk));
    }
    Uuid add(sketch::Sketch sk)
    {
        const Uuid id = sk.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(sk)), document).ok());
        return id;
    }
    // The (largest) profile of a sketch as a loft section.
    doc::LoftSection section(const Uuid& sketchId) const
    {
        const sketch::Sketch& sk = *document.sketch(sketchId);
        const auto regions = doc::sketchRegions(sk);
        EXPECT_TRUE(regions.ok() && !regions.value().empty());
        return {sketchId, doc::makeProfileRef(regions.value().front(), sk)};
    }
    std::unique_ptr<doc::LoftFeature> loft(std::vector<Uuid> sketches, bool ruled,
                                           doc::ExtrudeMode mode = doc::ExtrudeMode::NewBody) const
    {
        auto f = std::make_unique<doc::LoftFeature>();
        for (const Uuid& s : sketches)
            f->sections.push_back(section(s));
        f->ruled = ruled;
        f->mode = mode;
        return f;
    }
    // Adds the loft as a new body; returns its id.
    Uuid loftBody(std::vector<Uuid> sketches, bool ruled)
    {
        auto create = std::make_unique<cmd::CreateBodyCommand>("Loft", loft(std::move(sketches), ruled));
        const Uuid id = create->bodyId();
        const Status status = stack.push(std::move(create), document);
        EXPECT_TRUE(status.ok()) << status.userMessage() << " / " << status.developerMessage();
        return id;
    }
    const doc::Body& body(const Uuid& id) const { return *document.body(id); }
    double volume(const Uuid& id) const { return geom::volume(document.body(id)->shape()); }
    geom::BoundingBox box(const Uuid& id) const { return geom::boundingBox(document.body(id)->shape()); }
    void setDistance(double d)
    {
        doc::Datum edited = *document.datum(plane);
        ASSERT_TRUE(edited.setParameter("distance", d).ok());
        ASSERT_TRUE(stack.push(std::make_unique<cmd::EditDatumCommand>(edited, "Change distance"), document).ok());
    }
};

} // namespace

TEST(LoftFeature, TwoSquaresMakeAPrism)
{
    Scene scene(20);
    const Uuid lower = scene.square(10, false), upper = scene.square(10, true);
    const Uuid body = scene.loftBody({lower, upper}, false);
    EXPECT_NEAR(scene.volume(body), 2000.0, 1e-6);
    const auto box = scene.box(body);
    EXPECT_NEAR(box.min.x, -5, 1e-6);
    EXPECT_NEAR(box.max.x, 5, 1e-6);
    EXPECT_NEAR(box.min.z, 0, 1e-6);
    EXPECT_NEAR(box.max.z, 20, 1e-6);
    // A base step: the body's history is the loft alone.
    ASSERT_EQ(scene.body(body).features().size(), 1u);
    EXPECT_EQ(scene.body(body).features()[0]->kind(), doc::FeatureKind::Loft);
    EXPECT_TRUE(scene.body(body).features()[0]->isBaseFeature());
    // Both sketches are now used by it: neither can be deleted.
    EXPECT_EQ(scene.document.dependentFeatures(lower).size(), 1u);
    EXPECT_EQ(scene.document.dependentFeatures(upper).size(), 1u);
    EXPECT_FALSE(scene.stack.push(std::make_unique<cmd::DeleteSketchCommand>(upper), scene.document).ok());
}

TEST(LoftFeature, FrustumFollowsThePlaneAndTheSketches)
{
    Scene scene(30);
    const Uuid lower = scene.square(20, false), upper = scene.square(10, true);
    const Uuid body = scene.loftBody({lower, upper}, true);
    EXPECT_NEAR(scene.volume(body), 7000.0, 1e-6);
    EXPECT_NEAR(scene.box(body).max.z, 30, 1e-6);

    // The plane's distance, as edited in the Model panel: the upper sketch
    // and the loft follow.
    scene.setDistance(40);
    EXPECT_NEAR(scene.document.sketch(upper)->plane().origin.z, 40, 1e-9);
    EXPECT_FALSE(scene.body(body).hasFailures());
    EXPECT_NEAR(scene.volume(body), frustum(400, 100, 40), 1e-6);
    EXPECT_NEAR(scene.box(body).max.z, 40, 1e-6);
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_NEAR(scene.volume(body), 7000.0, 1e-6);
    ASSERT_TRUE(scene.stack.redo(scene.document));
    EXPECT_NEAR(scene.volume(body), frustum(400, 100, 40), 1e-6);
    ASSERT_TRUE(scene.stack.undo(scene.document));

    // A sketch edit: the upper square grows to 16 x 16.
    sketch::Sketch bigger = *scene.document.sketch(upper);
    for (const auto& [id, point] : bigger.points())
        if (id != sketch::kOriginId)
            bigger.point(id)->position = point.position * 1.6;
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::EditSketchCommand>(bigger, "Edit sketch"), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), frustum(400, 256, 30), 1e-6);
    EXPECT_NEAR(scene.box(body).size().x, 20, 1e-6);
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_NEAR(scene.volume(body), 7000.0, 1e-6);
}

TEST(LoftFeature, CirclesMakeAConeFrustumEitherWay)
{
    const double expected = kPi * 10 * (100 + 50 + 25); // h/3 = 10
    for (bool ruled : {false, true}) {
        Scene scene(30);
        const Uuid body = scene.loftBody({scene.circle(10, false), scene.circle(5, true)}, ruled);
        EXPECT_NEAR(scene.volume(body), expected, 1e-4) << (ruled ? "straight" : "smooth");
        EXPECT_NEAR(expected, 5497.787143782138, 1e-9);
    }
}

TEST(LoftFeature, SmoothOrStraightInTheModelPanel)
{
    // Three profiles: 10 x 10 on the ground, 30 x 30 at 15, 10 x 10 at 30.
    Scene scene(15);
    const Uuid bottom = scene.square(10, false), middle = scene.square(30, true);
    doc::Datum top;
    top.method = doc::DatumMethod::PlaneOffset;
    top.originIndex = 2;
    top.distance = 30;
    const Uuid topPlane = top.id();
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddDatumCommand>(std::move(top)), scene.document).ok());
    sketch::Sketch sk(Uuid::generate(), doc::sketchPlaneOn(scene.document.datum(topPlane)->geometry()));
    sk.setDatumPlane(topPlane);
    sketch::addRectangle(sk, {-5, -5}, {5, 5});
    const Uuid upper = scene.add(std::move(sk));

    const Uuid body = scene.loftBody({bottom, middle, upper}, false);
    const double smooth = scene.volume(body);
    EXPECT_GT(smooth, 13000.0 + 100.0); // bulges past the straight faces
    const Uuid step = scene.body(body).features()[0]->id();
    const auto choices = scene.body(body).features()[0]->textParameters();
    ASSERT_EQ(choices.size(), 1u); // a new body: no Join / Cut choice
    EXPECT_EQ(choices[0].key, "sections");
    EXPECT_EQ(choices[0].value, "Smooth");
    EXPECT_EQ(choices[0].choices, (std::vector<std::string>{"Smooth", "Straight"}));

    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::SetTextParameterCommand>(step, "sections", "Straight"), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), 13000.0, 1e-6);
    EXPECT_EQ(*scene.body(body).features()[0]->textParameter("sections"), "Straight");
    // Both pass through the middle profile: 30 wide.
    EXPECT_NEAR(scene.box(body).size().x, 30, 1e-4);
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_NEAR(scene.volume(body), smooth, 1e-6);
    EXPECT_NEAR(scene.box(body).size().x, 30, 1e-4);
    // Anything else is refused, changing nothing.
    EXPECT_FALSE(scene.stack.push(std::make_unique<cmd::SetTextParameterCommand>(step, "sections", "Wavy"), scene.document).ok());
    EXPECT_FALSE(scene.stack.push(std::make_unique<cmd::SetTextParameterCommand>(step, "mode", "Cut"), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), smooth, 1e-6);
}

TEST(LoftFeature, JoinGrowsTheBodyAndCutRemoves)
{
    // A 20 x 20 x 10 block on the ground centered on the origin; a loft from
    // its top (a 20 x 20 square at z = 10) to a 10 x 10 square at z = 40.
    Scene scene(40);
    auto block = test::boxFeature(20, 20, 10);
    block->origin = {-10, -10, 0};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    ASSERT_TRUE(scene.stack.push(std::move(create), scene.document).ok());
    sketch::Sketch onTop(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 10}, {0, 0, 1}));
    onTop.setHostBody(body);
    sketch::addRectangle(onTop, {-10, -10}, {10, 10});
    const Uuid top = scene.add(std::move(onTop));
    const Uuid upper = scene.square(10, true);

    auto joinStep = scene.loft({top, upper}, true, doc::ExtrudeMode::Join);
    const Uuid joinId = joinStep->id();
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(joinStep)), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), 4000.0 + 7000.0, 1e-6);
    EXPECT_EQ(scene.body(body).shape().solidCount(), 1);
    EXPECT_NEAR(scene.box(body).max.z, 40, 1e-6);
    // Join or Cut in the Model panel.
    const auto choices = scene.body(body).feature(joinId)->textParameters();
    ASSERT_EQ(choices.size(), 2u);
    EXPECT_EQ(choices[1].key, "mode");
    EXPECT_EQ(choices[1].value, "Join");

    // Suppressed: the block alone; restored: grown again.
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::SetFeatureSuppressedCommand>(joinId, true), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), 4000.0, 1e-6);
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_NEAR(scene.volume(body), 11000.0, 1e-6);
    // Deleted: the block alone; undo brings it back.
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::DeleteFeatureCommand>(joinId), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), 4000.0, 1e-6);
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_NEAR(scene.volume(body), 11000.0, 1e-6);

    // A cut: a frustum from 10 x 10 on the ground up to 20 x 20 at the top
    // of a 30 x 30 x 10 slab, taken out of it (a funnel).
    Scene cutScene(10);
    auto slab = test::boxFeature(30, 30, 10);
    slab->origin = {-15, -15, 0};
    auto createTower = std::make_unique<cmd::CreateBodyCommand>("Slab", std::move(slab));
    const Uuid towerId = createTower->bodyId();
    ASSERT_TRUE(cutScene.stack.push(std::move(createTower), cutScene.document).ok());
    auto cut = cutScene.loft({cutScene.square(10, false), cutScene.square(20, true)}, true, doc::ExtrudeMode::Cut);
    const Uuid cutId = cut->id();
    ASSERT_TRUE(cutScene.stack.push(std::make_unique<cmd::AddFeatureCommand>(towerId, std::move(cut)), cutScene.document).ok());
    EXPECT_NEAR(cutScene.volume(towerId), 9000.0 - frustum(100, 400, 10), 1e-6);
    // Cut -> Join (Model panel): the loft lies inside, so the slab is whole again.
    ASSERT_TRUE(cutScene.stack.push(std::make_unique<cmd::SetTextParameterCommand>(cutId, "mode", "Join"), cutScene.document).ok());
    EXPECT_NEAR(cutScene.volume(towerId), 9000.0, 1e-6);
    ASSERT_TRUE(cutScene.stack.undo(cutScene.document));
    EXPECT_NEAR(cutScene.volume(towerId), 9000.0 - frustum(100, 400, 10), 1e-6);
    ASSERT_TRUE(cutScene.stack.undo(cutScene.document));
    EXPECT_NEAR(cutScene.volume(towerId), 9000.0, 1e-6);
    ASSERT_TRUE(cutScene.stack.redo(cutScene.document));
    EXPECT_NEAR(cutScene.volume(towerId), 9000.0 - frustum(100, 400, 10), 1e-6);
}

TEST(LoftFeature, FailsPlainly)
{
    Scene scene(30);
    const Uuid lower = scene.square(20, false), upper = scene.square(10, true);
    const Uuid body = scene.loftBody({lower, upper}, true);
    ASSERT_NEAR(scene.volume(body), 7000.0, 1e-6);

    // The upper square opened (a side removed): the profile is gone; the
    // step fails with a message and the body keeps its last good shape.
    sketch::Sketch open = *scene.document.sketch(upper);
    ASSERT_FALSE(open.lines().empty());
    open.remove(open.lines().begin()->first);
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::EditSketchCommand>(open, "Edit sketch"), scene.document).ok());
    const doc::FeatureState& state = scene.body(body).state(0);
    EXPECT_EQ(state.status, doc::FeatureStatus::Failed);
    EXPECT_EQ(state.userMessage, "A profile this loft used is no longer closed or no longer exists.");
    ASSERT_TRUE(scene.stack.undo(scene.document));
    EXPECT_FALSE(scene.body(body).hasFailures());
    EXPECT_NEAR(scene.volume(body), 7000.0, 1e-6);

    // Refused as new steps (nothing changes): one profile, two in one plane,
    // a sketch that does not exist.
    const std::size_t bodies = scene.document.bodies().size();
    auto refused = [&](std::unique_ptr<doc::LoftFeature> f) {
        const Status status = scene.stack.push(std::make_unique<cmd::CreateBodyCommand>("Nope", std::move(f)), scene.document);
        EXPECT_EQ(scene.document.bodies().size(), bodies);
        return status.ok() ? std::string("(accepted)") : status.userMessage();
    };
    EXPECT_EQ(refused(scene.loft({lower}, false)), "Select two or more closed profiles to loft.");
    const Uuid beside = scene.circle(3, false);
    sketch::Sketch moved = *scene.document.sketch(beside);
    moved.point(moved.circles().begin()->second.center)->position = {30, 0};
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::EditSketchCommand>(moved, "Edit sketch"), scene.document).ok());
    EXPECT_NE(refused(scene.loft({lower, beside}, false)).find("same plane"), std::string::npos);
    auto ghost = scene.loft({lower, upper}, false);
    ghost->sections[1].sketchId = Uuid::generate();
    EXPECT_EQ(refused(std::move(ghost)), "A sketch this loft used no longer exists.");
}

TEST(LoftFeature, DuplicateTakesItsOwnSketches)
{
    Scene scene(30);
    const Uuid lower = scene.square(20, false), upper = scene.square(10, true);
    const Uuid body = scene.loftBody({lower, upper}, true);
    auto duplicate = std::make_unique<cmd::DuplicateBodyCommand>(body);
    const Uuid copy = duplicate->copyId();
    ASSERT_TRUE(scene.stack.push(std::move(duplicate), scene.document).ok());
    ASSERT_NE(scene.document.body(copy), nullptr);
    EXPECT_NEAR(scene.volume(copy), 7000.0, 1e-6);
    const auto& loft = static_cast<const doc::LoftFeature&>(*scene.body(copy).features()[0]);
    ASSERT_EQ(loft.sections.size(), 2u);
    EXPECT_NE(loft.sections[0].sketchId, lower);
    EXPECT_NE(loft.sections[1].sketchId, upper);
    EXPECT_NE(scene.document.sketch(loft.sections[0].sketchId), nullptr);
    EXPECT_NE(scene.document.sketch(loft.sections[1].sketchId), nullptr);
    // Editing the source's upper sketch leaves the copy alone.
    sketch::Sketch bigger = *scene.document.sketch(upper);
    for (const auto& [id, point] : bigger.points())
        if (id != sketch::kOriginId)
            bigger.point(id)->position = point.position * 1.6;
    ASSERT_TRUE(scene.stack.push(std::make_unique<cmd::EditSketchCommand>(bigger, "Edit sketch"), scene.document).ok());
    EXPECT_NEAR(scene.volume(body), frustum(400, 256, 30), 1e-6);
    EXPECT_NEAR(scene.volume(copy), 7000.0, 1e-6);
}

TEST(LoftFeature, SaveAndOpen)
{
    Scene scene(30);
    const Uuid lower = scene.square(20, false), upper = scene.circle(5, true);
    const Uuid body = scene.loftBody({lower, upper}, false);
    const double volume = scene.volume(body);
    EXPECT_GT(volume, 0.0);

    const auto json = io::documentToJson(scene.document);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    EXPECT_EQ(io::documentToJson(*again.value()), json);
    const doc::Body* reopened = again.value()->body(body);
    ASSERT_NE(reopened, nullptr);
    EXPECT_FALSE(reopened->hasFailures());
    EXPECT_NEAR(geom::volume(reopened->shape()), volume, 1e-6);
    const auto& loft = static_cast<const doc::LoftFeature&>(*reopened->features()[0]);
    ASSERT_EQ(loft.sections.size(), 2u);
    EXPECT_EQ(loft.sections[0].sketchId, lower);
    EXPECT_EQ(loft.sections[1].sketchId, upper);
    EXPECT_FALSE(loft.ruled);
    EXPECT_EQ(loft.mode, doc::ExtrudeMode::NewBody);

    // Through a real file, and the plane still moves the loft after opening.
    const auto path = std::filesystem::temp_directory_path() / "openshape_test_loft.openshape";
    ASSERT_TRUE(io::saveProject(scene.document, path).ok());
    auto loaded = io::loadProject(path);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    EXPECT_NEAR(geom::volume(d.body(body)->shape()), volume, 1e-6);
    doc::Datum edited = *d.datum(scene.plane);
    ASSERT_TRUE(edited.setParameter("distance", 50.0).ok());
    d.replaceDatum(edited);
    EXPECT_NEAR(geom::boundingBox(d.body(body)->shape()).max.z, 50, 1e-6);
    std::filesystem::remove(path);

    // What a reader refuses: fewer than two profiles, a bad mode, a
    // non-boolean "ruled", a profile without its point.
    auto broken = [&](auto change) {
        nlohmann::json j = json;
        for (auto& b : j["bodies"])
            for (auto& f : b["features"])
                if (f["type"] == "Loft")
                    change(f["params"]);
        return io::documentFromJson(j);
    };
    auto oneSection = broken([](nlohmann::json& p) { p["sections"].erase(1); });
    ASSERT_FALSE(oneSection.ok());
    EXPECT_EQ(oneSection.userMessage(), "The file contains an invalid loft.");
    EXPECT_FALSE(broken([](nlohmann::json& p) { p["mode"] = "Sideways"; }).ok());
    EXPECT_FALSE(broken([](nlohmann::json& p) { p["ruled"] = "yes"; }).ok());
    EXPECT_FALSE(broken([](nlohmann::json& p) { p["sections"][0].erase("point"); }).ok());
    // A build that does not know lofts refuses the file (unknown step type).
    auto unknown = broken([](nlohmann::json&) {});
    ASSERT_TRUE(unknown.ok());
    nlohmann::json older = json;
    for (auto& b : older["bodies"])
        for (auto& f : b["features"])
            if (f["type"] == "Loft")
                f["type"] = "Loft2";
    auto refused = io::documentFromJson(older);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::FileVersionUnsupported);
}
