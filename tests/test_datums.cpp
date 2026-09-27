// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Construction axes and planes (datums): where each kind is, following the
// geometry they were made from, failing plainly when it is gone, undo/redo,
// save/open, and a sketch on a construction plane following it.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

using namespace os;

namespace {

void expectVec(const Vec3& actual, const Vec3& expected, const char* what)
{
    EXPECT_NEAR(actual.x, expected.x, 1e-9) << what;
    EXPECT_NEAR(actual.y, expected.y, 1e-9) << what;
    EXPECT_NEAR(actual.z, expected.z, 1e-9) << what;
}

// Parallel (either way round).
void expectDirection(const Vec3& actual, const Vec3& expected, const char* what)
{
    EXPECT_NEAR(std::abs(actual.normalized().dot(expected.normalized())), 1.0, 1e-12) << what;
}

// A 20 mm cube from the origin (0..20 on every axis), then a push/pull of its
// top face by `push` (so its top is at 20 + push).
struct Cube {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid body, box, push;

    explicit Cube(double pushBy = 5)
    {
        auto boxStep = test::boxFeature(20, 20, 20);
        box = boxStep->id();
        auto create = std::make_unique<cmd::CreateBodyCommand>("Cube", std::move(boxStep));
        body = create->bodyId();
        EXPECT_TRUE(stack.push(std::move(create), document).ok());
        auto pushStep = test::pushPull(shape(), {0, 0, 1}, pushBy);
        push = pushStep->id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(pushStep)), document).ok());
    }
    const geom::Shape& shape() const { return document.body(body)->shape(); }
    int face(const Vec3& normal) const { return test::faceWithNormal(shape(), normal); }
    // The edge whose middle is at `mid`.
    int edgeAt(const Vec3& mid) const
    {
        for (int i = 0; i < shape().edgeCount(); ++i)
            if ((geom::edgeInfo(shape(), i)->midpoint - mid).length() < 1e-6)
                return i;
        return -1;
    }
    doc::GeometryRef ref(doc::GeometryRef::Kind kind, int index, const Vec3& near = {}) const
    {
        const auto r = doc::makeGeometryRef(*document.body(body), kind, index, near);
        EXPECT_TRUE(r.has_value());
        return r.value_or(doc::GeometryRef{});
    }
    // Adds the datum (undoably) and returns it as the document holds it.
    const doc::Datum& add(doc::Datum datum)
    {
        const Uuid id = datum.id();
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddDatumCommand>(std::move(datum)), document).ok());
        return *document.datum(id);
    }
};

} // namespace

