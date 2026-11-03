// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "samples/Samples.h"

#include "commands/DocumentCommands.h"
#include "core/Math.h"
#include "document/Datum.h"
#include "document/Feature.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "geometry/Profiles.h"
#include "geometry/TopoSignature.h"
#include "sketch/Sketch.h"
#include "sketch/SketchEdit.h"

#include <cmath>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <utility>

namespace os::samples {
namespace {

using sketch::ConstraintKind;
using sketch::EntityId;
using sketch::SketchConstraint;

constexpr double kDegree = kPi / 180;

// The origin planes as the Sketch button starts them (InteractionController::startSketch).
sketch::Plane frontPlane() { return {{0, 0, 0}, {1, 0, 0}, {0, 0, 1}}; } // XZ; its normal is -Y
sketch::Plane rightPlane() { return {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}}; } // YZ; its normal is +X

// A sample's steps, pushed through the undo stack. The first failure stops
// the rest (each call then does nothing) and is what status() says.
class Build {
public:
    Build(doc::Document& document, cmd::UndoStack& stack) : document_(document), stack_(stack) {}

    const Status& status() const { return status_; }
    bool ok() const { return status_.ok(); }

    // Sketches and planes are hidden once used (the Model panel shows them
    // again): the sample opens showing its parts.
    Uuid sketch(sketch::Sketch sk, const std::string& name)
    {
        sk.setName(name);
        sk.setVisible(false);
        const sketch::SolveReport report = sketch::solve(sk);
        if (!report.ok)
            fail("sketch " + name + " does not solve: " + report.message);
        const Uuid id = sk.id();
        push(std::make_unique<cmd::CreateSketchCommand>(std::move(sk)), "sketch " + name);
        return id;
    }

    // A construction plane `distance` from an origin plane (normal 0 X, 1 Y, 2 Z).
    Uuid plane(int originIndex, double distance)
    {
        doc::Datum datum;
        datum.method = doc::DatumMethod::PlaneOffset;
        datum.originIndex = originIndex;
        datum.distance = distance;
        datum.setName(document_.nextDatumName(doc::DatumKind::Plane));
        datum.setVisible(false);
        const Uuid id = datum.id();
        push(std::make_unique<cmd::AddDatumCommand>(std::move(datum)), "a plane");
        return id;
    }
    // A sketch on that plane (following it when its distance changes).
    sketch::Sketch sketchOn(const Uuid& datum) const
    {
        const doc::Datum* d = document_.datum(datum);
        sketch::Sketch sk(Uuid::generate(), d ? doc::sketchPlaneOn(d->geometry()) : sketch::Plane::xy());
        sk.setDatumPlane(datum);
        return sk;
    }

    // The closed region of a sketch around `inside` (sketch coordinates).
    doc::ProfileRef profile(const Uuid& sketchId, Vec2 inside)
    {
        const sketch::Sketch* sk = document_.sketch(sketchId);
        if (!sk) {
            fail("no sketch for a profile");
            return {};
        }
        const auto regions = doc::sketchRegions(*sk);
        const sketch::Plane& p = sk->plane();
        const Vec3 world = p.origin + p.xAxis * inside.x + p.yAxis * inside.y;
        if (regions)
            for (const geom::Region& region : regions.value())
                if (geom::regionContains(region.face, world))
                    return doc::makeProfileRef(region, *sk);
        fail("no closed shape around (" + std::to_string(inside.x) + ", " + std::to_string(inside.y) + ") in "
             + sk->name());
        return {};
    }
    std::vector<doc::ProfileRef> profiles(const Uuid& sketchId, const std::vector<Vec2>& insides)
    {
        std::vector<doc::ProfileRef> out;
        for (const Vec2& p : insides)
            out.push_back(profile(sketchId, p));
        return out;
    }

    Uuid body(const std::string& name, std::unique_ptr<doc::Feature> base)
    {
        auto create = std::make_unique<cmd::CreateBodyCommand>(name, std::move(base));
        const Uuid id = create->bodyId();
        push(std::move(create), "body " + name);
        return id;
    }
    void add(const Uuid& body, std::unique_ptr<doc::Feature> step, const std::string& what)
    {
        push(std::make_unique<cmd::AddFeatureCommand>(body, std::move(step)), what);
    }
    void hide(const Uuid& body) { push(std::make_unique<cmd::SetBodyVisibilityCommand>(body, false), "hiding a body"); }

