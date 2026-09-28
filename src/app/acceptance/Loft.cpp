// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Loft, clicked through the real UI: a rectangle sketched on the ground, a
// construction plane 30 mm above it (Construct → Plane), a circle sketched on
// that plane; the rectangle clicked and the circle Shift-clicked (on the
// rectangle's extrusion arrow where it runs over the circle), Loft, Straight,
// Apply; the plane's distance changed in the Model panel (the loft follows),
// Smooth chosen in the Model panel, and undo. Then the same with a finger:
// taps, Loft, Apply, in the touch layout's words. Then New body / Join / Cut:
// a square on a block's top face lofted to one under the block (joins by
// itself; Cut, New body and Cut clicked; a chosen square Shift-clicked out
// and back in; Apply; Join in the Model panel; undo).

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "document/Body.h"
#include "document/Datum.h"
#include "document/Document.h"
#include "document/SketchProfiles.h"
#include "geometry/Loft.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <iterator>
#include <memory>
#include <optional>

namespace os::app {
namespace {

QString idText(const Uuid& id)
{
    return QString::fromStdString(id.toString());
}

bool closeTo(double a, double b, double relative = 1e-6)
{
    return std::abs(a - b) <= relative * std::max({std::abs(a), std::abs(b), 1.0});
}

// The enclosed volume of a closed triangle mesh (the preview as shown).
double meshVolume(const geom::Mesh& mesh)
{
    double v = 0;
    for (std::size_t t = 0; t + 2 < mesh.indices.size(); t += 3)
        v += mesh.vertex(mesh.indices[t]).dot(mesh.vertex(mesh.indices[t + 1]).cross(mesh.vertex(mesh.indices[t + 2])));
    return std::abs(v) / 6;
}

// Where to click so that the view picks the profile of sketch `index` (in
// document order): the first candidate that reaches the view (not a panel)
// and picks it there.
QPointF profilePoint(AcceptanceRunner& r, std::size_t index, const std::vector<Vec3>& candidates)
{
    const auto& sketches = r.app().document().sketches();
    const Uuid sketch = index < sketches.size() ? sketches[index]->id() : Uuid();
    for (const Vec3& c : candidates) {
        const QPointF p = r.uncoveredScreenPoint({c});
        const auto device = r.app().touchMode() ? interact::PointerDevice::Touch : interact::PointerDevice::Mouse;
        const auto hit = r.app().interaction().pickAt({p.x(), p.y()}, interact::InputProfile::forDevice(device));
        if (hit.kind == sel::PickKind::Profile && hit.bodyId == sketch && r.itemAt(p)
            && r.itemAt(p)->objectName() == QLatin1String("viewport"))
            return p;
    }
    return r.screenPoint(candidates.front().x, candidates.front().y, candidates.front().z);
}

// The exact loft of the two sketches' profiles, computed apart from the UI
// (what the step must have made).
double expectedVolume(AcceptanceRunner& r, bool ruled)
{
    const auto& sketches = r.app().document().sketches();
    if (sketches.size() < 2)
        return -1;
    std::vector<geom::Shape> faces;
    for (std::size_t i = 0; i < 2; ++i) {
        auto regions = doc::sketchRegions(*sketches[i]);
        if (!regions || regions.value().empty())
            return -1;
        faces.push_back(regions.value().front().face);
    }
    auto loft = geom::loftFaces(faces, ruled);
    return loft ? geom::volume(loft.value()) : -1;
}

// A point on the armed extrusion's arrow (within its shaft and head) where
// the view shows the profile of sketch `index` under it, if the arrow runs
// over that profile on screen.
std::optional<QPointF> arrowPointOver(AcceptanceRunner& r, std::size_t index)
{
    const auto* op = r.app().interaction().operation();
    const auto& sketches = r.app().document().sketches();
    if (!op || op->handleCount() < 1 || index >= sketches.size())
        return std::nullopt;
    const auto& camera = r.app().interaction().camera();
    const interact::LinearManipulator arrow = op->handle(0);
    const Vec3 base = arrow.anchor(op->handleOffset(0));
    const double length = interact::ArrowStyle{}.totalPx() * camera.pixelSize(base);
    const auto device = r.app().touchMode() ? interact::PointerDevice::Touch : interact::PointerDevice::Mouse;
    for (double t = 0.2; t <= 0.95; t += 0.05) {
        const Vec2 s = camera.project(base + arrow.direction() * (length * t));
        const QPointF p(s.x, s.y);
        const auto hit = r.app().interaction().pickAt(s, interact::InputProfile::forDevice(device));
        if (hit.kind == sel::PickKind::Profile && hit.bodyId == sketches[index]->id() && r.itemAt(p)
            && r.itemAt(p)->objectName() == QLatin1String("viewport"))
            return p;
    }
    return std::nullopt;
}

// Zoomed out (the wheel, at the loft's middle) until the 30 mm between the
// profiles is shorter on screen than an arrow: the rectangle's extrusion
// arrow then runs over the circle, as it does in a small window or a view
// from further away.
void zoomOutUnderArrow(AcceptanceRunner& r)
{
    auto& in = r.app().interaction();
    const double arrow = interact::ArrowStyle{}.totalPx();
    for (int i = 0; i < 30 && (in.camera().project({15, 10, 30}) - in.camera().project({15, 10, 0})).length() > 0.6 * arrow; ++i)
        in.wheel(in.camera().project({15, 10, 15}), -1);
}

// A finger's tap on the middle of a QML item (a button).
bool tapItem(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return false;
    r.touchTap({item->mapToScene(QPointF(item->width() / 2, item->height() / 2))});
    return true;
}

QString hint(AcceptanceRunner& r)
{
    QQuickItem* line = r.findItem(QStringLiteral("hintText"));
    return line ? line->property("text").toString() : QString();
}

const doc::LoftFeature* loftStep(AcceptanceRunner& r)
{
    const auto& bodies = r.app().document().bodies();
    if (bodies.empty() || bodies.front()->features().empty())
        return nullptr;
    return dynamic_cast<const doc::LoftFeature*>(bodies.front()->features().front().get());
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    // The plane's id, the loft's volume with the plane at 30.
    struct State {
        Uuid plane;
        double volume = 0;
    };
    auto state = std::make_shared<State>();
    return {
        // ---- The palette's Loft says what to select --------------------------
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_loft")), "loft: Loft in the tools");
            QQuickItem* toast = r.findItem(QStringLiteral("toast"));
            const QString text = toast ? toast->property("text").toString() : QString();
            r.check(text.contains(QStringLiteral("profiles on other planes")), "loft: it says what to select", text);
            r.check(!r.app().operationActive(), "loft: nothing to loft yet");
        },
        // ---- A 30 x 20 rectangle on the ground -------------------------------
        [&r] { r.check(r.clickItem(QStringLiteral("sketchButton")), "loft: Sketch in the palette"); },
        [&r] { r.check(r.clickItem(QStringLiteral("planeTop")), "loft: on the ground (Top)"); },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.check(r.app().sketchMode(), "loft: sketching on the ground");
            r.click(r.screenPoint(0, 0, 0));
            r.mouseMove(r.screenPoint(12, 8, 0));
            r.type(QStringLiteral("30"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Return);
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->sketch().lines().size() == 4, "loft: a 30 x 20 rectangle");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("finishSketchButton")), "loft: Finish sketch"); },
        [] {}, [] {}, [] {}, [] {},
        // ---- Construct → Plane, 30 mm above the ground ------------------------
        [&r] {
            r.key(Qt::Key_Escape);
            r.check(r.clickItem(QStringLiteral("tool_plane")), "loft: Plane in the tools");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_datum:origin:2")), "loft: From XY"); },
        [&r] { r.type(QStringLiteral("30")); },
        [&r] { r.key(Qt::Key_Return); },
        [&r, state, num] {
            const auto& datums = r.app().document().datums();
            r.check(datums.size() == 1, "loft: the plane is added");
            if (datums.size() != 1)
                return;
            state->plane = datums.front()->id();
            r.check(closeTo(datums.front()->geometry().origin.z, 30), "loft: 30 above the ground",
                    num(datums.front()->geometry().origin.z));
            r.check(r.clickItem(QStringLiteral("barAction_sketch")), "loft: Sketch on the plane");
        },
        [] {}, [] {}, [] {}, [] {},
        // ---- A circle of 10 mm on it, over the rectangle's middle -------------
        [&r] {
            r.check(r.app().sketchMode(), "loft: sketching on the plane");
            r.check(r.clickItem(QStringLiteral("tool_circle")), "loft: the Circle tool");
        },
        [&r] {
            r.click(r.screenPoint(15, 10, 30));
            r.mouseMove(r.screenPoint(18, 10, 30));
            r.type(QStringLiteral("10"));
            r.key(Qt::Key_Return);
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->sketch().circles().size() == 1, "loft: a circle on the plane");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("finishSketchButton")), "loft: Finish sketch"); },
        [] {}, [] {}, [] {}, [] {},
        // ---- Click the rectangle, Shift-click the circle ----------------------
        [&r] {
            r.key(Qt::Key_Escape);
            r.check(r.app().sketchCount() == 2, "loft: two sketches", QString::number(r.app().sketchCount()));
            zoomOutUnderArrow(r);
            r.click(profilePoint(r, 0, {{15, 10, 0}, {15, 8, 0}, {13, 10, 0}, {4, 4, 0}, {26, 4, 0}}));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "loft: the rectangle is selected (extrude armed)",
                    r.app().operationTitle());
        },
        [&r] {
            // The rectangle was clicked under the circle, so its arrow (which
            // starts where a profile is clicked) points up over the circle on
            // screen: Shift-clicked on the arrow there, the circle is picked.
            const auto onArrow = arrowPointOver(r, 1);
            r.click(onArrow.value_or(profilePoint(r, 1, {{15, 10, 30}, {17, 11, 30}, {13, 9, 30}})), Qt::ShiftModifier);
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 2 && sel.allOfKind(sel::SelectionKind::SketchProfile), "loft: both profiles selected",
                    onArrow ? QStringLiteral("clicked on the rectangle's arrow") : QStringLiteral("the arrow is not over the circle"));
            r.check(onArrow.has_value(), "loft: the click went through the rectangle's arrow");
            r.check(!r.app().operationActive(), "loft: no extrusion for two sketches' profiles");
            r.check(hint(r).startsWith(QStringLiteral("Loft joins these profiles")), "loft: the hint says what Loft does", hint(r));
            r.app().interaction().fitAll(false);
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_loft")), "loft: Loft in the selection's actions"); },
        [] {}, [] {},
        [&r, num] {
            r.check(r.app().operationTitle() == QStringLiteral("Loft"), "loft: Loft armed", r.app().operationTitle());
            r.check(r.app().operationError().isEmpty() && r.app().operationCanCommit(), "loft: it previews",
                    r.app().operationError());
            const auto* op = dynamic_cast<const interact::LoftOperation*>(r.app().interaction().operation());
            const double expected = expectedVolume(r, false);
            const double shown = op && op->previewMesh() ? meshVolume(*op->previewMesh()) : 0.0;
            r.check(op && !op->ruled() && expected > 0 && closeTo(shown, expected, 0.01),
                    "loft: the smooth preview holds the loft's volume", num(shown) + QStringLiteral(" vs ") + num(expected));
            r.screenshot(QStringLiteral("loft_01_preview"));
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_loft:straight")), "loft: Straight"); },
        [] {}, [] {},
        [&r, num] {
            const auto* op = dynamic_cast<const interact::LoftOperation*>(r.app().interaction().operation());
            const double expected = expectedVolume(r, true);
            const double shown = op && op->previewMesh() ? meshVolume(*op->previewMesh()) : 0.0;
            r.check(op && op->ruled() && expected > 0 && closeTo(shown, expected, 0.01),
                    "loft: the straight preview holds the loft's volume (two profiles: as smooth)",
                    num(shown) + QStringLiteral(" vs ") + num(expected));
            r.check(r.clickItem(QStringLiteral("barAction_apply")), "loft: Apply");
        },
        [] {},
        [&r, state, num] {
            r.check(r.app().bodyCount() == 1, "loft: a new body");
            if (r.app().bodyCount() != 1)
                return;
            state->volume = r.bodyVolume();
            const double expected = expectedVolume(r, true);
            r.check(closeTo(state->volume, expected), "loft: the body is exactly the straight loft",
                    num(state->volume) + QStringLiteral(" vs ") + num(expected));
            // Between the cone from the circle inscribed in the rectangle's
            // short side and the rectangle's prism.
            const double h = 30, circle = kPi * 25;
            r.check(state->volume > h / 3 * (kPi * 100 + circle + std::sqrt(kPi * 100 * circle)) && state->volume < 600 * h,
                    "loft: a sensible volume", num(state->volume));
            const auto box = geom::boundingBox(r.body(0).shape());
            r.check(closeTo(box.min.x, 0) && closeTo(box.max.x, 30) && closeTo(box.min.y, 0) && closeTo(box.max.y, 20)
                        && closeTo(box.min.z, 0) && closeTo(box.max.z, 30),
                    "loft: from the rectangle up to the circle",
                    num(box.min.x) + QStringLiteral("..") + num(box.max.x) + QStringLiteral(", z ") + num(box.max.z));
            const auto* step = loftStep(r);
            r.check(step && step->ruled && step->sections.size() == 2, "loft: a Straight loft step of two profiles");
            r.screenshot(QStringLiteral("loft_02_applied"));
        },
        // ---- The plane's distance in the Model panel: the loft follows -------
        [&r, state] { r.check(r.clickItem(QStringLiteral("historyRow_") + idText(state->plane)), "loft: the plane's Model panel row"); },
        [&r, state] {
            r.check(r.clickItem(QStringLiteral("historyParam_") + idText(state->plane) + QStringLiteral("_distance")),
                    "loft: the plane's distance field");
            r.type(QStringLiteral("40"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, state, num] {
            // Parallel profiles: the volume grows with the height.
            const double v = r.bodyVolume();
            r.check(closeTo(v, state->volume * 40 / 30), "loft: 40 mm apart, the loft follows the plane",
                    num(v) + QStringLiteral(" vs ") + num(state->volume * 40 / 30));
            r.check(closeTo(r.bodyHeight(), 40), "loft: 40 mm tall", num(r.bodyHeight()));
            r.screenshot(QStringLiteral("loft_03_followed"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r, state, num] {
            r.check(closeTo(r.bodyVolume(), state->volume) && closeTo(r.bodyHeight(), 30), "loft: undo, 30 apart again",
                    num(r.bodyVolume()));
            r.app().interaction().fitAll(false); // the view kept the sketch's zoom
        },
        // ---- Smooth in the Model panel (two profiles: the same solid) --------
        [&r] {
            const auto* step = loftStep(r);
            r.check(step != nullptr, "loft: its step");
            if (step)
                r.check(r.clickItem(QStringLiteral("historyRow_") + idText(step->id())), "loft: the loft step's row");
        },
        [&r] {
            const auto* step = loftStep(r);
            if (step)
                r.check(r.clickItem(QStringLiteral("historyChoice_") + idText(step->id()) + QStringLiteral("_sections_Smooth")),
                        "loft: Smooth in the Model panel");
        },
        [] {},
        [&r, state, num] {
            const auto* step = loftStep(r);
            r.check(step && !step->ruled, "loft: the step is smooth now");
            r.check(closeTo(r.bodyVolume(), state->volume, 1e-5), "loft: same volume (two profiles)", num(r.bodyVolume()));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r] {
            const auto* step = loftStep(r);
            r.check(step && step->ruled, "loft: undo, straight again");
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r] {
            r.check(r.app().bodyCount() == 0, "loft: undo, the loft is gone", QString::number(r.app().bodyCount()));
            r.check(r.app().sketchCount() == 2, "loft: its sketches stay");
        },
        // ---- The same with a finger: tap, tap (through the arrow), Loft, Apply
        [&r] {
            r.key(Qt::Key_Escape);
            zoomOutUnderArrow(r);
            r.touchTap({profilePoint(r, 0, {{15, 10, 0}, {15, 8, 0}, {13, 10, 0}, {4, 4, 0}, {26, 4, 0}})});
            r.check(r.app().touchMode(), "loft (touch): the tap turns the touch layout on");
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "loft (touch): the rectangle tapped",
                    r.app().operationTitle());
        },
        [&r] {
            const auto onArrow = arrowPointOver(r, 1);
            r.touchTap({onArrow.value_or(profilePoint(r, 1, {{15, 10, 30}, {17, 11, 30}, {13, 9, 30}}))});
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 2 && sel.allOfKind(sel::SelectionKind::SketchProfile),
                    "loft (touch): tapping the circle adds it",
                    (onArrow ? QStringLiteral("tapped on the rectangle's arrow") : QStringLiteral("the arrow is not over the circle"))
                        + QStringLiteral(", selected ") + QString::number(sel.size()));
            r.check(onArrow.has_value(), "loft (touch): the tap went through the rectangle's arrow");
            r.check(hint(r).contains(QStringLiteral("tap another profile to add it")) && !hint(r).contains(QStringLiteral("Shift"))
                        && !hint(r).contains(QStringLiteral("Esc")),
                    "loft (touch): the hint speaks of taps", hint(r));
        },
        [&r] { r.check(tapItem(r, QStringLiteral("barAction_loft")), "loft (touch): tap Loft"); },
        [] {}, [] {},
        [&r] {
            r.check(r.app().operationTitle() == QStringLiteral("Loft") && r.app().operationCanCommit(),
                    "loft (touch): Loft previewed", r.app().operationTitle());
            r.check(hint(r).startsWith(QStringLiteral("Apply makes the loft")) && !hint(r).contains(QStringLiteral("Enter")),
                    "loft (touch): the hint names Apply, not Enter", hint(r));
            r.screenshot(QStringLiteral("loft_04_touch"));
            r.check(tapItem(r, QStringLiteral("barAction_apply")), "loft (touch): tap Apply");
        },
        [] {},
        [&r, state, num] {
            r.check(r.app().bodyCount() == 1 && closeTo(r.bodyVolume(), state->volume, 1e-5),
                    "loft (touch): the same loft (smooth: two profiles)", num(r.bodyVolume()));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r] { r.check(r.app().bodyCount() == 0, "loft (touch): undone"); },
    };
}

// ---- Part two: New body / Join / Cut ------------------------------------------
// A 40 x 40 x 20 block, a 10 x 10 square sketched on its top face and the
// same square on a plane 10 mm under the block: the loft is a straight
// 10 x 10 prism through the block (3000 mm³, 2000 of it inside).

// Set up (not what is tested; the first part clicks sketching and planes).
bool addBlockAndSquares(AcceptanceRunner& r)
{
    r.app().newDocument();
    auto& stack = r.app().interaction().undoStack();
    auto& document = r.app().document();
    auto block = std::make_unique<doc::BoxFeature>();
    block->origin = {0, 0, 0};
    block->size = {40, 40, 20};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Block", std::move(block));
    const Uuid body = create->bodyId();
    bool ok = stack.push(std::move(create), document).ok();
    sketch::Sketch onTop(Uuid::generate(), sketch::Plane::fromNormal({0, 0, 20}, {0, 0, 1}));
    onTop.setHostBody(body);
    sketch::addRectangle(onTop, onTop.plane().toLocal({15, 15, 20}), onTop.plane().toLocal({25, 25, 20}));
    ok = ok && stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(onTop)), document).ok();
    doc::Datum datum;
    datum.method = doc::DatumMethod::PlaneOffset;
    datum.originIndex = 2;
    datum.distance = -10;
    const Uuid plane = datum.id();
    ok = ok && stack.push(std::make_unique<cmd::AddDatumCommand>(std::move(datum)), document).ok();
    const doc::Datum* added = document.datum(plane);
    if (!ok || !added)
        return false;
    sketch::Sketch under(Uuid::generate(), doc::sketchPlaneOn(added->geometry()));
    under.setDatumPlane(plane);
    sketch::addRectangle(under, under.plane().toLocal({15, 15, -10}), under.plane().toLocal({25, 25, -10}));
    ok = stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(under)), document).ok();
    r.app().interaction().documentChanged();
    r.app().interaction().setStandardView(StandardView::Isometric, false);
    r.app().interaction().fitAll(false);
    return ok;
}