TEST(Datums, PlanesAreWhereTheirMethodsPutThem)
{
    Cube cube; // top at z = 25
    doc::Datum offset;
    offset.method = doc::DatumMethod::PlaneOffset;
    offset.refs = {cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1}))};
    offset.distance = 10;
    const auto& above = cube.add(offset);
    EXPECT_FALSE(above.failed()) << above.error();
    expectVec(above.geometry().origin, {10, 10, 35}, "offset plane: 10 above the top face's middle");
    expectVec(above.geometry().direction, {0, 0, 1}, "facing up, as the face does");
    EXPECT_GT(above.geometry().size, 0.0);

    doc::Datum fromOrigin;
    fromOrigin.method = doc::DatumMethod::PlaneOffset;
    fromOrigin.originIndex = 1; // XZ
    fromOrigin.distance = -5;
    const auto& xz = cube.add(fromOrigin);
    expectVec(xz.geometry().origin, {0, -5, 0}, "XZ offset by -5");
    expectVec(xz.geometry().direction, {0, 1, 0}, "normal Y");

    // Through the top face's front edge (y = 0, z = 25, along X).
    const int frontTop = cube.edgeAt({10, 0, 25});
    ASSERT_GE(frontTop, 0);
    doc::Datum angled;
    angled.method = doc::DatumMethod::PlaneAngle;
    angled.refs = {cube.ref(doc::GeometryRef::Kind::Edge, frontTop), cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1}))};
    angled.angle = 0;
    const auto& flat = cube.add(angled);
    EXPECT_FALSE(flat.failed()) << flat.error();
    expectVec(flat.geometry().direction, {0, 0, 1}, "at 0 degrees: the face's own plane");
    expectVec(flat.geometry().origin, {10, 0, 25}, "through the edge's middle");
    angled = flat;
    angled.angle = kPi / 4; // leaning back over the top face, like a roof
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::EditDatumCommand>(angled, "Change angle"), cube.document).ok());
    const doc::Datum& roof = *cube.document.datum(angled.id());
    const double s = std::sqrt(0.5);
    expectVec(roof.geometry().direction, {0, -s, s}, "at 45 degrees");
    // The plane still holds the edge: both of its ends lie on it.
    for (const Vec3& end : {Vec3{0, 0, 25}, Vec3{20, 0, 25}})
        EXPECT_NEAR((end - roof.geometry().origin).dot(roof.geometry().direction), 0.0, 1e-9);
    angled.angle = kPi / 2;
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::EditDatumCommand>(angled, "Change angle"), cube.document).ok());
    expectDirection(cube.document.datum(angled.id())->geometry().direction, {0, 1, 0}, "at 90 degrees: the front face's plane");

    doc::Datum midway;
    midway.method = doc::DatumMethod::PlaneMidway;
    midway.refs = {cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1})),
                   cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, -1}))};
    const auto& middle = cube.add(midway);
    EXPECT_FALSE(middle.failed()) << middle.error();
    expectVec(middle.geometry().origin, {10, 10, 12.5}, "midway between z = 0 and z = 25");
    expectDirection(middle.geometry().direction, {0, 0, 1}, "parallel to them");

    // Two faces that are not parallel: refused with a plain message.
    midway.refs[1] = cube.ref(doc::GeometryRef::Kind::Face, cube.face({1, 0, 0}));
    auto crooked = doc::resolveDatum(midway, cube.document.context());
    ASSERT_FALSE(crooked.ok());
    EXPECT_EQ(crooked.userMessage(), "The two faces are no longer parallel.");
}