    geom::Shape shape(const Uuid& body) const
    {
        const doc::Body* b = document_.body(body);
        return b ? b->shape() : geom::Shape();
    }

    // The flat face of `body` facing `normal` with `onPlane` on it (else the
    // one in that plane whose middle is nearest it).
    doc::FaceRef flatFace(const Uuid& body, const Vec3& normal, const Vec3& onPlane)
    {
        const geom::Shape s = shape(body);
        int best = -1;
        double bestDistance = std::numeric_limits<double>::max();
        for (int i = 0; ok() && i < s.faceCount(); ++i) {
            const auto info = geom::faceInfo(s, i);
            if (!info || !info->isPlanar() || info->normal.dot(normal.normalized()) < 0.9999)
                continue;
            if (std::abs((onPlane - info->planeOrigin).dot(info->normal)) > 1e-6)
                continue;
            const auto inside = geom::pointOnFace(s, i, onPlane);
            const double distance = inside && (*inside - onPlane).length() < 1e-6 ? -1 : (info->centroid - onPlane).length();
            if (distance < bestDistance) {
                best = i;
                bestDistance = distance;
            }
        }
        if (best < 0) {
            fail("no flat face there");
            return {};
        }
        return {best, *geom::captureFaceSignature(s, best)};
    }
    int faceIndex(const doc::FaceRef& face) const { return face.indexHint; }

    std::vector<doc::EdgeRef> edges(const Uuid& body, const std::function<bool(const geom::EdgeInfo&)>& pick)
    {
        const geom::Shape s = shape(body);
        std::vector<doc::EdgeRef> out;
        for (int i = 0; i < s.edgeCount(); ++i)
            if (const auto info = geom::edgeInfo(s, i); info && pick(*info))
                if (const auto signature = geom::captureEdgeSignature(s, i))
                    out.push_back({i, *signature});
        if (out.empty())
            fail("no edges picked");
        return out;
    }

    void fail(const std::string& what)
    {
        if (ok())
            status_ = Status::failure(ErrorCode::InvalidArgument, "A sample could not be built.", what);
    }

private:
    void push(std::unique_ptr<cmd::Command> command, const std::string& what)
    {
        if (!ok())
            return;
        const Status s = stack_.push(std::move(command), document_);
        if (!s)
            status_ = Status::failure(s.error(), s.userMessage(), what + ": " + s.developerMessage());
    }