const doc::LoftFeature* modesLoftStep(AcceptanceRunner& r)
{
    const auto& bodies = r.app().document().bodies();
    if (bodies.empty() || bodies.front()->features().size() < 2)
        return nullptr;
    return dynamic_cast<const doc::LoftFeature*>(bodies.front()->features()[1].get());
}

// The loft's preview, as shown (the block joined or cut, or the prism alone).
double shownVolume(AcceptanceRunner& r)
{
    const auto* op = dynamic_cast<const interact::LoftOperation*>(r.app().interaction().operation());
    return op && op->previewMesh() ? meshVolume(*op->previewMesh()) : 0.0;
}

std::optional<doc::ExtrudeMode> loftMode(AcceptanceRunner& r)
{
    const auto* op = dynamic_cast<const interact::LoftOperation*>(r.app().interaction().operation());
    return op ? std::optional(op->mode()) : std::nullopt;
}

// The square under the block, seen from below.
QPointF underPoint(AcceptanceRunner& r)
{
    return profilePoint(r, 1, {{23, 23, -10}, {20, 20, -10}, {17, 23, -10}, {23, 17, -10}});
}

std::vector<AcceptanceRunner::Step> modeSteps(AcceptanceRunner& r)
{
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    return {
        [&r] {
            r.check(addBlockAndSquares(r), "loft modes: a block, a square on its top face, one under it");
            r.check(r.app().bodyCount() == 1 && r.app().sketchCount() == 2, "loft modes: set up");
        },
        [&r] {
            // The top square from above; the arrow starts off the middle, so it
            // does not stand on the lower square's middle seen from below.
            r.click(profilePoint(r, 0, {{17, 17, 20}, {18, 22, 20}, {22, 18, 20}}));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "loft modes: the top square is selected",
                    r.app().operationTitle());
            r.app().interaction().setStandardView(StandardView::Bottom, false);
            r.app().interaction().fitAll(false);
        },
        [&r] {
            r.click(underPoint(r), Qt::ShiftModifier);
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 2 && sel.allOfKind(sel::SelectionKind::SketchProfile),
                    "loft modes: Shift-click the square under the block", QString::number(sel.size()));
        },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_loft")), "loft modes: Loft"); },
        [] {}, [] {},
        [&r, num] {
            r.check(loftMode(r) == doc::ExtrudeMode::Join, "loft modes: from the block's face it joins the block");
            r.check(closeTo(shownVolume(r), 32000 + 1000, 0.01), "loft modes: the block grows by the prism under it",
                    num(shownVolume(r)));
            r.screenshot(QStringLiteral("loft_05_join"));
            r.check(r.clickItem(QStringLiteral("barAction_mode:cut")), "loft modes: Cut");
        },
        [] {}, [] {},
        [&r, num] {
            r.check(loftMode(r) == doc::ExtrudeMode::Cut, "loft modes: cutting");
            r.check(r.app().operationCanCommit(), "loft modes: the cut can be applied", r.app().operationError());
            r.check(closeTo(shownVolume(r), 32000 - 2000, 0.01), "loft modes: the prism is cut out of the block",
                    num(shownVolume(r)));
            r.screenshot(QStringLiteral("loft_06_cut"));
            r.check(r.clickItem(QStringLiteral("barAction_mode:new")), "loft modes: New body");
        },
        [] {}, [] {},
        [&r, num] {
            r.check(loftMode(r) == doc::ExtrudeMode::NewBody, "loft modes: a new body");
            r.check(closeTo(shownVolume(r), 3000, 0.01), "loft modes: the prism alone", num(shownVolume(r)));
            r.check(r.clickItem(QStringLiteral("barAction_mode:cut")), "loft modes: Cut again");
        },
        [] {}, [] {},
        // Shift-click a chosen profile: it comes out of the loft.
        [&r] {
            r.check(loftMode(r) == doc::ExtrudeMode::Cut, "loft modes: cutting again");
            r.click(underPoint(r), Qt::ShiftModifier);
        },
        [] {},
        [&r] {
            r.check(r.app().interaction().selection().size() == 1, "loft modes: Shift-click takes the square out",
                    QString::number(r.app().interaction().selection().size()));
            r.check(r.app().operationTitle() != QStringLiteral("Loft"), "loft modes: one profile is no loft",
                    r.app().operationTitle());
        },
        // (Later than a double-click's time: the same place clicked again.)
        [] {}, [] {}, [] {},
        [&r] { r.click(underPoint(r), Qt::ShiftModifier); },
        [] {},
        [&r] {
            r.check(r.app().interaction().selection().size() == 2, "loft modes: and back in",
                    QString::number(r.app().interaction().selection().size()) + QStringLiteral(" selected, ")
                        + r.app().operationTitle());
            r.check(r.clickItem(QStringLiteral("barAction_loft")), "loft modes: Loft again");
        },
        [] {}, [] {},
        [&r] {
            r.check(loftMode(r) == doc::ExtrudeMode::Cut, "loft modes: Cut is kept while the squares stay selected");
            r.check(r.clickItem(QStringLiteral("barAction_apply")), "loft modes: Apply");
        },
        [] {},
        [&r, num] {
            r.check(r.app().bodyCount() == 1 && closeTo(r.bodyVolume(), 30000, 1e-9), "loft modes: the prism is cut out",
                    num(r.bodyVolume()));
            const auto* step = modesLoftStep(r);
            r.check(step && step->mode == doc::ExtrudeMode::Cut, "loft modes: a Cut loft step");
            r.app().interaction().setStandardView(StandardView::Isometric, false);
            r.app().interaction().fitAll(false);
            if (step)
                r.check(r.clickItem(QStringLiteral("historyRow_") + idText(step->id())), "loft modes: the loft's Model panel row");
        },
        [&r] {
            const auto* step = modesLoftStep(r);
            if (step)
                r.check(r.clickItem(QStringLiteral("historyChoice_") + idText(step->id()) + QStringLiteral("_mode_Join")),
                        "loft modes: Join in the Model panel");
        },
        [] {},
        [&r, num] {
            const auto* step = modesLoftStep(r);
            r.check(step && step->mode == doc::ExtrudeMode::Join, "loft modes: the step joins now");
            r.check(closeTo(r.bodyVolume(), 33000, 1e-9), "loft modes: the prism joins the block", num(r.bodyVolume()));
            r.screenshot(QStringLiteral("loft_07_joined"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r, num] {
            r.check(closeTo(r.bodyVolume(), 30000, 1e-9), "loft modes: undo, cut again", num(r.bodyVolume()));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r, num] {
            r.check(r.app().bodyCount() == 1 && closeTo(r.bodyVolume(), 32000, 1e-9), "loft modes: undo, the block alone",
                    num(r.bodyVolume()));
        },
    };
}

std::vector<AcceptanceRunner::Step> allSteps(AcceptanceRunner& r)
{
    auto out = steps(r);
    auto more = modeSteps(r);
    out.insert(out.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("loft"), 74, allSteps});

} // namespace
} // namespace os::app