TEST(Datums, AxesAreWhereTheirMethodsPutThem)
{
    Cube cube; // 20 x 20 x 25
    doc::Datum along;
    along.method = doc::DatumMethod::AxisAlongEdge;
    const int vertical = cube.edgeAt({20, 0, 12.5});
    ASSERT_GE(vertical, 0);
    along.refs = {cube.ref(doc::GeometryRef::Kind::Edge, vertical)};
    const auto& edgeAxis = cube.add(along);
    EXPECT_FALSE(edgeAxis.failed()) << edgeAxis.error();
    expectVec(edgeAxis.geometry().origin, {20, 0, 12.5}, "along the edge, at its middle");
    expectDirection(edgeAxis.geometry().direction, {0, 0, 1}, "vertical");

    // Two corners of the top face, diagonally.
    const int front = cube.edgeAt({10, 0, 25});
    const int back = cube.edgeAt({10, 20, 25});
    doc::Datum diagonal;
    diagonal.method = doc::DatumMethod::AxisTwoPoints;
    diagonal.refs = {cube.ref(doc::GeometryRef::Kind::Vertex, front, {0, 0, 25}),
                     cube.ref(doc::GeometryRef::Kind::Vertex, back, {20, 20, 25})};
    const auto& diag = cube.add(diagonal);
    EXPECT_FALSE(diag.failed()) << diag.error();
    expectVec(diag.geometry().origin, {10, 10, 25}, "between the corners");
    expectDirection(diag.geometry().direction, {1, 1, 0}, "diagonal");

    doc::Datum parallel;
    parallel.method = doc::DatumMethod::AxisParallel;
    parallel.originIndex = 0;
    parallel.refs = {cube.ref(doc::GeometryRef::Kind::Vertex, back, {20, 20, 25})};
    const auto& px = cube.add(parallel);
    expectVec(px.geometry().origin, {20, 20, 25}, "through the corner");
    expectVec(px.geometry().direction, {1, 0, 0}, "parallel to X");

    // Through a hole: a plate with a hole of radius 3 at (20, 20).
    doc::Document doc2;
    cmd::UndoStack stack2;
    auto plate = std::make_unique<doc::ImportedFeature>();
    plate->setShape(geom::booleanOp(geom::makeBox({10, 10, 0}, {20, 20, 5}).value(),
                                    geom::makeCylinder({20, 20, -1}, {0, 0, 1}, 3.0, 7.0).value(), geom::BooleanKind::Subtract)
                        .value());
    auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(plate));
    const Uuid plateId = create->bodyId();
    ASSERT_TRUE(stack2.push(std::move(create), doc2).ok());
    const geom::Shape& shape = doc2.body(plateId)->shape();
    int wall = -1, rim = -1;
    for (int i = 0; i < shape.faceCount(); ++i)
        if (geom::faceInfo(shape, i)->kind == geom::SurfaceKind::Cylinder)
            wall = i;
    for (int i = 0; i < shape.edgeCount(); ++i)
        if (geom::edgeInfo(shape, i)->kind == geom::CurveKind::Circle && geom::edgeInfo(shape, i)->center.z > 4)
            rim = i;
    ASSERT_GE(wall, 0);
    ASSERT_GE(rim, 0);
    for (const auto& [kind, index] : {std::pair{doc::GeometryRef::Kind::Face, wall}, std::pair{doc::GeometryRef::Kind::Edge, rim}}) {
        doc::Datum through;
        through.method = doc::DatumMethod::AxisThrough;
        through.refs = {*doc::makeGeometryRef(*doc2.body(plateId), kind, index)};
        auto g = doc::resolveDatum(through, doc2.context());
        ASSERT_TRUE(g.ok()) << g.userMessage();
        EXPECT_NEAR(g.value().origin.x, 20.0, 1e-9);
        EXPECT_NEAR(g.value().origin.y, 20.0, 1e-9);
        expectDirection(g.value().direction, {0, 0, 1}, "the hole's axis");
    }
    // A circle's center as a point; a straight edge has none.
    EXPECT_TRUE(doc::makeGeometryRef(*doc2.body(plateId), doc::GeometryRef::Kind::Center, rim).has_value());
    EXPECT_FALSE(doc::makeGeometryRef(*cube.document.body(cube.body), doc::GeometryRef::Kind::Center, front).has_value());
}

