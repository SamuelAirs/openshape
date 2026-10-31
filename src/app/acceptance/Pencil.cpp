// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Apple Pencil (the owner's Pencil USB-C: tilt, no pressure, no double-tap
// or squeeze; hover on the iPad Air, none on the iPhone Duo). The pen goes
// through Qt's tablet path (QWindowSystemInterface::handleTabletEvent, as
// the iOS plugin delivers it), fingers and the palm through the touch path,
// at the iPad Air 11" size (1180x820) and the iPhone 16 Pro size (402x874):
// pen hover highlights a face and, in a sketch, shows the snapped point; the
// pen draws a rectangle that is exactly the face while a palm rests and
// slides on the view (and lands again just after the pen lifts, with a
// two-finger tap that would undo): the view does not move; the pen never
// shows the loupe; a finger after the grace period still orbits; a finger
// tap in pen mode says how to select with a finger (once in a while);
// turning Pen off lets a finger tap select again.

#include "app/AcceptanceRunner.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

struct Config {
    QString name;
    int width = 0;
    int height = 0;
    QVariant safeArea;
    bool compact = false;
};

struct State {
    Camera camera;
    QPointF palm;          // a palm on the view
    QPointF penFrom, penTo; // the pen's stroke
    QStringList messages;
    QMetaObject::Connection listening;
    int hintsBefore = 0;
};
using Shared = std::shared_ptr<State>;

interact::SketchSession* session(AcceptanceRunner& r)
{
    return r.app().interaction().sketchSession();
}

QPointF at(AcceptanceRunner& r, Vec2 local)
{
    const auto* s = session(r);
    if (!s)
        return {};
    const Vec2 p = r.app().interaction().camera().project(s->sketch().plane().toWorld(local));
    return {p.x, p.y};
}

bool hasPoint(AcceptanceRunner& r, Vec2 p)
{
    const auto* s = session(r);
    if (!s)
        return false;
    for (const auto& [id, q] : s->sketch().points())
        if (id != sketch::kOriginId && (q.position - p).length() < 1e-9)
            return true;
    return false;
}

QString pointsText(AcceptanceRunner& r)
{
    QString out;
    if (const auto* s = session(r))
        for (const auto& [id, q] : s->sketch().points())
            out += QStringLiteral("(%1, %2) ").arg(q.position.x, 0, 'f', 3).arg(q.position.y, 0, 'f', 3);
    return out;
}

bool sameCamera(const Camera& a, const Camera& b)
{
    return std::abs(a.yaw - b.yaw) < 1e-12 && std::abs(a.pitch - b.pitch) < 1e-12 && (a.target - b.target).length() < 1e-9
        && std::abs(a.distance - b.distance) < 1e-9 && std::abs(a.orthoHeight - b.orthoHeight) < 1e-9;
}

QString cameraText(const Camera& c)
{
    return QStringLiteral("yaw %1 pitch %2 target (%3, %4, %5) distance %6")
        .arg(c.yaw, 0, 'f', 6)
        .arg(c.pitch, 0, 'f', 6)
        .arg(c.target.x, 0, 'f', 3)
        .arg(c.target.y, 0, 'f', 3)
        .arg(c.target.z, 0, 'f', 3)
        .arg(c.distance, 0, 'f', 3);
}

// A spot on the 3D view (no control over it) where a hand rests: right of
// and below the middle, as a right hand's palm while drawing.
QPointF palmSpot(AcceptanceRunner& r)
{
    const double w = r.window()->width(), h = r.window()->height();
    for (const QPointF& p : {QPointF(0.72 * w, 0.62 * h), QPointF(0.62 * w, 0.68 * h), QPointF(0.8 * w, 0.5 * h),
                             QPointF(0.3 * w, 0.62 * h), QPointF(0.5 * w, 0.4 * h)}) {
        const QQuickItem* item = r.itemAt(p);
        if (item && item->objectName() == QStringLiteral("viewport"))
            return p;
    }
    return {0.5 * w, 0.5 * h};
}

bool loupeShown(AcceptanceRunner& r)
{
    const QQuickItem* loupe = r.findItem(QStringLiteral("sketchLoupe"));
    return (loupe && loupe->isVisible()) || r.app().sketchLoupe().value(QStringLiteral("visible")).toBool();
}

int hints(const Shared& s)
{
    return int(s->messages.count(QString::fromUtf8(interact::InteractionController::kPenFingerHint)));
}

