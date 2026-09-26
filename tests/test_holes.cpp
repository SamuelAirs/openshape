// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Holes for screws: counterbores and countersinks on existing holes (Hole
// steps at a rim) and the kernel's drillHoles, checked by exact volumes.
#include "TestHelpers.h"

#include "document/SketchProfiles.h"
#include "geometry/Holes.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

using namespace os;

namespace {

double frustum(double r1, double r2, double h)
{
    return kPi * h / 3 * (r1 * r1 + r1 * r2 + r2 * r2);
}

// A 40 x 40 plate (centered on the origin, `thickness` thick) with a round
// hole of `diameter` in its middle, cut from a sketch on the top face:
// through, or `blindDepth` deep.
struct HoledPlate {
    doc::Document document;
    cmd::UndoStack stack;
    Uuid body;

    explicit HoledPlate(double diameter = 3.4, double thickness = 10, double blindDepth = 0)
    {
        auto box = std::make_unique<doc::BoxFeature>();
        box->origin = {-20, -20, 0};
        box->size = {40, 40, thickness};
        auto create = std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(box));
        body = create->bodyId();
        EXPECT_TRUE(stack.push(std::move(create), document).ok());
        auto sk = std::make_unique<sketch::Sketch>(Uuid::generate(), sketch::Plane::fromNormal({0, 0, thickness}, {0, 0, 1}));
        sk->addCircle(sketch::kOriginId, diameter / 2);
        const Uuid sketchId = sk->id();
        document.addSketch(std::move(sk));
        const sketch::Sketch& s = *document.sketch(sketchId);
        auto cut = std::make_unique<doc::ExtrudeFeature>();
        cut->sketchId = sketchId;
        cut->profiles = {doc::makeProfileRef(doc::sketchRegions(s).value().front(), s)};
        cut->distance = blindDepth > 0 ? -blindDepth : -thickness;
        cut->mode = doc::ExtrudeMode::Cut;
        cut->throughAll = blindDepth <= 0;
        EXPECT_TRUE(stack.push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(cut)), document).ok());
    }

    const geom::Shape& shape() const { return document.body(body)->shape(); }
    double volume() const { return geom::volume(shape()); }

    // The hole's circular edge at height z.
    int rimAt(double z) const
    {
        for (int i = 0; i < shape().edgeCount(); ++i) {
            const auto e = geom::edgeInfo(shape(), i);
            if (e && e->kind == geom::CurveKind::Circle && std::abs(e->center.z - z) < 1e-9
                && std::abs(e->center.x) < 1e-9 && std::abs(e->center.y) < 1e-9)
                return i;
        }
        return -1;
    }

    std::unique_ptr<doc::HoleFeature> head(doc::HoleKind kind, double diameter, double depth, double rimZ) const
    {
        auto f = std::make_unique<doc::HoleFeature>();
        const int rim = rimAt(rimZ);
        EXPECT_GE(rim, 0);
        f->rim = {rim, *geom::captureEdgeSignature(shape(), rim)};
        f->holeKind = kind;
        f->diameter = diameter;
        f->depth = depth;
        f->angle = kPi / 2;
        return f;
    }
};

// Flat faces with this normal at height z, and their total area.
double flatAreaAt(const geom::Shape& shape, double z, double normalZ)
{
    double area = 0;
    for (int i = 0; i < shape.faceCount(); ++i) {
        const auto f = geom::faceInfo(shape, i);
        if (f && f->isPlanar() && f->normal.z * normalZ > 0.9999 && std::abs(f->centroid.z - z) < 1e-9)
            area += f->area;
    }
    return area;
}

int countFaces(const geom::Shape& shape, geom::SurfaceKind kind)
{
    int n = 0;
    for (int i = 0; i < shape.faceCount(); ++i)
        if (const auto f = geom::faceInfo(shape, i); f && f->kind == kind)
            ++n;
    return n;
}

} // namespace