// Datums follow the step they were made from: an upstream edit moves them
// (and a sketch placed on a plane, and what was extruded from it); when the
// step is gone they fail with a message and keep their place.
TEST(Datums, FollowUpstreamEditsAndFailPlainly)
{
    Cube cube(5); // top at 25
    doc::Datum offset;
    offset.method = doc::DatumMethod::PlaneOffset;
    offset.refs = {cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1}))};
    offset.distance = 10;
    const Uuid planeId = offset.id();
    cube.add(offset);
    EXPECT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 35.0, 1e-9);

    // A sketch on the plane, extruded into a new body 4 mm up.
    const doc::DatumGeometry g = cube.document.datum(planeId)->geometry();
    sketch::Sketch sk(Uuid::generate(), doc::sketchPlaneOn(g));
    sk.setDatumPlane(planeId);
    sketch::addRectangle(sk, {2, 2}, {8, 6});
    const Uuid sketchId = sk.id();
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::CreateSketchCommand>(sk), cube.document).ok());
    const sketch::Sketch& onPlane = *cube.document.sketch(sketchId);
    EXPECT_NEAR(onPlane.plane().origin.z, 35.0, 1e-9);
    auto extrude = std::make_unique<doc::ExtrudeFeature>();
    extrude->sketchId = sketchId;
    extrude->profiles = {doc::makeProfileRef(doc::sketchRegions(onPlane).value().front(), onPlane)};
    extrude->distance = 4;
    auto createBlock = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(extrude));
    const Uuid block = createBlock->bodyId();
    ASSERT_TRUE(cube.stack.push(std::move(createBlock), cube.document).ok());
    auto blockBox = [&] { return geom::boundingBox(cube.document.body(block)->shape()); };
    EXPECT_NEAR(blockBox().min.z, 35.0, 1e-6);
    EXPECT_NEAR(blockBox().max.z, 39.0, 1e-6);
    EXPECT_NEAR(geom::volume(cube.document.body(block)->shape()), 6.0 * 4.0 * 4.0, 1e-6);

    // Push the top up by 15 instead of 5: the plane, the sketch and the block follow.
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::SetParameterCommand>(cube.push, "distance", 15.0), cube.document).ok());
    EXPECT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 45.0, 1e-9);
    EXPECT_NEAR(cube.document.sketch(sketchId)->plane().origin.z, 45.0, 1e-9);
    EXPECT_NEAR(blockBox().min.z, 45.0, 1e-6);
    // The plane's own distance, edited as in the Model panel.
    doc::Datum edited = *cube.document.datum(planeId);
    ASSERT_TRUE(edited.setParameter("distance", 2.0).ok());
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::EditDatumCommand>(edited, "Change distance"), cube.document).ok());
    EXPECT_NEAR(blockBox().min.z, 37.0, 1e-6);
    ASSERT_TRUE(cube.stack.undo(cube.document));
    EXPECT_NEAR(blockBox().min.z, 45.0, 1e-6);
    ASSERT_TRUE(cube.stack.undo(cube.document));
    EXPECT_NEAR(blockBox().min.z, 35.0, 1e-6);

    // Delete the step the plane was made from: it fails and stays at z = 35.
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::DeleteFeatureCommand>(cube.push), cube.document).ok());
    const doc::Datum& orphan = *cube.document.datum(planeId);
    EXPECT_TRUE(orphan.failed());
    EXPECT_EQ(orphan.error(), "The step it was made from is gone.");
    EXPECT_NEAR(orphan.geometry().origin.z, 35.0, 1e-9);
    EXPECT_NEAR(blockBox().min.z, 35.0, 1e-6); // the sketch stays where it was
    ASSERT_TRUE(cube.stack.undo(cube.document));
    EXPECT_FALSE(cube.document.datum(planeId)->failed());

    // A datum made from a later step's face does not move when an earlier
    // one changes shape elsewhere; the box's size moves it.
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::SetParameterCommand>(cube.box, "height", 30.0), cube.document).ok());
    EXPECT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 45.0, 1e-9); // 30 + 5 + 10
}

