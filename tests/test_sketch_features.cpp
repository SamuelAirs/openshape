// Sketch -> profile -> extrude, through the document and command layers.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "geometry/Exchange.h"
#include "io/ProjectFile.h"

#include <filesystem>

using namespace os;
using namespace os::test;

namespace {

struct Fixture {
    doc::Document document;
    cmd::UndoStack stack;

    // Creates a sketch through commands and returns its id.
    Uuid createSketch(sketch::Plane plane = sketch::Plane::xy(), std::optional<Uuid> host = std::nullopt)
    {
        sketch::Sketch s(Uuid::generate(), plane);
        s.setName(document.nextSketchName());
        s.setHostBody(host);
        auto command = std::make_unique<cmd::CreateSketchCommand>(s);
        const Uuid id = command->sketchId();
        EXPECT_TRUE(stack.push(std::move(command), document).ok());
        return id;
    }

    // Applies an edit to a copy of the sketch and commits it as one command.
    template <typename Fn>
    void editSketch(const Uuid& id, Fn&& fn)
    {
        sketch::Sketch copy = *document.sketch(id);
        fn(copy);
        EXPECT_TRUE(sketch::solve(copy).ok);
        EXPECT_TRUE(stack.push(std::make_unique<cmd::EditSketchCommand>(copy, "Edit sketch"), document).ok());
    }

    std::vector<doc::ProfileRef> profilesContaining(const Uuid& sketchId, std::vector<Vec2> points)
    {
        const sketch::Sketch& s = *document.sketch(sketchId);
        auto regions = doc::sketchRegions(s);
        EXPECT_TRUE(regions.ok());
        std::vector<doc::ProfileRef> refs;
        for (Vec2 p : points)
            for (const auto& r : regions.value())
                if (geom::regionContains(r.face, s.plane().toWorld(p)))
                    refs.push_back(doc::makeProfileRef(r, s));
        EXPECT_EQ(refs.size(), points.size());
        return refs;
    }

    Uuid extrudeNewBody(const Uuid& sketchId, std::vector<doc::ProfileRef> profiles, double distance)
    {
        auto f = std::make_unique<doc::ExtrudeFeature>();
        f->sketchId = sketchId;
        f->profiles = std::move(profiles);
        f->distance = distance;
        f->mode = doc::ExtrudeMode::NewBody;
        auto command = std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), std::move(f));
        const Uuid id = command->bodyId();
        const Status s = stack.push(std::move(command), document);
        EXPECT_TRUE(s.ok()) << s.developerMessage();
        return id;
    }

    Status extrudeInto(const Uuid& bodyId, const Uuid& sketchId, std::vector<doc::ProfileRef> profiles, double distance,
                       doc::ExtrudeMode mode)
    {
        auto f = std::make_unique<doc::ExtrudeFeature>();
        f->sketchId = sketchId;
        f->profiles = std::move(profiles);
        f->distance = distance;
        f->mode = mode;
        return stack.push(std::make_unique<cmd::AddFeatureCommand>(bodyId, std::move(f)), document);
    }
};

} // namespace

TEST(SketchFeatures, RectangleExtrudesToExactBlock)
{
    // MVP: rectangle 60 x 40 dimensioned, extruded 20 mm, 4 vertical edges filleted 3 mm.
    Fixture f;
    const Uuid sk = f.createSketch();
    f.editSketch(sk, [](sketch::Sketch& s) {
        const auto r = sketch::addRectangle(s, {0, 0}, {50, 30}, sketch::kOriginId);
        s.addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60});
        s.addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40});
    });
    EXPECT_EQ(f.document.sketch(sk)->solveReport().degreesOfFreedom, 0);

    const Uuid body = f.extrudeNewBody(sk, f.profilesContaining(sk, {{30, 20}}), 20);
    doc::Body* b = f.document.body(body);
    ASSERT_NE(b, nullptr);
    const auto bb = geom::boundingBox(b->shape());
    EXPECT_NEAR(bb.size().x, 60, 1e-6);
    EXPECT_NEAR(bb.size().y, 40, 1e-6);
    EXPECT_NEAR(bb.size().z, 20, 1e-6);
    EXPECT_NEAR(geom::volume(b->shape()), 48000, 1e-4);

    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::AddFeatureCommand>(body, filletVertical(b->shape(), 3.0)), f.document).ok());
    EXPECT_NEAR(geom::volume(b->shape()), 48000 - 4 * (9 - kPi * 9 / 4) * 20, 1e-3);
}