    doc::Document& document_;
    cmd::UndoStack& stack_;
    Status status_ = okStatus();
};

void constrain(sketch::Sketch& sk, ConstraintKind kind, EntityId a, EntityId b = sketch::kNoEntity, double value = 0)
{
    SketchConstraint c;
    c.kind = kind;
    c.a = a;
    c.b = b;
    c.value = value;
    (void)sk.addConstraint(c);
}

// A closed outline of lines through `corners`, in order; returns the lines
// (line i from corner i to corner i + 1). The first corner may be the origin.
std::vector<EntityId> outline(sketch::Sketch& sk, const std::vector<EntityId>& corners)
{
    std::vector<EntityId> lines;
    for (std::size_t i = 0; i < corners.size(); ++i)
        lines.push_back(sk.addLine(corners[i], corners[(i + 1) % corners.size()]));
    return lines;
}

// A rounded rectangle centered at `center`, `width` x `height`, corners of
// `radius` (filletCorner keeps the sharp corners as points, so the sizes are
// dimensions between them).
void roundedRectangle(sketch::Sketch& sk, Vec2 center, double width, double height, double radius,
                      EntityId reuseCenter = sketch::kNoEntity)
{
    const auto r = sketch::addCenterRectangle(sk, center, center + Vec2{width / 2, height / 2}, reuseCenter);
    constrain(sk, ConstraintKind::HorizontalDistance, r.rectangle.corners[0], r.rectangle.corners[1], width);
    constrain(sk, ConstraintKind::VerticalDistance, r.rectangle.corners[1], r.rectangle.corners[2], height);
    for (EntityId corner : r.rectangle.corners)
        (void)sketch::filletCorner(sk, corner, radius);
}

// A hexagon `acrossFlats` wide around `center`, one side facing `up`
// (the direction of its first side's middle); sized across flats.
// (Around the sketch's origin point when centered there.)
sketch::PolygonIds hexagon(sketch::Sketch& sk, Vec2 center, double acrossFlats, double upDegrees)
{
    const double a = upDegrees * kDegree;
    const EntityId reuse = center.length() < 1e-12 ? sketch::kOriginId : sketch::kNoEntity;
    const auto ids = sketch::addPolygon(sk, center, center + Vec2{std::cos(a), std::sin(a)} * (acrossFlats / 2), 6, reuse);
    constrain(sk, ConstraintKind::Diameter, ids.inner, sketch::kNoEntity, acrossFlats);
    return ids;
}

std::vector<EntityId> curvesOf(const sketch::PolygonIds& p)
{
    std::vector<EntityId> curves = p.sides;
    curves.push_back(p.outer);
    curves.push_back(p.inner);
    return curves;
}

std::unique_ptr<doc::ExtrudeFeature> extrude(const Uuid& sketchId, std::vector<doc::ProfileRef> profiles, double distance,
                                             doc::ExtrudeMode mode)
{
    auto f = std::make_unique<doc::ExtrudeFeature>();
    f->sketchId = sketchId;
    f->profiles = std::move(profiles);
    f->distance = distance;
    f->mode = mode;
    return f;
}

std::unique_ptr<doc::ShellFeature> shell(std::vector<doc::FaceRef> open, double thickness)
{
    auto f = std::make_unique<doc::ShellFeature>();
    f->faces = std::move(open);
    f->thickness = thickness;
    return f;
}

std::unique_ptr<doc::FilletFeature> fillet(std::vector<doc::EdgeRef> edges, double radius)
{
    auto f = std::make_unique<doc::FilletFeature>();
    f->edges = std::move(edges);
    f->size = radius;
    return f;
}

bool near(double a, double b, double tolerance = 1e-4) { return std::abs(a - b) < tolerance; }

// A straight edge along X whose middle is at (y, z).
std::function<bool(const geom::EdgeInfo&)> alongXAt(double y, double z)
{
    return [y, z](const geom::EdgeInfo& e) {
        return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.x) - 1) < 1e-9 && near(e.midpoint.y, y)
            && near(e.midpoint.z, z);
    };
}