// An independent copy (a separate Pattern or Mirror copy, Duplicate, a split
// piece) of a body built on a construction plane made from that body takes a
// hidden copy of the plane, made from the copy: editing the source never
// moves the copy (and editing the copy moves its own plane). A plane made
// from other bodies or an origin plane stays shared, as a face of another
// body does.
TEST(Datums, IndependentCopiesTakeTheirOwnPlane)
{
    Cube cube(5); // top at 25
    doc::Datum offset;
    offset.method = doc::DatumMethod::PlaneOffset;
    offset.refs = {cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1}))};
    offset.distance = 10;
    offset.setName("Plane 1");
    const Uuid planeId = offset.id();
    cube.add(offset);
    ASSERT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 35.0, 1e-9);

    // A 6 x 4 rectangle on the plane, extruded 10 down onto the top and joined.
    sketch::Sketch sk(Uuid::generate(), doc::sketchPlaneOn(cube.document.datum(planeId)->geometry()));
    sk.setDatumPlane(planeId);
    sketch::addRectangle(sk, {2, 2}, {8, 6});
    const Uuid sketchId = sk.id();
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::CreateSketchCommand>(sk), cube.document).ok());
    const sketch::Sketch& onPlane = *cube.document.sketch(sketchId);
    auto boss = std::make_unique<doc::ExtrudeFeature>();
    boss->sketchId = sketchId;
    boss->profiles = {doc::makeProfileRef(doc::sketchRegions(onPlane).value().front(), onPlane)};
    boss->distance = -10;
    boss->mode = doc::ExtrudeMode::Join;
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::AddFeatureCommand>(cube.body, std::move(boss)), cube.document).ok());
    const double volume = 20.0 * 20.0 * 25.0 + 6.0 * 4.0 * 10.0;
    ASSERT_NEAR(geom::volume(cube.shape()), volume, 1e-6);
    ASSERT_NEAR(geom::boundingBox(cube.shape()).max.z, 35.0, 1e-6);

    // A separate pattern copy 40 mm along X.
    std::vector<std::unique_ptr<doc::Feature>> moveStep;
    auto move = std::make_unique<doc::MoveFeature>();
    move->translation = {40, 0, 0};
    moveStep.push_back(std::move(move));
    ASSERT_TRUE(
        cube.stack.push(cmd::makeCopyBodiesCommand(cube.body, std::move(moveStep), {"Cube 2"}, "Pattern"), cube.document).ok());
    ASSERT_EQ(cube.document.bodies().size(), 2u);
    const Uuid copyId = cube.document.bodies()[1]->id();
    auto copyShape = [&]() -> const geom::Shape& { return cube.document.body(copyId)->shape(); };
    EXPECT_NEAR(geom::volume(copyShape()), volume, 1e-6);
    EXPECT_NEAR(geom::boundingBox(copyShape()).min.x, 40.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(copyShape()).max.z, 35.0, 1e-6);
    // Its hidden sketch is on a hidden copy of the plane, made from the copy.
    ASSERT_EQ(cube.document.datums().size(), 2u);
    const doc::Datum& planeCopy = *cube.document.datums()[1];
    EXPECT_EQ(planeCopy.name(), "Plane 1 copy");
    EXPECT_FALSE(planeCopy.isVisible());
    EXPECT_FALSE(planeCopy.failed()) << planeCopy.error();
    ASSERT_EQ(planeCopy.refs.size(), 1u);
    EXPECT_EQ(planeCopy.refs[0].body, copyId);
    EXPECT_EQ(planeCopy.refs[0].feature, cube.document.body(copyId)->features()[1]->id()) << "the copy's own push step";
    EXPECT_NEAR(planeCopy.geometry().origin.z, 35.0, 1e-9) << "where the source's is: the copy moves after its history";
    const Uuid planeCopyId = planeCopy.id();
    const auto sketchesOnCopy = cube.document.sketchesOn(planeCopyId);
    ASSERT_EQ(sketchesOnCopy.size(), 1u);
    EXPECT_NE(sketchesOnCopy[0], sketchId);
    EXPECT_EQ(cube.document.sketchesOn(planeId), std::vector<Uuid>{sketchId});

    // Taller source box: the source's plane and boss go up 10; the copy does not change.
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::SetParameterCommand>(cube.box, "height", 30.0), cube.document).ok());
    EXPECT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 45.0, 1e-9);
    EXPECT_NEAR(geom::boundingBox(cube.shape()).max.z, 45.0, 1e-6);
    EXPECT_NEAR(geom::volume(copyShape()), volume, 1e-6);
    EXPECT_NEAR(geom::boundingBox(copyShape()).max.z, 35.0, 1e-6) << "editing the source never moves the copy";
    EXPECT_NEAR(cube.document.datum(planeCopyId)->geometry().origin.z, 35.0, 1e-9);
    ASSERT_TRUE(cube.stack.undo(cube.document));

    // Taller copy box: its own plane and boss go up; the source stays.
    const Uuid copyBox = cube.document.body(copyId)->features()[0]->id();
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::SetParameterCommand>(copyBox, "height", 30.0), cube.document).ok());
    EXPECT_NEAR(cube.document.datum(planeCopyId)->geometry().origin.z, 45.0, 1e-9);
    EXPECT_NEAR(geom::boundingBox(copyShape()).max.z, 45.0, 1e-6);
    EXPECT_NEAR(geom::volume(copyShape()), volume + 20.0 * 20.0 * 10.0, 1e-6);
    EXPECT_NEAR(geom::boundingBox(cube.shape()).max.z, 35.0, 1e-6);
    EXPECT_NEAR(cube.document.datum(planeId)->geometry().origin.z, 35.0, 1e-9);

    // Save and open: the hidden plane and the copy's sketch on it come back.
    auto reopened = io::documentFromJson(io::documentToJson(cube.document));
    ASSERT_TRUE(reopened.ok()) << reopened.developerMessage();
    ASSERT_NE(reopened.value()->datum(planeCopyId), nullptr);
    EXPECT_FALSE(reopened.value()->datum(planeCopyId)->isVisible());
    EXPECT_EQ(reopened.value()->sketchesOn(planeCopyId), sketchesOnCopy);
    EXPECT_NEAR(geom::boundingBox(reopened.value()->body(copyId)->shape()).max.z, 45.0, 1e-6);
    ASSERT_TRUE(cube.stack.undo(cube.document));

    // Undo the pattern: the hidden plane goes with the copy; redo brings both back.
    ASSERT_TRUE(cube.stack.undo(cube.document));
    EXPECT_EQ(cube.document.datums().size(), 1u);
    EXPECT_EQ(cube.document.bodies().size(), 1u);
    ASSERT_TRUE(cube.stack.redo(cube.document).ok());
    ASSERT_NE(cube.document.datum(planeCopyId), nullptr);
    EXPECT_FALSE(cube.document.datum(planeCopyId)->failed());
    EXPECT_NEAR(geom::boundingBox(copyShape()).max.z, 35.0, 1e-6);

    // A plane from an origin plane is shared by a copy (nothing of it is copied).
    doc::Datum high;
    high.method = doc::DatumMethod::PlaneOffset;
    high.originIndex = 2;
    high.distance = 50;
    const Uuid highId = high.id();
    cube.add(high);
    sketch::Sketch top(Uuid::generate(), doc::sketchPlaneOn(cube.document.datum(highId)->geometry()));
    top.setDatumPlane(highId);
    sketch::addRectangle(top, {0, 0}, {5, 5});
    const Uuid topId = top.id();
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::CreateSketchCommand>(top), cube.document).ok());
    auto tile = std::make_unique<doc::ExtrudeFeature>();
    tile->sketchId = topId;
    tile->profiles = {doc::makeProfileRef(doc::sketchRegions(*cube.document.sketch(topId)).value().front(),
                                          *cube.document.sketch(topId))};
    tile->distance = 2;
    auto createTile = std::make_unique<cmd::CreateBodyCommand>("Tile", std::move(tile));
    const Uuid tileId = createTile->bodyId();
    ASSERT_TRUE(cube.stack.push(std::move(createTile), cube.document).ok());
    auto duplicate = std::make_unique<cmd::DuplicateBodyCommand>(tileId);
    const Uuid tileCopy = duplicate->copyId();
    ASSERT_TRUE(cube.stack.push(std::move(duplicate), cube.document).ok());
    EXPECT_EQ(cube.document.datums().size(), 3u) << "no copy of the origin-based plane";
    EXPECT_EQ(cube.document.sketchesOn(highId).size(), 2u);
    EXPECT_NEAR(geom::boundingBox(cube.document.body(tileCopy)->shape()).min.z, 50.0, 1e-6);
}