TEST(SketchFeatures, EditingSketchDimensionUpdatesSolid)
{
    Fixture f;
    const Uuid sk = f.createSketch();
    sketch::EntityId width = 0;
    f.editSketch(sk, [&](sketch::Sketch& s) {
        const auto r = sketch::addRectangle(s, {0, 0}, {60, 40}, sketch::kOriginId);
        width = s.addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60});
        s.addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 40});
    });
    const Uuid body = f.extrudeNewBody(sk, f.profilesContaining(sk, {{30, 20}}), 20);
    f.editSketch(sk, [&](sketch::Sketch& s) { s.constraint(width)->value = 80; });
    EXPECT_NEAR(geom::boundingBox(f.document.body(body)->shape()).size().x, 80, 1e-6);
    f.stack.undo(f.document);
    EXPECT_NEAR(geom::boundingBox(f.document.body(body)->shape()).size().x, 60, 1e-6);
}

TEST(SketchFeatures, DeletingUsedSketchIsRefused)
{
    Fixture f;
    const Uuid sk = f.createSketch();
    f.editSketch(sk, [](sketch::Sketch& s) { sketch::addRectangle(s, {0, 0}, {10, 10}); });
    f.extrudeNewBody(sk, f.profilesContaining(sk, {{5, 5}}), 5);
    const Status s = f.stack.push(std::make_unique<cmd::DeleteSketchCommand>(sk), f.document);
    EXPECT_FALSE(s.ok());
    EXPECT_NE(f.document.sketch(sk), nullptr);
}

TEST(SketchFeatures, UnusedSketchDeleteUndo)
{
    Fixture f;
    const Uuid sk = f.createSketch();
    ASSERT_TRUE(f.stack.push(std::make_unique<cmd::DeleteSketchCommand>(sk), f.document).ok());
    EXPECT_EQ(f.document.sketch(sk), nullptr);
    f.stack.undo(f.document);
    EXPECT_NE(f.document.sketch(sk), nullptr);
}

TEST(SketchFeatures, OpenSketchCannotBeExtruded)
{
    Fixture f;
    const Uuid sk = f.createSketch();
    f.editSketch(sk, [](sketch::Sketch& s) {
        const auto a = s.addPoint({0, 0});
        const auto b = s.addPoint({10, 0});
        s.addLine(a, b);
    });
    auto regions = doc::sketchRegions(*f.document.sketch(sk));
    ASSERT_TRUE(regions.ok());
    EXPECT_TRUE(regions.value().empty());
    auto feature = std::make_unique<doc::ExtrudeFeature>();
    feature->sketchId = sk;
    feature->profiles = {{{5, 5}, 100}};
    feature->distance = 5;
    const Status s = f.stack.push(std::make_unique<cmd::CreateBodyCommand>("Body", std::move(feature)), f.document);
    EXPECT_FALSE(s.ok());
    EXPECT_FALSE(s.userMessage().empty());
    EXPECT_TRUE(f.document.bodies().empty());
}