TEST(Holes, ScrewTablesAreConsistent)
{
    const auto& screws = doc::metricScrews();
    ASSERT_EQ(screws.size(), 6u);
    EXPECT_STREQ(screws[doc::kDefaultScrew].name, "M3");
    EXPECT_DOUBLE_EQ(doc::holeDiameterFor(screws[2], doc::HoleFit::Close), 3.2); // ISO 273 fine
    EXPECT_DOUBLE_EQ(doc::holeDiameterFor(screws[2], doc::HoleFit::Normal), 3.4); // ISO 273 medium
    EXPECT_DOUBLE_EQ(doc::holeDiameterFor(screws[2], doc::HoleFit::Tap), 2.5);
    EXPECT_EQ(doc::holeFitLabel(screws[2], doc::HoleFit::Tap), "M3 tap");
    for (const auto& s : screws) {
        // Every seat is wider than the screw's clearance hole, and grows with the size.
        EXPECT_GT(s.clearanceNormal, s.clearanceClose) << s.name;
        EXPECT_GT(s.clearanceClose, s.nominal) << s.name;
        EXPECT_LT(s.tapDrill, s.nominal) << s.name;
        EXPECT_GT(s.counterboreDiameter, s.clearanceNormal + 0.5) << s.name;
        EXPECT_GT(s.counterboreDepth, s.nominal) << s.name; // ISO 4762 head height k = d
        EXPECT_GT(s.countersinkDiameter, s.clearanceNormal) << s.name;
    }
}

TEST(Holes, MaterialDepthAlongAHole)
{
    HoledPlate plate;
    EXPECT_NEAR(geom::materialDepth(plate.shape(), {10, 0, 10}, {0, 0, -1}).value_or(-1), 10.0, 1e-7);
    EXPECT_NEAR(geom::materialDepth(plate.shape(), {-20, 5, 5}, {1, 0, 0}).value_or(-1), 40.0, 1e-7);
    EXPECT_FALSE(geom::materialDepth(plate.shape(), {0, 0, 10}, {0, 0, -1})) << "in the hole: no material";
    EXPECT_FALSE(geom::materialDepth(plate.shape(), {30, 0, 10}, {0, 0, -1})) << "beside the plate";
}

// A counterbore on an existing hole takes away exactly the ring
// pi (D^2 - d^2) / 4 x depth, and its floor is flat.
TEST(Holes, CounterboreOnAHoleRemovesTheRing)
{
    HoledPlate plate; // M3 normal fit: 3.4 through 10 mm
    const double before = plate.volume();
    ASSERT_NEAR(before, 16000.0 - kPi * 1.7 * 1.7 * 10, 1e-6);
    plate.document.insertFeature(plate.body, plate.head(doc::HoleKind::Counterbore, 6.5, 3.4, 10));
    const doc::Body& body = *plate.document.body(plate.body);
    ASSERT_FALSE(body.hasFailures()) << body.state(2).userMessage;
    const double ring = kPi * (3.25 * 3.25 - 1.7 * 1.7) * 3.4;
    EXPECT_NEAR(before - plate.volume(), ring, 1e-6);
    // The seat is one flat ring at 10 - 3.4, facing up; the plate's bottom stays whole.
    EXPECT_NEAR(flatAreaAt(plate.shape(), 6.6, 1), kPi * (3.25 * 3.25 - 1.7 * 1.7), 1e-6);
    EXPECT_NEAR(flatAreaAt(plate.shape(), 0, -1), 1600 - kPi * 1.7 * 1.7, 1e-6);
    const auto box = geom::boundingBox(plate.shape());
    EXPECT_NEAR(box.size().z, 10.0, 1e-7);
    EXPECT_NEAR(box.size().x, 40.0, 1e-7);
    // The seat's wall is a new cylinder of the counterbore's radius.
    bool wall = false;
    for (int i = 0; i < plate.shape().faceCount(); ++i)
        if (const auto f = geom::faceInfo(plate.shape(), i); f && f->kind == geom::SurfaceKind::Cylinder)
            wall = wall || std::abs(f->radius - 3.25) < 1e-9;
    EXPECT_TRUE(wall);
}

// From the bottom rim, the counterbore goes up into the part.
TEST(Holes, CounterboreFromTheOtherSide)
{
    HoledPlate plate;
    const double before = plate.volume();
    plate.document.insertFeature(plate.body, plate.head(doc::HoleKind::Counterbore, 8.0, 4.4, 0));
    ASSERT_FALSE(plate.document.body(plate.body)->hasFailures());
    EXPECT_NEAR(before - plate.volume(), kPi * (16 - 1.7 * 1.7) * 4.4, 1e-6);
    EXPECT_NEAR(flatAreaAt(plate.shape(), 4.4, -1), kPi * (16 - 1.7 * 1.7), 1e-6);
}