// ---------------------------------------------------------------- Lamp shade
// A table-lamp shade: a fully sized outline of its 1.6 mm wall (radius 70
// at the foot, 32 at the top, 120 high, its sides arcs of 150 and 148.4
// about one center) revolved, a ring for an E27 lamp holder joined at the
// top, and 16 windows: one wedge sketched on a construction plane,
// patterned around the axis in its sketch and cut up through the side.
Status lampShade(Build& b)
{
    const double foot = 70, top = 32, height = 120, side = 150, wall = 1.6;
    sketch::Sketch sk(Uuid::generate(), frontPlane());
    const Vec2 A{foot, 0}, B{top, height};
    const Vec2 chord = B - A;
    const double half = chord.length() / 2;
    const Vec2 left = Vec2{-chord.y, chord.x} * (1 / chord.length());
    const Vec2 center = (A + B) * 0.5 + left * std::sqrt(side * side - half * half); // a counterclockwise arc A -> B, bulging out
    // Where the inside of the wall (radius side - wall, the same center) is at a height.
    const auto inside = [&](double z) { return Vec2{center.x + std::sqrt(std::pow(side - wall, 2) - std::pow(z - center.y, 2)), z}; };
    const EntityId origin = sketch::kOriginId;
    const EntityId a = sk.addPoint(A), t = sk.addPoint(B), ia = sk.addPoint(inside(0)), it = sk.addPoint(inside(height));
    const EntityId m = sk.addPoint(center);
    const EntityId base = sk.addLine(ia, a);
    const EntityId arc = sk.addArc(m, a, t);
    const EntityId cap = sk.addLine(t, it);
    const EntityId insideArc = sk.addArc(m, ia, it);
    const EntityId ground = sk.addLine(origin, a, true); // construction: the foot on the ground
    constrain(sk, ConstraintKind::Horizontal, base);
    constrain(sk, ConstraintKind::Horizontal, cap);
    constrain(sk, ConstraintKind::Horizontal, ground);
    constrain(sk, ConstraintKind::HorizontalDistance, origin, a, foot);
    constrain(sk, ConstraintKind::VerticalDistance, origin, t, height);
    constrain(sk, ConstraintKind::HorizontalDistance, origin, t, top);
    constrain(sk, ConstraintKind::Radius, arc, sketch::kNoEntity, side);
    constrain(sk, ConstraintKind::Radius, insideArc, sketch::kNoEntity, side - wall);
    const Uuid outlineSketch = b.sketch(std::move(sk), "Outline");

    // The wall revolved (open at both ends): both its sides stay surfaces of
    // revolution, which the window cut below meets quickly (a shelled solid's
    // inside is an offset surface: the cut took 14 s to open, TD-164).
    auto revolve = std::make_unique<doc::RevolveFeature>();
    revolve->sketchId = outlineSketch;
    revolve->profiles = {b.profile(outlineSketch, inside(60) + Vec2{wall / 2, 0})};
    revolve->axis = doc::SketchAxis::Y;
    const Uuid shade = b.body("Shade", std::move(revolve));

    // The lamp holder's ring: 42 mm across its hole, 3 mm thick, under the top.
    const Uuid topPlane = b.plane(2, height);
    sketch::Sketch ring = b.sketchOn(topPlane);
    const EntityId hole = ring.addCircle(sketch::kOriginId, 21);
    const EntityId rim = ring.addCircle(sketch::kOriginId, 31.5);
    constrain(ring, ConstraintKind::Diameter, hole, sketch::kNoEntity, 42);
    constrain(ring, ConstraintKind::Diameter, rim, sketch::kNoEntity, 63);
    const Uuid ringSketch = b.sketch(std::move(ring), "Holder ring");
    b.add(shade, extrude(ringSketch, {b.profile(ringSketch, {26, 0})}, -3, doc::ExtrudeMode::Join), "the holder ring");

    // The windows: one wedge on a construction plane 24 mm up (two sides
    // along radii, 9 degrees apart), patterned 16 times around the axis in
    // its sketch and cut up through the side, 62 mm. Every face of the cut
    // meets the shade's curved wall in a circle (a plane through the axis,
    // or across it), which keeps the cut quick: slots there took 15 s.
    const double inner = 35, outer = 90, spread = 4.5 * kDegree, front = -90 * kDegree;
    const auto polar = [](double radius, double angle) { return Vec2{std::cos(angle), std::sin(angle)} * radius; };
    const Uuid windowPlane = b.plane(2, 24);
    sketch::Sketch wedge = b.sketchOn(windowPlane);
    const EntityId p1 = wedge.addPoint(polar(inner, front - spread)), p2 = wedge.addPoint(polar(outer, front - spread));
    const EntityId p3 = wedge.addPoint(polar(outer, front + spread)), p4 = wedge.addPoint(polar(inner, front + spread));
    const std::vector<EntityId> sides{wedge.addLine(p1, p2), wedge.addLine(p2, p3), wedge.addLine(p3, p4), wedge.addLine(p4, p1)};
    sketch::PatternLayout around;
    around.circular = true;
    around.count = 16;
    (void)sketch::patternCurves(wedge, sides, around);
    const Uuid windowSketch = b.sketch(std::move(wedge), "Windows");
    std::vector<Vec2> insides;
    for (int k = 0; k < 16; ++k)
        insides.push_back(polar(60, front + k * 2 * kPi / 16));
    b.add(shade, extrude(windowSketch, b.profiles(windowSketch, insides), 62, doc::ExtrudeMode::Cut), "the windows");
    return b.status();
}

