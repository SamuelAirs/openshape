// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Reference geometry: Align onto the origin's axes, planes and point (the
// action buttons and the axis lines clicked in the view).

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickWindow>

#include <cmath>

namespace os::app {
namespace {

// Set up (not what is tested): a 20 x 20 x 5 plate from (10, 10, 0) with a
// hole of radius 3 through (20, 20).
bool addHoledPlate(AcceptanceRunner& r)
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
    const bool ok = r.app().interaction().undoStack().push(std::make_unique<cmd::CreateBodyCommand>("Plate", std::move(feature)),
                                                           r.app().document()).ok();
    r.app().interaction().documentChanged();
    r.app().interaction().fitAll(false);
    return ok;
}

const interact::AlignOperation* alignOf(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::AlignOperation*>(r.app().interaction().operation());
}

std::vector<AcceptanceRunner::Step> alignOriginSteps(AcceptanceRunner& r)
{
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    auto box = [&r] { return geom::boundingBox(r.body(0).shape()); };
    auto selectRim = [&r] {
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
        r.click(r.screenPoint(20, 17, 5)); // the near side of the hole's top rim
        const auto& sel = r.app().interaction().selection();
        r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Edge, "the hole's rim is selected");
    };
    auto select = [&r](QPointF at, sel::SelectionKind kind, const QString& what) {
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
        r.click(at);
        const auto& sel = r.app().interaction().selection();
        r.check(sel.size() == 1 && sel.items()[0].kind == kind, what + QStringLiteral(" is selected"));
    };
    return {
        [&r] { r.check(addHoledPlate(r), "align to origin: a plate with a hole"); },
        [] {}, [] {},
        // ---- A hole's axis onto the Z axis (the action button) --------------
        [&r, selectRim] {
            selectRim();
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button");
            r.check(alignOf(r) && !alignOf(r)->hasTarget(), "Align waits for its target", r.app().operationPrompt());
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:z")), "Z axis button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::ZAxis && alignOf(r)->canCommit(),
                    "the Z axis is the target");
            r.screenshot(QStringLiteral("alignorigin_01_z_axis"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.center().x) < 1e-6 && std::abs(bb.center().y) < 1e-6, "Enter: the hole is on the Z axis",
                    num(bb.center().x) + "," + num(bb.center().y));
            r.check(std::abs(bb.min.z) < 1e-6 && std::abs(bb.max.z - 5.0) < 1e-6, "at the height it was", num(bb.min.z));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(box().min.x - 10.0) < 1e-6, "undo puts it back", num(box().min.x));
        },
        // ---- The same by clicking the Z axis line in the view --------------
        [&r, selectRim] {
            selectRim();
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button again");
        },
        [&r] { r.mouseMove(r.screenPoint(0, 0, 18)); },
        [&r] {
            r.check(r.app().interaction().hover().kind == sel::PickKind::OriginAxis
                        && r.app().interaction().hover().index == 2,
                    "hovering the Z axis line shows it as a target");
            r.click(r.screenPoint(0, 0, 18));
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::ZAxis,
                    "clicking the Z axis line aims there");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.center().x) < 1e-6 && std::abs(bb.center().y) < 1e-6, "the hole is on the Z axis again",
                    num(bb.center().x) + "," + num(bb.center().y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- A flat face onto the XZ plane, flipped -------------------------
        [&r] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.click(r.screenPoint(14, 10, 2.5)); // the plate's front (-Y) face
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Face, "the plate's front face is selected");
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the face");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:xz")), "XZ plane button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::XZPlane, "the XZ plane is the target");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("action_flip")), "Flip button"); },
        [&r] {
            r.check(alignOf(r) && alignOf(r)->flipped(), "flipped");
            r.screenshot(QStringLiteral("alignorigin_02_xz_flipped"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.max.y) < 1e-6 && std::abs(bb.min.y + 20.0) < 1e-6,
                    "Enter: the face lies on XZ, the plate turned over to the -Y side", num(bb.min.y) + ".." + num(bb.max.y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- The rim's center onto the origin -------------------------------
        [&r, selectRim] {
            selectRim();
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the rim");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:point")), "Origin button");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.min.x + 10.0) < 1e-6 && std::abs(bb.max.y - 10.0) < 1e-6 && std::abs(bb.max.z) < 1e-6,
                    "Enter: the rim's center is at the origin", num(bb.min.x) + "," + num(bb.max.y) + "," + num(bb.max.z));
            r.screenshot(QStringLiteral("alignorigin_03_origin"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- An edge along X (the X axis button) ----------------------------
        [&r, select] {
            select(r.screenPoint(30, 24, 5), sel::SelectionKind::Edge, "the plate's top right edge (along Y)");
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the edge");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:x")), "X axis button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::XAxis, "the X axis is the target");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            // Turned a quarter about Z: the edge lies on the X axis (y = z = 0
            // on the box's boundary), its middle still at x = 30.
            const bool onAxis = (std::abs(bb.min.y) < 1e-6 || std::abs(bb.max.y) < 1e-6)
                             && (std::abs(bb.min.z) < 1e-6 || std::abs(bb.max.z) < 1e-6);
            r.check(onAxis && std::abs(bb.size().x - 20.0) < 1e-6 && std::abs(bb.size().y - 20.0) < 1e-6
                        && std::abs(bb.size().z - 5.0) < 1e-6 && std::abs(bb.center().x - 30.0) < 1e-6,
                    "Enter: the edge runs along the X axis",
                    num(bb.min.x) + ".." + num(bb.max.x) + " y " + num(bb.min.y) + ".." + num(bb.max.y) + " z "
                        + num(bb.min.z) + ".." + num(bb.max.z));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- The hole's axis onto Y: the plate stands up --------------------
        [&r, selectRim] {
            selectRim();
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the rim (Y)");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:y")), "Y axis button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::YAxis, "the Y axis is the target");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            // The rim's center (20, 20, 5) lands at (0, 20, 0); the plate's
            // 5 mm now run along Y on one side of it.
            const bool atRim = std::abs(bb.min.y - 20.0) < 1e-6 || std::abs(bb.max.y - 20.0) < 1e-6;
            r.check(atRim && std::abs(bb.center().x) < 1e-6 && std::abs(bb.center().z) < 1e-6
                        && std::abs(bb.size().y - 5.0) < 1e-6 && std::abs(bb.size().z - 20.0) < 1e-6,
                    "Enter: the hole's axis is the Y axis",
                    num(bb.center().x) + "," + num(bb.center().z) + " y " + num(bb.min.y) + ".." + num(bb.max.y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- The front face onto XY: the plate stands on it ------------------
        [&r, select] {
            select(r.screenPoint(14, 10, 2.5), sel::SelectionKind::Face, "the plate's front face (for XY)");
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the front face");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:xy")), "XY plane button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::XYPlane, "the XY plane is the target");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.min.z) < 1e-6 && std::abs(bb.max.z - 20.0) < 1e-6 && std::abs(bb.min.y - 7.5) < 1e-6
                        && std::abs(bb.max.y - 12.5) < 1e-6 && std::abs(bb.min.x - 10.0) < 1e-6,
                    "Enter: the front face lies on XY, the plate standing above it",
                    "z " + num(bb.min.z) + ".." + num(bb.max.z) + " y " + num(bb.min.y) + ".." + num(bb.max.y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        // ---- The right face onto YZ: turned to face it ----------------------
        [&r, select] {
            select(r.screenPoint(30, 16, 2.5), sel::SelectionKind::Face, "the plate's right face (for YZ)");
            r.check(r.clickItem(QStringLiteral("tool_align")), "Align tool button for the right face");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("barAction_origin:yz")), "YZ plane button");
            r.check(alignOf(r) && alignOf(r)->originTarget() == interact::OriginTarget::YZPlane, "the YZ plane is the target");
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num, box] {
            const auto bb = box();
            r.check(std::abs(bb.min.x) < 1e-6 && std::abs(bb.max.x - 20.0) < 1e-6 && std::abs(bb.min.y - 10.0) < 1e-6
                        && std::abs(bb.max.y - 30.0) < 1e-6 && std::abs(bb.min.z) < 1e-6 && std::abs(bb.max.z - 5.0) < 1e-6,
                    "Enter: the right face lies on YZ (half a turn), the plate on the +X side",
                    "x " + num(bb.min.x) + ".." + num(bb.max.x));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(box().min.x - 10.0) < 1e-6 && std::abs(box().min.y - 10.0) < 1e-6, "undo: where it started");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("alignorigin"), 60, alignOriginSteps});

} // namespace
} // namespace os::app