// A 90-degree countersink takes away the cone frustum around the hole, from
// the countersink's diameter at the surface down to the hole.
TEST(Holes, CountersinkOnAHoleRemovesTheFrustum)
{
    HoledPlate plate;
    const double before = plate.volume();
    plate.document.insertFeature(plate.body, plate.head(doc::HoleKind::Countersink, 6.72, 0, 10));
    const doc::Body& body = *plate.document.body(plate.body);
    ASSERT_FALSE(body.hasFailures()) << body.state(2).userMessage;
    const double R = 3.36, r = 1.7, h = R - r; // 45 degrees each side
    EXPECT_NEAR(before - plate.volume(), frustum(R, r, h) - kPi * r * r * h, 1e-6);
    EXPECT_EQ(countFaces(plate.shape(), geom::SurfaceKind::Cone), 1);
    // The top keeps its flat face (minus the countersink), the bottom is untouched.
    EXPECT_NEAR(flatAreaAt(plate.shape(), 10, 1), 1600 - kPi * R * R, 1e-6);
    EXPECT_NEAR(flatAreaAt(plate.shape(), 0, -1), 1600 - kPi * r * r, 1e-6);
    // An 82-degree countersink (imperial screws) of the same diameter is deeper.
    const Uuid id = body.features().back()->id();
    ASSERT_TRUE(plate.document.body(plate.body)->feature(id)->setParameter("angle", 82 * kPi / 180).ok());
    plate.document.featureChanged(id);
    const double h82 = (R - r) / std::tan(41 * kPi / 180);
    EXPECT_NEAR(before - plate.volume(), frustum(R, r, h82) - kPi * r * r * h82, 1e-6);
}

TEST(Holes, HeadsThatCannotBeMadeAreRefusedWithAReason)
{
    HoledPlate plate;
    auto refused = [&](std::unique_ptr<doc::HoleFeature> f, const std::string& words) {
        const auto result = plate.document.preview(plate.body, *f);
        EXPECT_FALSE(result.ok());
        EXPECT_NE(result.userMessage().find(words), std::string::npos) << result.userMessage();
    };
    refused(plate.head(doc::HoleKind::Counterbore, 3.0, 3, 10), "counterbore must be wider than the hole (3.40 mm)");
    refused(plate.head(doc::HoleKind::Counterbore, 3.4, 3, 10), "wider than the hole");
    refused(plate.head(doc::HoleKind::Counterbore, 6.5, 10, 10), "would reach through the part: it is 10.00 mm thick");
    refused(plate.head(doc::HoleKind::Counterbore, 6.5, 12, 10), "reach through the part");
    refused(plate.head(doc::HoleKind::Countersink, 3.0, 0, 10), "countersink must be wider");
    refused(plate.head(doc::HoleKind::Countersink, 30, 0, 10), "countersink would reach through the part");
    // A blind hole 2 mm deep: a 3.4 mm counterbore would be deeper than the hole.
    HoledPlate blind(3.4, 10, 2);
    const auto deep = blind.document.preview(blind.body, *blind.head(doc::HoleKind::Counterbore, 6.5, 3.4, 10));
    EXPECT_FALSE(deep.ok());
    EXPECT_EQ(deep.userMessage(), "The counterbore is deeper than the hole.");
    EXPECT_TRUE(blind.document.preview(blind.body, *blind.head(doc::HoleKind::Counterbore, 6.5, 1.5, 10)).ok());
}