// ---------------------------------------------------------------- Twisted vase
// Four hexagons on the ground and three construction planes, each a little
// larger or smaller and turned 15 degrees further, lofted smoothly into one
// twisting body; shelled open at the top.
Status twistedVase(Build& b)
{
    struct Section {
        double height, acrossFlats, turn;
    };
    const Section sections[] = {{0, 62, 0}, {60, 94, 15}, {125, 76, 30}, {175, 58, 45}};
    std::vector<doc::LoftSection> loftSections;
    int n = 0;
    for (const Section& s : sections) {
        ++n;
        sketch::Sketch sk = s.height == 0 ? sketch::Sketch(Uuid::generate(), sketch::Plane::xy()) : b.sketchOn(b.plane(2, s.height));
        (void)hexagon(sk, {0, 0}, s.acrossFlats, 90 + s.turn);
        const Uuid id = b.sketch(std::move(sk), "Section " + std::to_string(n));
        loftSections.push_back({id, b.profile(id, {0, 0})});
    }
    auto loft = std::make_unique<doc::LoftFeature>();
    loft->sections = std::move(loftSections);
    const Uuid vase = b.body("Vase", std::move(loft));
    b.add(vase, shell({b.flatFace(vase, {0, 0, 1}, {0, 0, sections[3].height})}, 2), "the shell");
    return b.status();
}

// ---------------------------------------------------------------- Electronics box
// A 90 x 60 x 36 mm box with rounded corners: its foot rounded, shelled with
// 2 mm walls, four standoffs for a circuit board, two countersunk M3 holes
// through the floor, five vent slots (one slot and a sketch pattern) and a
// USB-C opening; beside it the lid, upside down: a plate with a lip that
// fits inside the walls (0.2 mm all round) and the name engraved.
Status electronicsBox(Build& b)
{
    const double w = 90, d = 60, h = 36, wall = 2, corner = 8;
    sketch::Sketch foot(Uuid::generate(), sketch::Plane::xy());
    roundedRectangle(foot, {0, 0}, w, d, corner, sketch::kOriginId);
    const Uuid footSketch = b.sketch(std::move(foot), "Footprint");
    const Uuid box = b.body("Box", extrude(footSketch, {b.profile(footSketch, {0, 0})}, h, doc::ExtrudeMode::NewBody));
    b.add(box, fillet(b.edges(box, [](const geom::EdgeInfo& e) { return near(e.start.z, 0) && near(e.end.z, 0); }), 3),
          "the rounded foot");
    b.add(box, shell({b.flatFace(box, {0, 0, 1}, {0, 0, h})}, wall), "the shell");

    // Standoffs for a board with holes 64 x 36 apart: rings from the ground up.
    sketch::Sketch posts(Uuid::generate(), sketch::Plane::xy());
    std::vector<Vec2> insides;
    for (const Vec2 c : {Vec2{-32, -18}, Vec2{32, -18}, Vec2{32, 18}, Vec2{-32, 18}}) {
        const EntityId p = posts.addPoint(c);
        constrain(posts, ConstraintKind::Diameter, posts.addCircle(p, 3.5), sketch::kNoEntity, 7);
        constrain(posts, ConstraintKind::Diameter, posts.addCircle(p, 1.25), sketch::kNoEntity, 2.5);
        insides.push_back(c + Vec2{2.4, 0});
    }
    const Uuid postsSketch = b.sketch(std::move(posts), "Standoffs");
    b.add(box, extrude(postsSketch, b.profiles(postsSketch, insides), wall + 7, doc::ExtrudeMode::Join), "the standoffs");

    // Two M3 countersunk holes through the floor, for screws into a wall.
    auto holes = std::make_unique<doc::HolesFeature>();
    holes->face = b.flatFace(box, {0, 0, 1}, {0, 0, wall});
    holes->positions = {{-16, 0}, {16, 0}};
    holes->diameter = 3.4;
    holes->head = doc::HoleKind::Countersink;
    holes->headDiameter = 6.72;
    holes->preset = "M3 normal fit";
    b.add(box, std::move(holes), "the screw holes");

    // Vent slots in the front wall: one slot, patterned in the sketch.
    sketch::Sketch vents(Uuid::generate(), frontPlane());
    const auto first = sketch::addSlot(vents, {-16, 14}, {-16, 28}, 1.5);
    std::vector<EntityId> slotCurves{first.lines[0], first.lines[1], first.arcs[0], first.arcs[1]};
    sketch::PatternLayout row;
    row.step = {8, 0};
    row.count = 5;
    (void)sketch::patternCurves(vents, slotCurves, row);
    const Uuid ventsSketch = b.sketch(std::move(vents), "Vents");
    b.add(box,
          extrude(ventsSketch, b.profiles(ventsSketch, {{-16, 21}, {-8, 21}, {0, 21}, {8, 21}, {16, 21}}), 40,
                  doc::ExtrudeMode::Cut),
          "the vents");

    // A USB-C opening in the right end.
    sketch::Sketch port(Uuid::generate(), rightPlane());
    (void)sketch::addSlot(port, {-3.5, 12}, {3.5, 12}, 1.9);
    const Uuid portSketch = b.sketch(std::move(port), "USB-C");
    b.add(box, extrude(portSketch, {b.profile(portSketch, {0, 12})}, 60, doc::ExtrudeMode::Cut), "the USB-C opening");

    // The lid, beside the box and upside down: a 2 mm plate, a lip 4 mm
    // higher that fits inside the walls, the name engraved inside.
    const Vec2 at{105, 0};
    sketch::Sketch plate(Uuid::generate(), sketch::Plane::xy());
    roundedRectangle(plate, at, w, d, corner);
    const Uuid plateSketch = b.sketch(std::move(plate), "Lid");
    const Uuid lid = b.body("Lid", extrude(plateSketch, {b.profile(plateSketch, at)}, wall, doc::ExtrudeMode::NewBody));
    b.add(lid, fillet(b.edges(lid, [](const geom::EdgeInfo& e) { return near(e.start.z, 0) && near(e.end.z, 0); }), 1.5),
          "the lid's rounded edge");
    const double fit = 0.2, lipWall = 1.6;
    const double lipW = w - 2 * (wall + fit), lipD = d - 2 * (wall + fit), lipCorner = corner - wall - fit;
    sketch::Sketch lip(Uuid::generate(), sketch::Plane::xy());
    roundedRectangle(lip, at, lipW, lipD, lipCorner);
    roundedRectangle(lip, at, lipW - 2 * lipWall, lipD - 2 * lipWall, lipCorner - lipWall);
    const Uuid lipSketch = b.sketch(std::move(lip), "Lip");
    b.add(lid, extrude(lipSketch, {b.profile(lipSketch, at + Vec2{lipW / 2 - lipWall / 2, 0})}, wall + 4, doc::ExtrudeMode::Join),
          "the lip");
    auto name = std::make_unique<doc::TextFeature>();
    name->face = b.flatFace(lid, {0, 0, 1}, {at.x, at.y, wall});
    name->position = {at.x, at.y};
    name->text = "FoldForm";
    name->size = 8;
    name->depth = -0.6;
    name->font = doc::kTextFontBold;
    b.add(lid, std::move(name), "the name");
    return b.status();
}