TEST(Datums, ValuesAFileCannotHoldAreRefused)
{
    Cube cube;
    doc::Datum far;
    far.method = doc::DatumMethod::PlaneOffset;
    far.originIndex = 2;
    far.distance = 2e6;
    const Uuid farId = far.id();
    const Status added = cube.stack.push(std::make_unique<cmd::AddDatumCommand>(far), cube.document);
    ASSERT_FALSE(added.ok());
    EXPECT_EQ(added.userMessage(), "The distance is too large.");
    EXPECT_EQ(cube.document.datum(farId), nullptr);
    far.distance = 1e6; // the limit itself is fine
    const doc::Datum& atLimit = cube.add(far);
    EXPECT_NEAR(atLimit.geometry().origin.z, 1e6, 1e-6);
    doc::Datum edited = atLimit;
    edited.distance = -1e6 - 1;
    const Status edit = cube.stack.push(std::make_unique<cmd::EditDatumCommand>(edited, "Change distance"), cube.document);
    ASSERT_FALSE(edit.ok());
    EXPECT_EQ(edit.userMessage(), "The distance is too large.");
    EXPECT_NEAR(cube.document.datum(farId)->distance, 1e6, 1e-9);
    EXPECT_EQ(edited.setParameter("distance", 3e6).userMessage(), "The distance is too large.");

    doc::Datum angled;
    angled.method = doc::DatumMethod::PlaneAngle;
    angled.refs = {cube.ref(doc::GeometryRef::Kind::Edge, cube.edgeAt({10, 0, 25})),
                   cube.ref(doc::GeometryRef::Kind::Face, cube.face({0, 0, 1}))};
    angled.angle = kPi * 1.5;
    const Status tooSteep = cube.stack.push(std::make_unique<cmd::AddDatumCommand>(angled), cube.document);
    ASSERT_FALSE(tooSteep.ok());
    EXPECT_EQ(tooSteep.userMessage(), "The angle must be between -180° and 180°.");
    EXPECT_EQ(angled.setParameter("angle", -kPi * 1.01).userMessage(), "The angle must be between -180° and 180°.");
    // Everything the document holds can be saved and opened again.
    EXPECT_TRUE(io::documentFromJson(io::documentToJson(cube.document)).ok());
}