// Steps from older files (no "type") drill the plain cylinder as before;
// counterbores and countersinks survive a save and reopen.
TEST(Holes, HoleStepsRoundTripAndOldFilesComputeAsBefore)
{
    HoledPlate plate;
    const double holed = plate.volume();
    auto plain = plate.head(doc::HoleKind::Plain, 4.0, 6.0, 10);
    plain->preset = "M3 heat-set insert";
    nlohmann::json params;
    plain->writeParams(params);
    EXPECT_FALSE(params.contains("type")) << "plain holes are written as before";
    params.erase("preset");
    doc::HoleFeature old;
    ASSERT_TRUE(old.readParams(params).ok());
    EXPECT_EQ(old.holeKind, doc::HoleKind::Plain);
    const auto oldResult = plate.document.preview(plate.body, old);
    ASSERT_TRUE(oldResult.ok());
    EXPECT_NEAR(holed - geom::volume(oldResult.value()), kPi * (4 - 1.7 * 1.7) * 6, 1e-6);

    plate.document.insertFeature(plate.body, plate.head(doc::HoleKind::Countersink, 6.72, 0, 10));
    plate.document.insertFeature(plate.body, plate.head(doc::HoleKind::Counterbore, 6.5, 3.4, 0));
    ASSERT_FALSE(plate.document.body(plate.body)->hasFailures());
    const double volume = plate.volume();
    const auto json = io::documentToJson(plate.document);
    auto again = io::documentFromJson(json);
    ASSERT_TRUE(again.ok()) << again.developerMessage();
    const doc::Body& body = *again.value()->body(plate.body);
    EXPECT_FALSE(body.hasFailures());
    EXPECT_NEAR(geom::volume(body.shape()), volume, 1e-6);
    const auto& sink = static_cast<const doc::HoleFeature&>(*body.features()[2]);
    const auto& bore = static_cast<const doc::HoleFeature&>(*body.features()[3]);
    EXPECT_EQ(sink.holeKind, doc::HoleKind::Countersink);
    EXPECT_NEAR(sink.angle, kPi / 2, 1e-12);
    EXPECT_EQ(bore.holeKind, doc::HoleKind::Counterbore);
    EXPECT_NEAR(bore.depth, 3.4, 1e-12);
    // A countersink is written without a depth, so builds that predate it
    // refuse the file rather than drill a plain hole of its diameter.
    for (const auto& f : json["bodies"][0]["features"]) {
        if (f["type"] == "Hole" && f["params"].value("type", "") == "Countersink") {
            EXPECT_FALSE(f["params"].contains("depth"));
        }
    }
    // An unknown hole type is refused, not guessed.
    auto broken = json;
    for (auto& f : broken["bodies"][0]["features"])
        if (f["type"] == "Hole")
            f["params"]["type"] = "Spotface";
    EXPECT_FALSE(io::documentFromJson(broken).ok());
}

// drillHoles directly: a counterbored and a countersunk through hole, and a
// blind hole, each with exactly the volume its shape holds.
TEST(Holes, DrillHolesCutsExactShapes)
{
    const auto plate = geom::makeBox({0, 0, 0}, {60, 30, 5}).value();
    const double full = 60 * 30 * 5;
    geom::HoleCut bore;
    bore.entry = {10, 15, 5};
    bore.diameter = 3.4;
    bore.throughAll = true;
    bore.head = geom::HoleHead::Counterbore;
    bore.headDiameter = 6.5;
    bore.headDepth = 3.4;
    geom::HoleCut sink = bore;
    sink.entry = {30, 15, 5};
    sink.head = geom::HoleHead::Countersink;
    sink.headDiameter = 6.72;
    sink.headAngle = kPi / 2;
    geom::HoleCut blind;
    blind.entry = {50, 15, 5};
    blind.diameter = 2.5;
    blind.depth = 3;
    auto drilled = geom::drillHoles(plate, {bore, sink, blind});
    ASSERT_TRUE(drilled.ok()) << drilled.developerMessage();
    const double shaft = kPi * 1.7 * 1.7 * 5;
    const double expected = full - (shaft + kPi * (3.25 * 3.25 - 1.7 * 1.7) * 3.4) - (shaft + frustum(3.36, 1.7, 1.66) - kPi * 1.7 * 1.7 * 1.66)
                          - kPi * 1.25 * 1.25 * 3;
    EXPECT_NEAR(geom::volume(drilled.value()), expected, 1e-6);
    EXPECT_TRUE(geom::isValid(drilled.value()));
    // The blind hole's bottom is flat, at 3 mm below the top.
    EXPECT_NEAR(flatAreaAt(drilled.value(), 2, 1), kPi * 1.25 * 1.25, 1e-6);

    // Refusals before any kernel work.
    geom::HoleCut narrow = bore;
    narrow.headDiameter = 3.0;
    EXPECT_EQ(geom::drillHoles(plate, {narrow}).userMessage(), "The counterbore must be wider than the hole.");
    geom::HoleCut shallow = blind;
    shallow.head = geom::HoleHead::Countersink;
    shallow.headDiameter = 10;
    shallow.headAngle = kPi / 2;
    EXPECT_EQ(geom::drillHoles(plate, {shallow}).userMessage(), "The countersink must be shallower than the hole.");
    geom::HoleCut away = blind;
    away.entry = {100, 15, 5};
    EXPECT_EQ(geom::drillHoles(plate, {away}).userMessage(), "The hole does not reach into the part here.");
}
