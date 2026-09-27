// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Construction axes and planes: every way the Axis and Plane tools make them
// (clicked in the palette, the action bar and the view), the tools that use
// them (Pattern, Rotate, Mirror, Align, Sketch), their Model panel rows
// (select, change the distance, hide, show, delete) and Delete in the view.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "document/Datum.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

// Set up (not what is tested): a 20 x 20 x 5 plate from (10, 10, 0) with a
// hole of radius 3 through (20, 20), and a 4 mm block at (35, 18, 0).
bool addPlateAndBlock(AcceptanceRunner& r)
{
    const auto plate = geom::makeBox({10, 10, 0}, {20, 20, 5});
    const auto pin = geom::makeCylinder({20, 20, -1}, {0, 0, 1}, 3.0, 7.0);
    if (!plate || !pin)
        return false;
    const auto holed = geom::booleanOp(plate.value(), pin.value(), geom::BooleanKind::Subtract);
    if (!holed)
        return false;
    auto feature = std::make_unique<doc::ImportedFeature>();
    feature->setShape(holed.value());
    feature->source = "plate";
    auto& stack = r.app().interaction().undoStack();
    bool ok = stack.push(std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(feature)), r.app().document()).ok();
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = {35, 18, 0};
    box->size = {4, 4, 4};
    ok = ok && stack.push(std::make_unique<cmd::CreateBodyCommand>("Block", std::move(box)), r.app().document()).ok();
    r.app().interaction().documentChanged();
    r.app().interaction().fitAll(false);
    return ok;
}

const interact::DatumOperation* constructOf(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::DatumOperation*>(r.app().interaction().operation());
}

const std::vector<std::unique_ptr<doc::Datum>>& datums(AcceptanceRunner& r)
{
    return r.app().document().datums();
}

QString idText(const Uuid& id)
{
    return QString::fromStdString(id.toString());
}

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

// Where to click a construction axis in the view: points along its drawn
// line from its upper end down, the first one a click reaches.
QPointF onAxis(AcceptanceRunner& r, const doc::Datum& axis)
{
    const auto shape = r.app().interaction().datumShape(axis.geometry(), axis.kind());
    const Vec3 top = shape.a.z >= shape.b.z ? shape.a : shape.b;
    const Vec3 bottom = shape.a.z >= shape.b.z ? shape.b : shape.a;
    std::vector<Vec3> candidates;
    for (double t : {0.15, 0.25, 0.1, 0.3})
        candidates.push_back(top + (bottom - top) * t);
    return r.uncoveredScreenPoint(candidates);
}

// A plane's outline: its highest side (nothing stands in front of it), the
// middle first.
QPointF onPlaneOutline(AcceptanceRunner& r, const doc::Datum& plane)
{
    const auto shape = r.app().interaction().datumShape(plane.geometry(), plane.kind());
    int side = 0;
    for (int i = 1; i < 4; ++i)
        if (shape.corners[i].z + shape.corners[(i + 1) % 4].z > shape.corners[side].z + shape.corners[(side + 1) % 4].z + 1e-9)
            side = i;
    const Vec3 a = shape.corners[side], b = shape.corners[(side + 1) % 4];
    return r.uncoveredScreenPoint({(a + b) * 0.5, a + (b - a) * 0.3, a + (b - a) * 0.7});
}

double volumeOf(AcceptanceRunner& r, std::size_t body)
{
    return geom::volume(r.body(body).shape());
}

