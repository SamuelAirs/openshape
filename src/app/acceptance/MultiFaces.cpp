// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Collecting faces with a finger, and turning the view over the top (the
// owner on his iPhone Duo, 2026-11-12, after an evening's design work in
// 0.3.1: "It's hard to select multiple faces at once, it either locks on to
// an edge (and you can't tap it again to unselect it), or it deselects
// everything. Navigation while designing is tough. There are locks to where
// you can rotate your image."). Real touch input through Qt's touch path
// with a finger's timing, on an iPhone-sized window (402x874) and the Duo's
// outer display (466x678): a plate with three small pads (a pad's top is
// about 30 px wide, inside a finger's two edge zones); taps on the pads' tops
// add them, two quick taps on two neighbouring faces add both (not the
// body), a tap beside an edge takes the face under the finger, a tap just
// off the part costs nothing, empty space clears; a selected face and a
// selected edge under their arrows are taken out by a second tap; then one
// finger drags the view over the top, where it used to stop, a sideways
// drag there still carries the model with the finger, and the isometric
// view rights it. Last, his next report (2026-11-13: "if I have a square
// shape cut out of a plane, and I try to make the hole bigger by selecting
// all of the faces and expanding them all outward, I can't do that"): on an
// iPad-sized window the four walls of a square hole are tapped, two from
// each side; one arrow shows (there was none), a finger drags it back into
// the part, the value box's tick applies, and the hole is larger by that
// much on every side, square, one undo step.

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "interaction/InputEvents.h"
#include "interaction/InteractionController.h"
#include "interaction/Manipulator.h"
#include "interaction/Operation.h"
#include "ui/AppController.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <memory>
#include <optional>
#include <vector>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