// ---------------------------------------------------------------- Phone stand
// A phone stand from a fully sized side outline (a base, a rest leaning
// back at 70 degrees, a lip): half of it extruded, its edges rounded,
// mirrored across the middle; a cable notch in the lip and a window in the
// rest, both cut through the middle; the name engraved on the rest.
Status phoneStand(Build& b)
{
    const double length = 100, base = 6, front = 24, thick = 8.5, top = 110, lean = 70 * kDegree, lipW = 8, lipH = 16;
    const Vec2 dir{std::cos(lean), std::sin(lean)};
    const double up = (top - base) / dir.y;
    const Vec2 F1{front, base}, G{front + thick, base};
    const Vec2 H = G + dir * up, F2 = F1 + dir * up;
    sketch::Sketch sk(Uuid::generate(), rightPlane());
    const EntityId A = sketch::kOriginId;
    const EntityId B = sk.addPoint({length, 0}), C = sk.addPoint({length, base}), g = sk.addPoint(G), h = sk.addPoint(H),
                   f2 = sk.addPoint(F2), f1 = sk.addPoint(F1), l1 = sk.addPoint({lipW, base}), l2 = sk.addPoint({lipW, lipH}),
                   l3 = sk.addPoint({0, lipH});
    const auto e = outline(sk, {A, B, C, g, h, f2, f1, l1, l2, l3});
    for (int i : {0, 2, 4, 6, 8})
        constrain(sk, ConstraintKind::Horizontal, e[i]);
    for (int i : {1, 7, 9})
        constrain(sk, ConstraintKind::Vertical, e[i]);
    constrain(sk, ConstraintKind::Parallel, e[3], e[5]);
    constrain(sk, ConstraintKind::PointOnLine, f1, e[2]);
    constrain(sk, ConstraintKind::HorizontalDistance, A, B, length);
    constrain(sk, ConstraintKind::VerticalDistance, B, C, base);
    constrain(sk, ConstraintKind::HorizontalDistance, A, f1, front);
    constrain(sk, ConstraintKind::HorizontalDistance, f1, g, thick);
    constrain(sk, ConstraintKind::VerticalDistance, A, h, top);
    constrain(sk, ConstraintKind::Angle, e[0], e[3], lean);
    constrain(sk, ConstraintKind::HorizontalDistance, l3, l2, lipW);
    constrain(sk, ConstraintKind::VerticalDistance, A, l3, lipH);
    const Uuid side = b.sketch(std::move(sk), "Side");
    const Uuid stand = b.body("Stand", extrude(side, {b.profile(side, {60, 3})}, 40, doc::ExtrudeMode::NewBody));

    // Rounded: the top of the rest, the rear of the base and the lip; the
    // inside corners filled where the rest meets the base.
    b.add(stand, fillet(b.edges(stand, [&](const geom::EdgeInfo& x) {
                            return alongXAt(H.x, H.y)(x) || alongXAt(F2.x, F2.y)(x) || alongXAt(length, base)(x);
                        }),
                        3),
          "the rounded top and rear");
    b.add(stand, fillet(b.edges(stand, [&](const geom::EdgeInfo& x) { return alongXAt(lipW, lipH)(x) || alongXAt(0, lipH)(x); }),
                        1.5),
          "the rounded lip");
    b.add(stand, fillet(b.edges(stand, [&](const geom::EdgeInfo& x) { return alongXAt(G.x, G.y)(x); }), 6), "the rest's foot");
    b.add(stand, fillet(b.edges(stand, [&](const geom::EdgeInfo& x) { return alongXAt(F1.x, F1.y)(x); }), 2), "the groove");

    // The outer side's outline softened.
    b.add(stand, fillet(b.edges(stand, [](const geom::EdgeInfo& x) { return near(x.start.x, 40) && near(x.end.x, 40); }), 1.5),
          "the rounded side");
    auto mirror = std::make_unique<doc::MirrorFeature>();
    mirror->planeNormal = {1, 0, 0};
    b.add(stand, std::move(mirror), "the mirror");

    // A notch in the lip for the cable, and a window in the rest.
    sketch::Sketch notch(Uuid::generate(), frontPlane());
    (void)sketch::addCenterRectangle(notch, {0, 14}, {6, 21});
    const Uuid notchSketch = b.sketch(std::move(notch), "Cable notch");
    b.add(stand, extrude(notchSketch, {b.profile(notchSketch, {0, 14})}, -(lipW + 1), doc::ExtrudeMode::Cut), "the cable notch");
    sketch::Sketch window(Uuid::generate(), frontPlane());
    (void)sketch::addSlot(window, {0, 36}, {0, 78}, 7);
    const Uuid windowSketch = b.sketch(std::move(window), "Window");
    auto through = extrude(windowSketch, {b.profile(windowSketch, {0, 57})}, -10, doc::ExtrudeMode::Cut);
    through->throughAll = true;
    through->symmetric = true;
    b.add(stand, std::move(through), "the window");

    // The name on the rest, above the window.
    const double z = 97;
    const Vec3 onFront{0, F1.x + (z - base) / std::tan(lean), z};
    auto name = std::make_unique<doc::TextFeature>();
    name->face = b.flatFace(stand, {0, -std::sin(lean), std::cos(lean)}, onFront);
    if (b.ok()) {
        const auto frame = doc::holeFrame(b.shape(stand), b.faceIndex(name->face));
        if (frame)
            name->position = frame->toLocal(onFront);
        else
            b.fail("the rest is not flat");
    }
    name->text = "FoldForm";
    name->size = 7;
    name->depth = -0.8;
    name->font = doc::kTextFontBold;
    b.add(stand, std::move(name), "the name");
    return b.status();
}