std::vector<AcceptanceRunner::Step> constructSteps(AcceptanceRunner& r)
{
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    auto clear = [&r] {
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
    };
    auto selectBlock = [&r] {
        r.check(r.clickItem(QStringLiteral("historyRow_") + idText(r.body(1).id())), "the block selected in the Model panel");
    };
    // Ids of what the scenario made: the axis through the hole, the plane above the plate.
    auto state = std::make_shared<std::pair<Uuid, Uuid>>();
    return {
        [&r] { r.check(addPlateAndBlock(r), "construct: a plate with a hole and a block"); },
        [] {}, [] {},
        // ---- Axis through the hole: the selected rim is its first pick ------
        [&r, clear] {
            clear();
            r.click(r.screenPoint(20, 17, 5)); // the near side of the hole's top rim
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Edge, "the hole's rim is selected");
            r.check(r.clickItem(QStringLiteral("tool_axis")), "Axis tool button");
            r.check(constructOf(r) && constructOf(r)->preview().has_value(), "the rim makes the axis at once");
            r.key(Qt::Key_Return);
        },
        [&r, state] {
            r.check(datums(r).size() == 1, "Enter adds the axis");
            if (datums(r).size() != 1)
                return;
            const doc::Datum& axis = *datums(r).front();
            state->first = axis.id();
            r.check(axis.method == doc::DatumMethod::AxisThrough && near(axis.geometry().origin.x, 20)
                        && near(axis.geometry().origin.y, 20) && near(std::abs(axis.geometry().direction.z), 1),
                    "the axis goes through the hole along Z");
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Datum, "the new axis is selected");
            r.screenshot(QStringLiteral("construct_01_axis_through_hole"));
        },
        // ---- Pattern the block around it (clicking the axis) -----------------
        [&r, clear, selectBlock] {
            clear();
            selectBlock();
        },
        [&r] { r.check(r.clickItem(QStringLiteral("tool_pattern")), "Pattern tool button"); },
        [&r] { r.check(r.clickItem(QStringLiteral("action_layout:circular")), "Circular"); },
        [&r] { r.mouseMove(onAxis(r, *datums(r).front())); },
        [&r] {
            r.check(r.app().interaction().hover().kind == sel::PickKind::Datum, "hovering the axis shows it as a target");
            r.click(onAxis(r, *datums(r).front()));
            const auto* pattern = dynamic_cast<const interact::PatternOperation*>(r.app().interaction().operation());
            r.check(pattern && pattern->circular() && pattern->axisIndex() == -1, "clicking the axis makes it the pattern's axis");
        },
        [&r] { r.key(Qt::Key_Return); },
        [] {},
        [&r, num] {
            const auto bb = geom::boundingBox(r.body(1).shape());
            r.check(near(volumeOf(r, 1), 6 * 64.0), "six blocks around the axis", num(volumeOf(r, 1)));
            r.check(near(bb.center().x, 20) && near(bb.center().y, 20), "centered on the hole",
                    num(bb.center().x) + "," + num(bb.center().y));
            r.screenshot(QStringLiteral("construct_02_pattern_around_axis"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(near(volumeOf(r, 1), 64.0), "undo: one block again", num(volumeOf(r, 1)));
        },
        // ---- Rotate the block a quarter turn about the axis ------------------
        [&r, clear, selectBlock] {
            clear();
            selectBlock();
        },
        [&r] { r.check(r.clickItem(QStringLiteral("tool_rotate")), "Rotate tool button"); },
        [&r] {
            r.click(onAxis(r, *datums(r).front()));
            const auto* rotate = dynamic_cast<const interact::RotateOperation*>(r.app().interaction().operation());
            r.check(rotate && rotate->axis() && near(std::abs(rotate->axis()->z), 1), "clicking the axis turns about it");
            r.type(QStringLiteral("90"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            const auto bb = geom::boundingBox(r.body(1).shape());
            r.check(near(bb.center().x, 20) && near(bb.center().y, 37), "the block turned about the hole's axis",
                    num(bb.center().x) + "," + num(bb.center().y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- Offset plane -> sketch on it -> extrude ------------------------
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_plane")), "Plane tool button");
            r.check(constructOf(r) && !r.app().operationPrompt().isEmpty(), "the Plane tool asks for a face",
                    r.app().operationPrompt());
            r.click(r.screenPoint(14, 14, 5)); // the plate's top
            r.check(constructOf(r) && constructOf(r)->preview().has_value() && r.app().valueLabelVisible(),
                    "the top face: a plane on it with a distance to type");
            r.type(QStringLiteral("10"));
        },
        [&r] { r.key(Qt::Key_Return); },
        [&r, state, num] {
            r.check(datums(r).size() == 2, "Enter adds the plane");
            if (datums(r).size() != 2)
                return;
            const doc::Datum& plane = *datums(r).back();
            state->second = plane.id();
            r.check(plane.method == doc::DatumMethod::PlaneOffset && near(plane.geometry().origin.z, 15)
                        && near(plane.geometry().direction.z, 1),
                    "10 mm above the plate", num(plane.geometry().origin.z));
            r.check(r.clickItem(QStringLiteral("barAction_sketch")), "Sketch on the new plane");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.app().sketchMode(), "sketching on the plane");
            r.click(r.screenPoint(12, 12, 15));
            r.mouseMove(r.screenPoint(15, 14, 15));
            r.type(QStringLiteral("6"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("4"));
            r.key(Qt::Key_Return);
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->sketch().lines().size() == 4, "a 6 x 4 rectangle on the plane");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("finishSketchButton")), "Finish sketch"); },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(15, 14, 15));
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::SketchProfile, "the rectangle's profile");
            r.type(QStringLiteral("5"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            r.check(r.app().bodyCount() == 3, "the extrusion is a new body");
            if (r.app().bodyCount() != 3)
                return;
            const auto bb = geom::boundingBox(r.body(2).shape());
            r.check(near(volumeOf(r, 2), 120.0) && near(bb.min.z, 15) && near(bb.max.z, 20),
                    "6 x 4 x 5 standing on the plane", num(bb.min.z) + ".." + num(bb.max.z));
            r.screenshot(QStringLiteral("construct_03_sketch_on_plane"));
        },
        // The plane's distance in the Model panel: the sketch and the block follow.
        [&r, state] { r.check(r.clickItem(QStringLiteral("historyRow_") + idText(state->second)), "the plane's Model panel row"); },
        [&r, state] {
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Datum, "its row selects the plane");
            r.check(r.clickItem(QStringLiteral("historyParam_") + idText(state->second) + QStringLiteral("_distance")),
                    "the plane's distance field");
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            const auto bb = geom::boundingBox(r.body(2).shape());
            r.check(near(bb.min.z, 25) && near(bb.max.z, 30), "20 mm: the sketch and the block moved up with the plane",
                    num(bb.min.z));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(near(geom::boundingBox(r.body(2).shape()).min.z, 15), "undo: back at 10 mm",
                    num(geom::boundingBox(r.body(2).shape()).min.z));
            r.app().interaction().fitAll(false); // the view kept the sketch's zoom
        },
        // ---- Align the block's top face onto the plane, then onto the axis ---
        [&r, clear] {
            clear();
            r.click(r.screenPoint(37, 19, 4)); // the block's top
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Face, "the block's top face");
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button");
        },
        [&r, state] {
            const doc::Datum* plane = r.app().document().datum(state->second);
            r.check(plane != nullptr, "the plane is there");
            if (!plane)
                return;
            r.click(onPlaneOutline(r, *plane));
            const auto* align = dynamic_cast<const interact::AlignOperation*>(r.app().interaction().operation());
            r.check(align && align->datumTarget() == state->second, "clicking the plane's outline aims at it");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            const auto bb = geom::boundingBox(r.body(1).shape());
            r.check(near(bb.max.z, 15) && near(bb.min.z, 11), "the top face touches the plane from below",
                    num(bb.min.z) + ".." + num(bb.max.z));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- Hole onto Z (Align's origin axis) ------------------------------
        [&r, clear] {
            clear();
            r.click(r.screenPoint(20, 17, 5));
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the rim");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:z")), "Z axis button");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, state, num] {
            const auto bb = geom::boundingBox(r.body(0).shape());
            r.check(near(bb.center().x, 0) && near(bb.center().y, 0), "the hole is on the Z axis",
                    num(bb.center().x) + "," + num(bb.center().y));
            // Made from the plate's first step: edits of that step move it, a
            // later step (this Align) does not (as for a sketch on a face).
            const doc::Datum* axis = r.app().document().datum(state->first);
            r.check(axis && near(axis->geometry().origin.x, 20) && near(axis->geometry().origin.y, 20),
                    "the axis stays where it was made");
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- Mirror the block across a plane 1 mm right of it (from YZ) ------
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_plane")), "Plane tool for an origin plane");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:origin:0")), "From YZ"); },
        [&r] {
            r.type(QStringLiteral("40"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            const doc::Datum& plane = *datums(r).back();
            r.check(datums(r).size() == 3 && plane.originIndex == 0 && near(plane.geometry().origin.x, 40),
                    "a plane 40 mm from YZ", num(plane.geometry().origin.x));
        },
        [&r, clear, selectBlock] {
            clear();
            selectBlock();
        },
        [&r] { r.check(r.clickItem(QStringLiteral("tool_mirror")), "Mirror tool button"); },
        [&r] {
            r.click(onPlaneOutline(r, *datums(r).back()));
            const auto* mirror = dynamic_cast<const interact::MirrorOperation*>(r.app().interaction().operation());
            r.check(mirror && mirror->hasPlane(), "clicking the plane's outline mirrors across it");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            double total = 0, maxX = -1e9;
            for (const auto& body : r.app().document().bodies()) {
                total += geom::volume(body->shape());
                maxX = std::max(maxX, geom::boundingBox(body->shape()).max.x);
            }
            r.check(near(total, 2000.0 - kPi * 9.0 * 5.0 + 64.0 * 2 + 120.0), "the block's image was added",
                    num(total));
            r.check(near(maxX, 45), "mirrored across x = 40 (35..39 -> 41..45)", num(maxX));
            r.screenshot(QStringLiteral("construct_04_mirror_across_plane"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- The other ways to make an axis ----------------------------------
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_axis")), "Axis tool: along an edge");
            r.click(r.screenPoint(30, 10, 2.5)); // the plate's front right vertical edge
            r.check(constructOf(r) && constructOf(r)->preview().has_value(), "a straight edge makes it");
            r.key(Qt::Key_Return);
        },
        [&r] {
            const doc::Datum& axis = *datums(r).back();
            r.check(axis.method == doc::DatumMethod::AxisAlongEdge && near(axis.geometry().origin.x, 30)
                        && near(axis.geometry().origin.y, 10) && near(std::abs(axis.geometry().direction.z), 1),
                    "an axis along the edge");
        },
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_axis")), "Axis tool: two points");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:twoPoints")), "Two points"); },
        [&r] {
            r.click(r.screenPoint(11.5, 10, 5)); // the top front edge near (10, 10, 5)
            r.click(r.screenPoint(30, 28.5, 5)); // the top right edge near (30, 30, 5)
            r.check(constructOf(r) && constructOf(r)->preview().has_value(), "two corners make it");
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            const doc::Datum& axis = *datums(r).back();
            const Vec3 o = axis.geometry().origin, d = axis.geometry().direction;
            r.check(axis.method == doc::DatumMethod::AxisTwoPoints && near(o.x, 20) && near(o.y, 20) && near(o.z, 5)
                        && near(std::abs(d.x), std::sqrt(0.5)) && near(std::abs(d.y), std::sqrt(0.5)),
                    "an axis across the plate's top, corner to corner", num(o.x) + "," + num(o.y) + "," + num(o.z));
        },
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_axis")), "Axis tool: parallel to Z");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:parallel:2")), "Parallel to Z"); },
        [&r] {
            r.click(r.screenPoint(28.5, 10, 5)); // the top front edge near (30, 10, 5)
            r.key(Qt::Key_Return);
        },
        [&r] {
            const doc::Datum& axis = *datums(r).back();
            r.check(axis.method == doc::DatumMethod::AxisParallel && near(axis.geometry().origin.x, 30)
                        && near(axis.geometry().origin.y, 10) && near(axis.geometry().origin.z, 5)
                        && near(axis.geometry().direction.z, 1),
                    "parallel to Z through the corner");
        },
        // ---- The other ways to make a plane ----------------------------------
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_plane")), "Plane tool: at an angle");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:angle")), "At angle"); },
        [&r] {
            r.click(r.screenPoint(20, 10, 5)); // the top front edge: a face along it is taken with it
            r.check(constructOf(r) && constructOf(r)->picked().size() == 2 && r.app().valueLabelVisible(),
                    "the edge and a face along it, an angle to type");
            r.click(r.screenPoint(14, 16, 5)); // the angle is measured from the top face
            r.check(constructOf(r) && constructOf(r)->picked().size() == 2, "the top face instead");
            r.type(QStringLiteral("90"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            const doc::Datum& plane = *datums(r).back();
            r.check(plane.method == doc::DatumMethod::PlaneAngle && near(std::abs(plane.geometry().direction.y), 1)
                        && near(plane.geometry().origin.y, 10) && near(plane.geometry().origin.z, 5),
                    "standing up on the front edge", num(plane.geometry().direction.y));
        },
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("tool_plane")), "Plane tool: midway");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:midway")), "Midway"); },
        [&r] {
            r.click(r.screenPoint(14, 14, 5));   // the plate's top (z = 5)
            r.click(r.screenPoint(37, 19.5, 4)); // the block's top (z = 4)
            r.check(constructOf(r) && constructOf(r)->preview().has_value(), "two parallel faces make it");
            r.check(r.clickItem(QStringLiteral("barAction_apply")), "Apply");
        },
        [&r, num] {
            const doc::Datum& plane = *datums(r).back();
            r.check(plane.method == doc::DatumMethod::PlaneMidway && near(plane.geometry().origin.z, 4.5)
                        && near(std::abs(plane.geometry().direction.z), 1),
                    "midway between them", num(plane.geometry().origin.z));
            r.check(datums(r).size() == 8, "eight axes and planes", QString::number(datums(r).size()));
            r.screenshot(QStringLiteral("construct_05_all"));
        },
        // The palette's Sketch button with a plane selected sketches on it.
        [&r, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("historyRow_") + idText(datums(r).back()->id())), "the midway plane's row");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("sketchButton")), "Sketch in the palette"); },
        [] {}, [] {}, [] {},
        [&r] {
            const auto* session = r.app().interaction().sketchSession();
            const sketch::Sketch* sk = session ? r.app().document().sketch(session->sketchId()) : nullptr;
            r.check(sk && sk->datumPlane() == datums(r).back()->id() && near(sk->plane().origin.z, 4.5),
                    "a sketch on the selected plane");
            r.check(r.clickItem(QStringLiteral("finishSketchButton")), "Finish (the empty sketch goes away)");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.app().sketchCount() == 1, "only the first sketch is kept", QString::number(r.app().sketchCount()));
            r.app().interaction().fitAll(false);
        },
        // ---- Model panel: hide, show, delete; Delete on a selected axis -------
        [&r, state, clear] {
            clear();
            r.check(r.clickItem(QStringLiteral("historyRow_") + idText(state->first)), "the axis's Model panel row");
        },
        [&r, state] { r.check(r.clickItem(QStringLiteral("historyVisibility_") + idText(state->first)), "Hide"); },
        [&r, state] {
            const doc::Datum* axis = r.app().document().datum(state->first);
            r.check(axis && !axis->isVisible(), "hidden");
            r.check(r.clickItem(QStringLiteral("historyVisibility_") + idText(state->first)), "Show");
        },
        [&r, state] {
            const doc::Datum* axis = r.app().document().datum(state->first);
            r.check(axis && axis->isVisible(), "shown again");
            r.check(r.clickItem(QStringLiteral("historyDelete_") + idText(state->first)), "Delete in its row");
        },
        [&r, state] {
            r.check(r.app().document().datum(state->first) == nullptr, "deleted");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().document().datum(state->first) != nullptr, "undo brings it back");
        },
        [&r, state, clear] {
            clear();
            const doc::Datum* axis = r.app().document().datum(state->first);
            if (!axis)
                return;
            r.click(onAxis(r, *axis));
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Datum && sel.items()[0].bodyId == state->first,
                    "clicking the axis in the view selects it");
            r.key(Qt::Key_Delete);
            r.check(r.app().document().datum(state->first) == nullptr, "Delete removes it");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().document().datum(state->first) != nullptr, "undo again");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("construct"), 61, constructSteps});

} // namespace
} // namespace os::app