// Milestone 1 acceptance model: 60 x 30 plate, 5 mm thick, with two 6 mm
// holes cut through it (the second one duplicated at the other end).
TEST(SketchFeatures, Milestone1BracketModel)
{
    Fixture f;
    const Uuid base = f.createSketch();
    f.editSketch(base, [](sketch::Sketch& s) {
        const auto r = sketch::addRectangle(s, {0, 0}, {50, 25}, sketch::kOriginId);
        s.addConstraint({sketch::ConstraintKind::HorizontalDistance, r.corners[0], r.corners[1], 60});
        s.addConstraint({sketch::ConstraintKind::VerticalDistance, r.corners[1], r.corners[2], 30});
    });
    const Uuid plate = f.extrudeNewBody(base, f.profilesContaining(base, {{30, 15}}), 5);
    EXPECT_NEAR(geom::volume(f.document.body(plate)->shape()), 9000, 1e-4);

    // Hole sketch on the plate's top face.
    sketch::Plane top = sketch::Plane::xy();
    top.origin = {0, 0, 5};
    const Uuid holes = f.createSketch(top, plate);
    f.editSketch(holes, [](sketch::Sketch& s) {
        for (double x : {10.0, 50.0}) {
            const auto c = s.addPoint({x, 15});
            const auto circle = s.addCircle(c, 2);
            s.addConstraint({sketch::ConstraintKind::Diameter, circle, sketch::kNoEntity, 6});
            s.addConstraint({sketch::ConstraintKind::HorizontalDistance, sketch::kOriginId, c, x});
            s.addConstraint({sketch::ConstraintKind::VerticalDistance, sketch::kOriginId, c, 15});
        }
    });
    EXPECT_EQ(f.document.sketch(holes)->solveReport().degreesOfFreedom, 0);
    const Status cut = f.extrudeInto(plate, holes, f.profilesContaining(holes, {{10, 15}, {50, 15}}), -5,
                                     doc::ExtrudeMode::Cut);
    ASSERT_TRUE(cut.ok()) << cut.developerMessage();
    const double expected = 9000 - 2 * kPi * 9 * 5;
    EXPECT_NEAR(geom::volume(f.document.body(plate)->shape()), expected, 1e-3);
    EXPECT_TRUE(geom::isValid(f.document.body(plate)->shape()));

    // Save, reopen, still identical and editable; export STEP + STL.
    const auto dir = std::filesystem::temp_directory_path();
    const auto project = dir / "openshape_bracket.openshape";
    ASSERT_TRUE(io::saveProject(f.document, project).ok());
    auto loaded = io::loadProject(project);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    doc::Document& d = *loaded.value();
    ASSERT_EQ(d.sketches().size(), 2u);
    ASSERT_NE(d.body(plate), nullptr);
    EXPECT_FALSE(d.body(plate)->hasFailures());
    EXPECT_NEAR(geom::volume(d.body(plate)->shape()), expected, 1e-3);

    ASSERT_TRUE(geom::exportStep({{"Bracket", d.body(plate)->shape()}}, dir / "openshape_bracket.step").ok());
    ASSERT_TRUE(geom::exportStl({{"Bracket", d.body(plate)->shape()}}, dir / "openshape_bracket.stl").ok());
    auto reimported = geom::importStep(dir / "openshape_bracket.step");
    ASSERT_TRUE(reimported.ok());
    EXPECT_NEAR(geom::volume(reimported.value().front().shape), expected, 1e-2);
}

TEST(SketchFeatures, JoinExtrudeAddsMaterial)
{
    Fixture f;
    const Uuid base = f.createSketch();
    f.editSketch(base, [](sketch::Sketch& s) { sketch::addRectangle(s, {0, 0}, {20, 20}, sketch::kOriginId); });
    const Uuid body = f.extrudeNewBody(base, f.profilesContaining(base, {{10, 10}}), 10);
    sketch::Plane top = sketch::Plane::xy();
    top.origin = {0, 0, 10};
    const Uuid boss = f.createSketch(top, body);
    f.editSketch(boss, [](sketch::Sketch& s) { s.addCircle(s.addPoint({10, 10}), 4); });
    ASSERT_TRUE(f.extrudeInto(body, boss, f.profilesContaining(boss, {{10, 10}}), 6, doc::ExtrudeMode::Join).ok());
    EXPECT_NEAR(geom::volume(f.document.body(body)->shape()), 4000 + kPi * 16 * 6, 1e-3);
    EXPECT_NEAR(geom::boundingBox(f.document.body(body)->shape()).size().z, 16, 1e-6);
}