// ---------------------------------------------------------------- Honeycomb organizer
// Seven hexagonal cells for pens and brushes, 38 mm across: the middle one
// 100 mm tall, three around it 80, three 60 (each ring of three one hexagon
// and a circular pattern in its sketch); then all seven pockets in one cut,
// from a construction plane 3 mm up (the floor) through the top.
Status honeycombOrganizer(Build& b)
{
    const double cell = 38, pocket = 34, pitch = 36, floor = 3;
    const auto at = [pitch](double degrees) { return Vec2{std::cos(degrees * kDegree), std::sin(degrees * kDegree)} * pitch; };

    sketch::Sketch middle(Uuid::generate(), sketch::Plane::xy());
    (void)hexagon(middle, {0, 0}, cell, 90);
    const Uuid middleSketch = b.sketch(std::move(middle), "Middle cell");
    const Uuid organizer = b.body("Organizer", extrude(middleSketch, {b.profile(middleSketch, {0, 0})}, 100, doc::ExtrudeMode::NewBody));

    const auto ring = [&](double startDegrees, double height, const std::string& name) {
        sketch::Sketch sk(Uuid::generate(), sketch::Plane::xy());
        const auto first = hexagon(sk, at(startDegrees), cell, 90);
        sketch::PatternLayout around;
        around.circular = true;
        around.count = 3;
        (void)sketch::patternCurves(sk, curvesOf(first), around);
        const Uuid id = b.sketch(std::move(sk), name);
        b.add(organizer,
              extrude(id, b.profiles(id, {at(startDegrees), at(startDegrees + 120), at(startDegrees + 240)}), height,
                      doc::ExtrudeMode::Join),
              name);
    };
    ring(30, 80, "Tall cells");
    ring(90, 60, "Short cells");

    const Uuid floorPlane = b.plane(2, floor);
    sketch::Sketch pockets = b.sketchOn(floorPlane);
    (void)hexagon(pockets, {0, 0}, pocket, 90);
    const auto first = hexagon(pockets, at(30), pocket, 90);
    sketch::PatternLayout around;
    around.circular = true;
    around.count = 6;
    (void)sketch::patternCurves(pockets, curvesOf(first), around);
    const Uuid pocketsSketch = b.sketch(std::move(pockets), "Pockets");
    std::vector<Vec2> insides{{0, 0}};
    for (int k = 0; k < 6; ++k)
        insides.push_back(at(30 + 60 * k));
    auto cut = extrude(pocketsSketch, b.profiles(pocketsSketch, insides), 10, doc::ExtrudeMode::Cut);
    cut->throughAll = true;
    b.add(organizer, std::move(cut), "the pockets");
    // The corners rounded, inside and out.
    b.add(organizer, fillet(b.edges(organizer, [](const geom::EdgeInfo& e) {
                                return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.z) - 1) < 1e-9;
                            }),
                            1),
          "the rounded corners");
    return b.status();
}