void addFlow(Steps& steps, AcceptanceRunner& r, const Config& c)
{
    auto s = std::make_shared<State>();
    const QString shot = QStringLiteral("pencil_") + c.name + QStringLiteral("_");
    auto label = [c](const char* text) { return c.name + QStringLiteral(": ") + QString::fromUtf8(text); };
    steps.push_back([&r, c, s] {
        r.app().setTouchMode(true);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
        s->listening = QObject::connect(&r.app(), &ui::AppController::message, &r.app(), [s](const QString& t) { s->messages << t; });
    });
    wait(steps, 3);
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("tool_box")), label("Box")); });
    wait(steps, 3);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);

    // ---- Pen hover highlights the face under it (the pen's tolerances).
    steps.push_back([&r] {
        r.penHover(r.screenPoint(0, 0, 20) + QPointF(30, 20));
        r.penHover(r.screenPoint(0, 0, 20));
    });
    wait(steps, 1);
    steps.push_back([&r, label, shot] {
        const auto& ic = r.app().interaction();
        r.check(ic.hover().kind == sel::PickKind::Face, label("pen hover highlights the top face"),
                QString::number(int(ic.hover().kind)));
        r.check(ic.hoverDevice() == interact::PointerDevice::Pen, label("the hover is the pen's (not taken for a mouse)"),
                QString::number(int(ic.hoverDevice())));
        r.check(!r.app().penMode(), label("a hover alone does not turn pen mode on"));
        r.screenshot(shot + QStringLiteral("1_hover_face"));
    });
    // ---- A pen tap selects the face and turns pen mode on; Sketch on it.
    steps.push_back([&r] {
        r.penPress(r.screenPoint(0, 0, 20));
        r.penRelease(r.screenPoint(0, 0, 20));
    });
    wait(steps, 2);
    steps.push_back([&r, label] {
        const auto& sel = r.app().interaction().selection();
        r.check(r.app().penMode(), label("the pen turned pen mode on"));
        r.check(sel.size() == 1 && sel.items().front().kind == sel::SelectionKind::Face, label("the pen tap selected the face"));
        r.check(r.tapItem(QStringLiteral("action_sketch")), label("Sketch on the top face"));
    });
    wait(steps, 6);
    // ---- Pen hover in the sketch: the point a tap would place, snapped.
    // (The top left corner: an iPad's Model panel covers the top right one.)
    steps.push_back([&r] { r.penHover(at(r, {-10, 10}) + QPointF(5, 6)); });
    wait(steps, 1);
    steps.push_back([&r, label, shot] {
        const auto* ss = session(r);
        r.check(r.app().sketchMode() && ss && ss->tool() == interact::SketchTool::Rectangle, label("sketching, Rectangle"));
        if (!ss)
            return;
        const auto cursor = ss->cursorPoint();
        r.check(cursor && (*cursor - Vec2{-10, 10}).length() < 1e-9, label("pen hover shows the point snapped to the face's corner"),
                cursor ? QStringLiteral("(%1, %2)").arg(cursor->x).arg(cursor->y) : QStringLiteral("none"));
        r.check(ss->pointerDevice() == interact::PointerDevice::Pen, label("with the pen's tolerances"));
        r.check(!loupeShown(r), label("a hover shows no loupe"));
        r.screenshot(shot + QStringLiteral("2_hover_sketch"));
    });

    // ---- The pen draws a rectangle while a palm rests and slides.
    steps.push_back([&r, s] {
        s->camera = r.app().interaction().camera();
        s->palm = palmSpot(r);
        s->penFrom = at(r, {-10, 10}) + QPointF(4, 3);
        s->penTo = at(r, {10, -10}) + QPointF(-3, -4);
        r.penHover(s->penFrom);
        r.penPress(s->penFrom);
    });
    steps.push_back([&r, s] {
        r.touchPress(s->palm); // the hand lands on the view
        r.penMove(s->penFrom + (s->penTo - s->penFrom) * 0.1);
    });
    for (int k = 2; k <= 7; ++k)
        steps.push_back([&r, s, k] {
            s->palm += QPointF(14, 9);
            r.touchMove(s->palm);
            r.penMove(s->penFrom + (s->penTo - s->penFrom) * (0.1 * k));
        });
    steps.push_back([&r, s, label, shot] {
        r.check(!loupeShown(r), label("the pen never shows the loupe"));
        r.check(session(r) && session(r)->snapInfo(r.app().interaction().camera()).active, label("(while the pen places the corner)"));
        r.screenshot(shot + QStringLiteral("3_pen_and_palm"));
        r.touchRelease(s->palm);
        r.penMove(s->penTo);
    });
    steps.push_back([&r, s] { r.penRelease(s->penTo); });
    // Just after the pen lifts (inside the grace period): a two-finger tap
    // that would undo, and the palm landing again and sliding.
    steps.push_back([&r, s] {
        r.touchTap({s->palm, s->palm + QPointF(60, 25)});
        r.touchPress(s->palm);
    });
    for (int k = 0; k < 4; ++k)
        steps.push_back([&r, s] {
            s->palm -= QPointF(15, 10);
            r.touchMove(s->palm);
        });
    steps.push_back([&r, s] { r.touchRelease(s->palm); });
    wait(steps, 2);
    steps.push_back([&r, s, label, shot] {
        const auto* ss = session(r);
        const bool exact = hasPoint(r, {-10, -10}) && hasPoint(r, {10, -10}) && hasPoint(r, {10, 10}) && hasPoint(r, {-10, 10});
        r.check(exact && ss && ss->sketch().points().size() == 5, label("the pen's rectangle is exactly the face (no undo, no stray point)"),
                pointsText(r));
        const double area = ss && ss->regions().size() == 1 ? ss->regions().front().area : 0;
        r.check(std::abs(area - 400) < 1e-6, label("20 x 20 mm"), QString::number(area));
        const Camera& now = r.app().interaction().camera();
        r.check(sameCamera(now, s->camera), label("the palm moved nothing: the view is where it was"), cameraText(now));
        r.screenshot(shot + QStringLiteral("4_rectangle"));
    });
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("finishSketchButton")), label("Finish sketch")); });
    wait(steps, 5); // well past the grace period

    // ---- After the grace period a finger orbits again.
    steps.push_back([&r, s, label] {
        r.check(!r.app().sketchMode(), label("back to the model"));
        s->camera = r.app().interaction().camera();
        s->palm = palmSpot(r);
        r.touchPress(s->palm);
    });
    for (int k = 0; k < 4; ++k)
        steps.push_back([&r, s] {
            s->palm += QPointF(12, 6);
            r.touchMove(s->palm);
        });
    steps.push_back([&r, s] { r.touchRelease(s->palm); });
    wait(steps, 1);
    steps.push_back([&r, s, label] {
        const Camera& now = r.app().interaction().camera();
        r.check(std::abs(now.yaw - s->camera.yaw) > 1e-3, label("a finger after the grace period orbits"), cameraText(now));
    });
    // ---- A finger tap on the model in pen mode: the hint, once in a while.
    steps.push_back([&r, s] {
        r.app().interaction().cancelOperation(); // (the operation, then the selection)
        r.app().interaction().cancelOperation();
        s->hintsBefore = hints(s);
        r.touchTap({r.screenPoint(0, 0, 20)});
    });
    wait(steps, 2);
    steps.push_back([&r, s, label] {
        r.check(r.app().interaction().selection().empty(), label("in pen mode a finger tap selects nothing"));
        r.check(hints(s) == s->hintsBefore + 1, label("it says fingers move the view and how to select with a finger"),
                s->messages.isEmpty() ? QString() : s->messages.back());
        QQuickItem* toast = r.findItem(QStringLiteral("toastText"));
        r.check(toast && toast->property("text").toString() == QString::fromUtf8(interact::InteractionController::kPenFingerHint),
                label("shown in the toast"), toast ? toast->property("text").toString() : QString());
        r.touchTap({r.screenPoint(0, 0, 20)});
    });
    wait(steps, 2);
    steps.push_back([&r, s, label, shot] {
        r.check(hints(s) == s->hintsBefore + 1, label("a second tap right away does not repeat it"));
        r.screenshot(shot + QStringLiteral("5_hint"));
    });
    // ---- Pen off: a finger tap selects again.
    if (c.compact) {
        steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("viewMenuButton")), label("View")); });
        wait(steps, 2);
    }
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("penModeButton")), label("tap Pen")); });
    wait(steps, 2);
    steps.push_back([&r, label] {
        r.check(!r.app().penMode(), label("pen mode is off"));
        r.touchTap({r.screenPoint(0, 0, 20)});
    });
    wait(steps, 2);
    steps.push_back([&r, s, label, shot] {
        const auto& sel = r.app().interaction().selection();
        r.check(sel.size() == 1 && sel.items().front().kind == sel::SelectionKind::SketchProfile,
                label("a finger tap selects again (the rectangle's profile on the top face)"));
        r.screenshot(shot + QStringLiteral("6_finger_selects"));
        QObject::disconnect(s->listening);
    });
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    addFlow(out, r, {QStringLiteral("ipad"), 1180, 820, QVariantList{24, 0, 20, 0}, false});
    out.push_back([&r] { r.app().newDocument(); });
    wait(out, 3);
    addFlow(out, r, {QStringLiteral("phone"), 402, 874, QVariantList{62, 0, 34, 0}, true});
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("pencil"), 95, steps});

} // namespace
} // namespace os::app