TEST(Datums, UndoRedoAddHideDelete)
{
    Cube cube;
    doc::Datum plane;
    plane.method = doc::DatumMethod::PlaneOffset;
    plane.originIndex = 2;
    plane.distance = 7;
    plane.setName("Plane 1");
    const Uuid id = plane.id();
    cube.add(plane);
    ASSERT_NE(cube.document.datum(id), nullptr);
    EXPECT_EQ(cube.document.nextDatumName(doc::DatumKind::Plane), "Plane 2");
    EXPECT_EQ(cube.document.nextDatumName(doc::DatumKind::Axis), "Axis 1");
    doc::Datum hidden = *cube.document.datum(id);
    hidden.setVisible(false);
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::EditDatumCommand>(hidden, "Hide"), cube.document).ok());
    EXPECT_FALSE(cube.document.datum(id)->isVisible());
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::DeleteDatumCommand>(id), cube.document).ok());
    EXPECT_EQ(cube.document.datum(id), nullptr);
    ASSERT_TRUE(cube.stack.undo(cube.document)); // delete
    ASSERT_NE(cube.document.datum(id), nullptr);
    EXPECT_FALSE(cube.document.datum(id)->isVisible());
    ASSERT_TRUE(cube.stack.undo(cube.document)); // hide
    EXPECT_TRUE(cube.document.datum(id)->isVisible());
    ASSERT_TRUE(cube.stack.undo(cube.document)); // add
    EXPECT_EQ(cube.document.datum(id), nullptr);
    ASSERT_TRUE(cube.stack.redo(cube.document).ok());
    ASSERT_NE(cube.document.datum(id), nullptr);
    expectVec(cube.document.datum(id)->geometry().origin, {0, 0, 7}, "redo brings it back where it was");
}