// Lets the application run (events, frames) for `ms` milliseconds.
void pause(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

// A finger's tap of 80 ms. `restBefore`: at least half a second since the
// last tap makes it a tap of its own, never half of a double-tap.
void tap(AcceptanceRunner& r, QPointF p, int restBefore = 500)
{
    pause(restBefore);
    r.touchPress(p);
    pause(80);
    r.touchRelease(p);
}

struct Scene {
    Uuid body;
    int pads[3] = {-1, -1, -1}; // the pads' top faces
    int plate = -1;             // the plate's top face
    Camera before;              // the view before the orbit
    QPointF dragFrom;
    double nearX = 0;
};

interact::InteractionController& ix(AcceptanceRunner& r) { return r.app().interaction(); }

Vec2 toVec(QPointF p) { return {p.x(), p.y()}; }

// The face at a model point well inside it (a mouse's pick), or -1.
int faceAt(AcceptanceRunner& r, double x, double y, double z)
{
    const auto hit = ix(r).pickAt(toVec(r.screenPoint(x, y, z)), interact::InputProfile());
    return hit.kind == sel::PickKind::Face ? hit.index : -1;
}

std::vector<int> selectedFaces(AcceptanceRunner& r)
{
    std::vector<int> faces;
    for (const auto& item : ix(r).selection().items())
        if (item.kind == sel::SelectionKind::Face)
            faces.push_back(item.index);
    std::sort(faces.begin(), faces.end());
    return faces;
}

bool facesAre(AcceptanceRunner& r, std::vector<int> expected)
{
    std::sort(expected.begin(), expected.end());
    return selectedFaces(r) == expected && ix(r).selection().size() == expected.size();
}

QString selectionText(AcceptanceRunner& r)
{
    QStringList parts;
    for (const auto& item : ix(r).selection().items())
        parts << (item.kind == sel::SelectionKind::Face   ? QStringLiteral("face %1").arg(item.index)
                  : item.kind == sel::SelectionKind::Edge ? QStringLiteral("edge %1").arg(item.index)
                  : item.kind == sel::SelectionKind::Body ? QStringLiteral("body")
                                                          : QStringLiteral("other"));
    return parts.isEmpty() ? QStringLiteral("nothing") : parts.join(QStringLiteral(", "));
}

bool inView(AcceptanceRunner& r, QPointF p)
{
    QQuickItem* item = r.itemAt(p);
    return item && item->objectName() == QLatin1String("viewport");
}

// Within a finger's grab zone of the active operation's arrow.
bool onArrow(AcceptanceRunner& r, QPointF p)
{
    const interact::Operation* op = ix(r).operation();
    const double tolerance = interact::InputProfile::forDevice(interact::PointerDevice::Touch).handleTolerance;
    for (int i = 0; op && i < op->handleCount(); ++i)
        if (op->handle(i).hitTest(ix(r).camera(), toVec(p), op->handleOffset(i), tolerance))
            return true;
    return false;
}

// A spot of the view with nothing under it and no control over it.
std::optional<QPointF> emptySpot(AcceptanceRunner& r)
{
    const auto touch = interact::InputProfile::forDevice(interact::PointerDevice::Touch);
    for (double y = 60; y < r.window()->height() - 60; y += 20)
        for (double x = 30; x < r.window()->width() - 30; x += 20) {
            const QPointF p(x, y);
            bool clear = inView(r, p);
            // Empty for a finger, and for a finger's reach around it.
            for (const QPointF d : {QPointF(0, 0), QPointF(30, 0), QPointF(-30, 0), QPointF(0, 30), QPointF(0, -30)})
                clear = clear && !ix(r).pickAt(toVec(p + d), touch).hit();
            if (clear)
                return p;
        }
    return std::nullopt;
}

// A 60 x 30 x 5 plate with three 10 x 10 pads standing 5 mm on it.
void makePlate(AcceptanceRunner& r, Scene& s)
{
    r.app().newDocument();
    geom::Shape shape = geom::makeBox({0, 0, 0}, {60, 30, 5}).value();
    for (const double x : {5.0, 25.0, 45.0})
        shape = geom::booleanOp(shape, geom::makeBox({x, 10, 5}, {10, 10, 5}).value(), geom::BooleanKind::Union).value();
    const bool imported = ix(r).importBodies({{"Plate", shape}}, "plate.step").ok();
    ix(r).cancelOperation();
    ix(r).cancelOperation();
    ix(r).fitAll(false);
    r.check(imported && r.app().bodyCount() == 1 && ix(r).selection().empty(), "multifaces: a plate with three pads");
    if (r.app().bodyCount() == 1)
        s.body = r.app().document().bodies().front()->id();
}

void addPass(Steps& steps, AcceptanceRunner& r, const std::shared_ptr<Scene>& s, int width, int height, const QVariant& safeArea,
             const QString& where)
{
    steps.push_back([&r, width, height, safeArea] {
        r.app().setTouchMode(true);
        r.resizeWindow(width, height);
        r.window()->setProperty("simulatedSafeArea", safeArea);
    });
    wait(steps, 3);
    steps.push_back([&r, s] { makePlate(r, *s); });
    wait(steps, 3);
    // ---- Three small faces, one tap each.
    steps.push_back([&r, s, where] {
        for (int i = 0; i < 3; ++i)
            s->pads[i] = faceAt(r, 10.0 + 20.0 * i, 15, 10);
        s->plate = faceAt(r, 30, 5, 5);
        r.check(s->pads[0] >= 0 && s->pads[1] >= 0 && s->pads[2] >= 0 && s->plate >= 0, where + QStringLiteral(": the faces are in sight"));
        const double padWidth = std::hypot(r.screenPoint(25, 15, 10).x() - r.screenPoint(35, 15, 10).x(),
                                           r.screenPoint(25, 15, 10).y() - r.screenPoint(35, 15, 10).y());
        r.check(padWidth < 40, where + QStringLiteral(": a pad's top is narrower than a finger's two edge zones"),
                AcceptanceRunner::num(padWidth));
        bool reachable = true;
        for (int i = 0; i < 3; ++i)
            reachable = reachable && inView(r, r.screenPoint(10.0 + 20.0 * i, 15, 10));
        r.check(reachable, where + QStringLiteral(": no control over the pads"));
        tap(r, r.screenPoint(10, 15, 10));
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[0]}), where + QStringLiteral(": a tap on a small face selects the face, not an edge"),
                selectionText(r));
        tap(r, r.screenPoint(51.5, 15, 10)); // a little off its middle
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[0], s->pads[2]}), where + QStringLiteral(": a second small face is added"), selectionText(r));
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Push/Pull"),
                where + QStringLiteral(": Push/Pull stays, one arrow for both"), r.app().operationTitle());
        // Two quick taps, 100 ms apart and under 32 px from each other: the
        // third pad's top near its front edge, and the plate in front of it.
        const QPointF onPad = r.screenPoint(30, 12.5, 10);
        const QPointF onPlate = r.screenPoint(30, 6.5, 5);
        const double apart = std::hypot(onPad.x() - onPlate.x(), onPad.y() - onPlate.y());
        r.check(apart < 32, where + QStringLiteral(": the two quick taps are a double-tap's distance apart"),
                AcceptanceRunner::num(apart));
        tap(r, onPad);
        tap(r, onPlate, 100);
    });
    wait(steps, 3);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[0], s->pads[1], s->pads[2], s->plate}),
                where + QStringLiteral(": two quick taps on two faces add both (not the body)"), selectionText(r));
        r.check(r.app().interaction().undoStack().size() == 1, where + QStringLiteral(": selecting changed nothing in the model"));
        r.screenshot(QStringLiteral("multifaces_") + where + QStringLiteral("_four_faces"));
        // 3 px inside the plate top's front edge: an edge's zone for a finger.
        const QPointF edge = r.screenPoint(50, 0, 5);
        const QPointF in = r.screenPoint(50, 5, 5) - edge;
        tap(r, edge + in * (3.0 / std::hypot(in.x(), in.y())));
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[0], s->pads[1], s->pads[2]}),
                where + QStringLiteral(": a tap beside an edge takes the face under the finger out; nothing turns into an edge"),
                selectionText(r));
        // Just off the part, beside its outline at the right end.
        const auto touch = interact::InputProfile::forDevice(interact::PointerDevice::Touch);
        const QPointF inside = r.screenPoint(60, 15, 2.5);
        std::optional<QPointF> justOff;
        for (double t = 0; t < 80 && !justOff; t += 1) {
            const QPointF p = inside + QPointF(t, 0);
            if (!ix(r).pickAt(toVec(p), interact::InputProfile()).hit() && ix(r).pickAt(toVec(p), touch).kind == sel::PickKind::Edge
                && !ix(r).pickAt(toVec(p - QPointF(6, 0)), interact::InputProfile()).hit())
                justOff = p;
        }
        r.check(justOff && inView(r, *justOff), where + QStringLiteral(": a spot just off the part, within a finger's reach of its outline"));
        if (justOff)
            tap(r, *justOff);
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[0], s->pads[1], s->pads[2]}), where + QStringLiteral(": a tap just off the part costs nothing"),
                selectionText(r));
        const auto empty = emptySpot(r);
        r.check(empty.has_value(), where + QStringLiteral(": empty space in the view"));
        if (empty)
            tap(r, *empty);
    });
    wait(steps, 2);
    // ---- "You can't tap it again to unselect it".
    steps.push_back([&r, where] {
        r.check(ix(r).selection().empty(), where + QStringLiteral(": a tap on empty space clears"), selectionText(r));
        tap(r, r.screenPoint(30, 15, 10));
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        r.check(facesAre(r, {s->pads[1]}), where + QStringLiteral(": one face"), selectionText(r));
        r.check(onArrow(r, r.screenPoint(30, 15, 10)), where + QStringLiteral(": its arrow's finger zone covers the spot tapped"));
        tap(r, r.screenPoint(30, 15, 10));
    });
    wait(steps, 2);
    steps.push_back([&r, where] {
        r.check(ix(r).selection().empty() && !r.app().operationActive(),
                where + QStringLiteral(": a second tap on the selected face, under its arrow, takes it out"), selectionText(r));
        tap(r, r.screenPoint(30, 0, 5)); // the middle of the plate top's front edge
    });
    wait(steps, 2);
    steps.push_back([&r, where] {
        const auto& items = ix(r).selection().items();
        r.check(items.size() == 1 && items.front().kind == sel::SelectionKind::Edge, where + QStringLiteral(": an edge, with nothing selected"),
                selectionText(r));
        r.check(onArrow(r, r.screenPoint(30, 0, 5)), where + QStringLiteral(": the Fillet arrow starts on it"));
        tap(r, r.screenPoint(30, 0, 5));
    });
    wait(steps, 2);
    // ---- Over the top with one finger.
    steps.push_back([&r, s, where] {
        r.check(ix(r).selection().empty(), where + QStringLiteral(": a second tap on the selected edge takes it out"), selectionText(r));
        r.check(r.app().interaction().undoStack().size() == 1, where + QStringLiteral(": no tap pushed, pulled or rounded anything"));
        const auto empty = emptySpot(r);
        s->dragFrom = empty ? *empty : QPointF(40, 200);
        s->before = ix(r).camera();
        r.touchPress(s->dragFrom);
    });
    // 0.008 rad a pixel: 20 px a step, 15 steps: 2.4 rad, from 35 degrees up
    // to 8 degrees past straight down on the far side.
    for (int i = 1; i <= 15; ++i) {
        steps.push_back([&r, s, i, where] {
            r.touchMove(s->dragFrom + QPointF(0, 20.0 * i));
            if (i == 9) {
                const Camera& c = ix(r).camera();
                r.check(c.upsideDown() && c.up().z < 0 && c.eye().z > c.target.z,
                        where + QStringLiteral(": the drag went over the top (it stopped there before)"), AcceptanceRunner::num(c.pitch));
                r.screenshot(QStringLiteral("multifaces_") + where + QStringLiteral("_over_the_top"));
            }
        });
    }
    steps.push_back([&r, s] { r.touchRelease(s->dragFrom + QPointF(0, 300)); });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        const Camera& c = ix(r).camera();
        const double turned = std::remainder(c.pitch - s->before.pitch, 2 * kPi);
        r.check(std::abs(std::abs(turned) - 2.4) < 0.25 && c.upsideDown(), where + QStringLiteral(": 300 px turned the view 2.4 rad, no stop"),
                AcceptanceRunner::num(turned));
        r.check(std::abs(std::remainder(c.yaw - s->before.yaw, 2 * kPi)) < 1e-6, where + QStringLiteral(": straight down the screen never turns about the vertical"));
        // Upside down, a sideways drag still carries the near side with the finger.
        const Vec3 near = c.target + c.backward() * (c.distance * 0.2);
        s->nearX = c.project(near).x;
        s->before = c;
        r.touchPress(s->dragFrom);
    });
    for (int i = 1; i <= 4; ++i)
        steps.push_back([&r, s, i] { r.touchMove(s->dragFrom + QPointF(15.0 * i, 0)); });
    steps.push_back([&r, s] { r.touchRelease(s->dragFrom + QPointF(60, 0)); });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        const Camera& c = ix(r).camera();
        const Vec3 near = s->before.target + s->before.backward() * (s->before.distance * 0.2);
        r.check(c.project(near).x > s->nearX + 5, where + QStringLiteral(": upside down, a drag to the right carries the model to the right"),
                AcceptanceRunner::num(c.project(near).x - s->nearX));
        r.check(ix(r).selection().empty() && r.app().interaction().undoStack().size() == 1, where + QStringLiteral(": the drags selected and changed nothing"));
        ix(r).setStandardView(StandardView::Isometric, false);
    });
    wait(steps, 2);
    steps.push_back([&r, where] {
        r.check(!ix(r).camera().upsideDown() && ix(r).camera().up().z > 0, where + QStringLiteral(": the isometric view is the right way up again"));
    });
}