struct Entry {
    SampleInfo info;
    Status (*build)(Build&);
};

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> list{
        {{"Lamp shade", "A sized wall revolved, a sketch pattern of windows"}, lampShade},
        {{"Twisted vase", "Loft through four turned hexagons, Shell"}, twistedVase},
        {{"Electronics box", "Shell, standoffs, M3 countersunk holes, vents, a lid"}, electronicsBox},
        {{"Phone stand", "A fully sized sketch, fillets, Mirror, engraved text"}, phoneStand},
        {{"Honeycomb organizer", "Polygons, sketch patterns, seven pockets in one cut"}, honeycombOrganizer},
    };
    return list;
}

} // namespace

const std::vector<SampleInfo>& sampleList()
{
    static const std::vector<SampleInfo> list = [] {
        std::vector<SampleInfo> out;
        for (const Entry& e : entries())
            out.push_back(e.info);
        return out;
    }();
    return list;
}

Status buildSample(std::string_view name, doc::Document& document, cmd::UndoStack& stack)
{
    for (const Entry& e : entries())
        if (e.info.name == name) {
            Build b(document, stack);
            return e.build(b);
        }
    return Status::failure(ErrorCode::InvalidArgument, "There is no such sample.", "no sample " + std::string(name));
}

} // namespace os::samples