TEST(Datums, SaveAndOpenKeepThemAndTheirSketches)
{
    Cube cube;
    doc::Datum offset;
    offset.method = doc::DatumMethod::PlaneOffset;
    offset.refs = {cube.ref(doc::GeometryRef::Kind::Face, cube.face({1, 0, 0}))};
    offset.distance = 6;
    offset.setName("Side plane");
    const Uuid planeId = offset.id();
    cube.add(offset);
    doc::Datum corners;
    corners.method = doc::DatumMethod::AxisTwoPoints;
    corners.refs = {cube.ref(doc::GeometryRef::Kind::Vertex, cube.edgeAt({10, 0, 25}), {0, 0, 25}),
                    cube.ref(doc::GeometryRef::Kind::Vertex, cube.edgeAt({10, 20, 25}), {20, 20, 25})};
    corners.setVisible(false);
    const Uuid axisId = corners.id();
    cube.add(corners);
    sketch::Sketch sk(Uuid::generate(), doc::sketchPlaneOn(cube.document.datum(planeId)->geometry()));
    sk.setDatumPlane(planeId);
    sketch::addRectangle(sk, {1, 1}, {4, 3});
    const Uuid sketchId = sk.id();
    ASSERT_TRUE(cube.stack.push(std::make_unique<cmd::CreateSketchCommand>(sk), cube.document).ok());

    const nlohmann::json saved = io::documentToJson(cube.document);
    ASSERT_TRUE(saved.contains("datums"));
    EXPECT_EQ(saved["datums"].size(), 2u);
    auto loaded = io::documentFromJson(saved);
    ASSERT_TRUE(loaded.ok()) << loaded.developerMessage();
    const doc::Document& reopened = *loaded.value();
    ASSERT_EQ(reopened.datums().size(), 2u);
    const doc::Datum& plane = *reopened.datum(planeId);
    EXPECT_EQ(plane.name(), "Side plane");
    EXPECT_EQ(plane.method, doc::DatumMethod::PlaneOffset);
    EXPECT_DOUBLE_EQ(plane.distance, 6.0);
    EXPECT_FALSE(plane.failed()) << plane.error();
    expectVec(plane.geometry().origin, {26, 10, 12.5}, "6 beyond the +X face's middle");
    expectVec(plane.geometry().direction, {1, 0, 0}, "facing +X");
    const doc::Datum& axis = *reopened.datum(axisId);
    EXPECT_FALSE(axis.isVisible());
    EXPECT_FALSE(axis.failed()) << axis.error();
    expectDirection(axis.geometry().direction, {1, 1, 0}, "the diagonal again");
    ASSERT_NE(reopened.sketch(sketchId), nullptr);
    EXPECT_EQ(reopened.sketch(sketchId)->datumPlane(), planeId);
    EXPECT_NEAR(reopened.sketch(sketchId)->plane().origin.x, 26.0, 1e-9);
    // Same JSON again (nothing lost or reordered).
    EXPECT_EQ(io::documentToJson(reopened)["datums"], saved["datums"]);

    // Untrusted input: a reference that does not fit its method, or an
    // unknown method, is refused with a message.
    nlohmann::json broken = saved;
    broken["datums"][0]["refs"] = nlohmann::json::array();
    auto refused = io::documentFromJson(broken);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.userMessage(), "The file contains an invalid construction axis or plane.");
    broken = saved;
    broken["datums"][0]["method"] = "PlaneThroughThreePoints";
    refused = io::documentFromJson(broken);
    ASSERT_FALSE(refused.ok());
    EXPECT_EQ(refused.error(), ErrorCode::FileVersionUnsupported);
    broken = saved;
    broken["datums"][1]["refs"][0]["edge"]["tangent"] = "up";
    EXPECT_FALSE(io::documentFromJson(broken).ok());
    // A document without datums writes none (as older builds did).
    doc::Document empty;
    EXPECT_FALSE(io::documentToJson(empty).contains("datums"));
}