// A 40 x 30 x 5 plate with a 10 x 10 square hole through its middle; its
// walls in `walls`: the -X side (facing +X into the hole), +X, -Y, +Y.
struct Hole {
    int walls[4] = {-1, -1, -1, -1};
    QPointF dragFrom, dragTo;
    double dragged = 0;
};

void makeHolePlate(AcceptanceRunner& r, Hole& hole)
{
    r.app().newDocument();
    const auto plate = geom::makeBox({0, 0, 0}, {40, 30, 5}).value();
    const auto cut = geom::makeBox({15, 10, -1}, {10, 10, 7}).value();
    const geom::Shape shape = geom::booleanOp(plate, cut, geom::BooleanKind::Subtract).value();
    const bool imported = ix(r).importBodies({{"Plate", shape}}, "plate.step").ok();
    ix(r).cancelOperation();
    ix(r).cancelOperation();
    r.check(imported && r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - 5500.0) < 1e-6, "hole: a plate with a square hole",
            AcceptanceRunner::num(r.bodyVolume()));
    if (r.app().bodyCount() != 1)
        return;
    const geom::Shape& body = r.app().document().bodies().front()->shape();
    for (int i = 0; i < body.faceCount(); ++i) {
        const auto f = geom::faceInfo(body, i);
        if (!f || !f->isPlanar() || std::abs(f->normal.z) > 1e-6 || f->point.x < 1 || f->point.x > 39 || f->point.y < 1 || f->point.y > 29)
            continue;
        hole.walls[f->normal.x > 0.5 ? 0 : f->normal.x < -0.5 ? 1 : f->normal.y > 0.5 ? 2 : 3] = i;
    }
}

void addHole(Steps& steps, AcceptanceRunner& r)
{
    auto h = std::make_shared<Hole>();
    steps.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(true);
        r.resizeWindow(1180, 820);
    });
    wait(steps, 3);
    steps.push_back([&r, h] {
        makeHolePlate(r, *h);
        ix(r).setViewAngles(-45 * kPi / 180, 50 * kPi / 180, false);
        ix(r).fitAll(false);
    });
    wait(steps, 3);
    // From the front right: the two walls on the far sides.
    steps.push_back([&r, h] {
        r.check(faceAt(r, 15, 15, 2.5) == h->walls[0] && faceAt(r, 20, 20, 2.5) == h->walls[3] && h->walls[0] >= 0 && h->walls[3] >= 0,
                "hole: two of its walls are in sight from the front right");
        r.check(inView(r, r.screenPoint(15, 15, 2.5)) && inView(r, r.screenPoint(20, 20, 2.5)), "hole: no control over them");
        tap(r, r.screenPoint(15, 15, 2.5));
        tap(r, r.screenPoint(20, 20, 2.5));
    });
    wait(steps, 3);
    steps.push_back([&r, h] {
        r.check(facesAre(r, {h->walls[0], h->walls[3]}), "hole: two walls that meet at a corner", selectionText(r));
        const auto* op = dynamic_cast<const interact::MultiPushPullOperation*>(ix(r).operation());
        r.check(op && op->eachAlongItsOwnNormal() && r.app().operationTitle() == QStringLiteral("Push/Pull"),
                "hole: faces facing different ways have one Push/Pull arrow (they had none)", r.app().operationTitle());
        ix(r).setViewAngles(135 * kPi / 180, 50 * kPi / 180, false);
    });
    wait(steps, 3);
    // From the back left: the other two.
    steps.push_back([&r, h] {
        r.check(ix(r).selection().size() == 2, "hole: turning the view keeps the selection", selectionText(r));
        r.check(faceAt(r, 25, 15, 2.5) == h->walls[1] && faceAt(r, 20, 10, 2.5) == h->walls[2] && h->walls[1] >= 0 && h->walls[2] >= 0,
                "hole: the other two walls are in sight from the back left");
        tap(r, r.screenPoint(25, 15, 2.5));
        tap(r, r.screenPoint(20, 10, 2.5));
    });
    wait(steps, 3);
    steps.push_back([&r, h] {
        r.check(facesAre(r, {h->walls[0], h->walls[1], h->walls[2], h->walls[3]}), "hole: all four walls", selectionText(r));
        const auto* op = dynamic_cast<const interact::MultiPushPullOperation*>(ix(r).operation());
        r.check(op && op->eachAlongItsOwnNormal() && op->movingFaces().size() == 4, "hole: one arrow moves all four");
        r.check(r.findItem(QStringLiteral("valueChipField")) && r.findItem(QStringLiteral("valueChipField"))->isVisible(),
                "hole: the value box shows");
        r.screenshot(QStringLiteral("multifaces_hole_four_walls"));
        // The arrow stands on the wall tapped last (y = 10, facing +Y into
        // the hole): a finger on its shaft drags it 3 mm back into the part.
        // (In a hole this small the arrow's finger zone reaches its walls,
        // so a drag that starts on a wall is the arrow's too; a wall dragged
        // along its own normal is test_multi_face's, zoomed in.)
        if (!op)
            return;
        const interact::LinearManipulator arrow = op->handle(0);
        const Vec3 anchor = arrow.anchor(op->handleOffset(0));
        const interact::ArrowStyle style;
        const double px = ix(r).camera().pixelSize(anchor);
        const Vec3 grab = anchor + arrow.direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
        const Vec3 to = grab - arrow.direction() * 3.0;
        r.check(std::abs(arrow.direction().y - 1.0) < 1e-9, "hole: the arrow stands on the wall tapped last, pointing into the hole");
        h->dragFrom = r.screenPoint(grab.x, grab.y, grab.z);
        h->dragTo = r.screenPoint(to.x, to.y, to.z);
        pause(500);
        r.touchPress(h->dragFrom);
    });
    for (int i = 1; i <= 6; ++i)
        steps.push_back([&r, h, i] { r.touchMove(h->dragFrom + (h->dragTo - h->dragFrom) * (i / 6.0)); });
    steps.push_back([&r, h] { r.touchRelease(h->dragTo); });
    wait(steps, 4);
    steps.push_back([&r, h] {
        const interact::Operation* op = ix(r).operation();
        h->dragged = op ? op->value() : 0.0;
        r.check(op && h->dragged < -1.0 && h->dragged > -5.0, "hole: the arrow dragged back into the part gives a negative distance",
                AcceptanceRunner::num(h->dragged));
        r.check(ix(r).selection().size() == 4, "hole: the four walls stay selected", selectionText(r));
        r.check(std::abs(r.bodyVolume() - 5500.0) < 1e-6, "hole: nothing applied yet", AcceptanceRunner::num(r.bodyVolume()));
        r.screenshot(QStringLiteral("multifaces_hole_preview"));
        r.check(r.tapItem(QStringLiteral("valueChipApply")), "hole: the value box's tick");
    });
    wait(steps, 4);
    steps.push_back([&r, h] {
        const double side = 10 + 2 * std::abs(h->dragged);
        const double expected = (1200 - side * side) * 5;
        r.check(std::abs(r.bodyVolume() - expected) < 1e-6,
                QStringLiteral("hole: the hole is %1 mm square now (every wall back by %2)").arg(side).arg(std::abs(h->dragged)),
                AcceptanceRunner::num(r.bodyVolume()) + QStringLiteral(" vs ") + AcceptanceRunner::num(expected));
        const auto box = geom::boundingBox(r.app().document().bodies().front()->shape());
        r.check(std::abs(box.min.x) < 1e-9 && std::abs(box.max.x - 40) < 1e-9 && std::abs(box.max.y - 30) < 1e-9 && std::abs(box.max.z - 5) < 1e-9,
                "hole: the plate's outside did not move");
        r.check(r.app().document().bodies().front()->shape().faceCount() == 10, "hole: still four walls, square corners");
        r.screenshot(QStringLiteral("multifaces_hole_larger"));
        r.check(ix(r).undo(), "hole: one undo step");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(std::abs(r.bodyVolume() - 5500.0) < 1e-6, "hole: undone", AcceptanceRunner::num(r.bodyVolume()));
        ix(r).cancelOperation();
    });
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<Scene>();
    Steps steps;
    addPass(steps, r, s, 402, 874, QVariantList{62, 0, 34, 0}, QStringLiteral("iphone"));
    addPass(steps, r, s, 466, 678, QVariantList{0, 84, 34, 0}, QStringLiteral("duo_outer"));
    addHole(steps, r);
    steps.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    wait(steps, 3);
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("multifaces"), 96, steps});

} // namespace
} // namespace os::app
