// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketching on a phone (owner, from an iPhone, 2026-09-27: "It's difficult to
// sketch on iPhone"; "if I start to draw on an object, I should have quick
// lock on points. Like if I have a cube and draw on one face, the four
// corners of the square face I'm drawing on should be apparent as lock on
// points"). With one finger through Qt's touch path, on the iPhone 16 Pro
// layout (402x874, the Dynamic Island's safe area) and an iPad (1180x820):
// a 20 mm cube's top face sketched on; a rectangle tapped near two opposite
// corners is exactly the face (400 mm2); a line from edge middle to edge
// middle; the loupe while a finger places a point (and what it costs); a
// corner dragged onto the face's corner (the loupe says "Corner"); a side's
// size label tapped (a 44 pt target) and typed; one finger on empty space
// pans without tilting; Face on after an orbit; Select pinned outside the
// phone's scrolling tool strip, six tools in sight, every button at least
// 44 pt; Finish sketch by the thumb.

#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
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
    QPointF finger;       // a finger held down
    Camera camera;        // before a pan
    double grabWithMs = 0; // a frame with the loupe
    sketch::EntityId side = sketch::kNoEntity;
};
using Shared = std::shared_ptr<State>;

void info(const QString& text)
{
    OS_LOG(Info, App) << "SKETCHPHONE " << text.toStdString();
}

interact::SketchSession* session(AcceptanceRunner& r)
{
    return r.app().interaction().sketchSession();
}

// Where the sketch point `local` is on screen.
QPointF at(AcceptanceRunner& r, Vec2 local)
{
    const auto* s = session(r);
    if (!s)
        return {};
    const Vec2 p = r.app().interaction().camera().project(s->sketch().plane().toWorld(local));
    return {p.x, p.y};
}

QRectF sceneRect(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return {};
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

QString rectText(const QRectF& rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x(), 0, 'f', 0).arg(rect.y(), 0, 'f', 0).arg(rect.width(), 0, 'f', 0).arg(rect.height(), 0, 'f', 0);
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

// Median time of `count` window grabs (a full frame each), ms.
double grabMs(AcceptanceRunner& r, int count)
{
    std::vector<double> ms;
    for (int i = 0; i < count; ++i) {
        QElapsedTimer timer;
        timer.start();
        (void)r.window()->grabWindow();
        ms.push_back(double(timer.nsecsElapsed()) / 1e6);
    }
    std::sort(ms.begin(), ms.end());
    return ms[ms.size() / 2];
}

void addFlow(Steps& steps, AcceptanceRunner& r, const Config& c)
{
    auto s = std::make_shared<State>();
    const QString shot = QStringLiteral("sketchphone_") + c.name + QStringLiteral("_");
    auto label = [c](const char* text) { return c.name + QStringLiteral(": ") + QString::fromUtf8(text); };
    steps.push_back([&r, c] {
        r.app().setTouchMode(true);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
    });
    wait(steps, 3);
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("tool_box")), label("Box")); });
    wait(steps, 3);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);
    // ---- The top face, Sketch.
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 20)}); });
    wait(steps, 2);
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("action_sketch")), label("Sketch on the top face")); });
    wait(steps, 5);
    // ---- Its outline is reference geometry: corners, edge middles, center.
    steps.push_back([&r, label, shot] {
        r.check(r.app().sketchMode(), label("sketching"));
        const auto* ss = session(r);
        if (!ss)
            return;
        int corners = 0;
        for (const auto& p : ss->references().points)
            corners += p.kind == interact::SketchReferences::PointKind::Corner ? 1 : 0;
        r.check(corners == 4 && ss->references().points.size() == 9 && ss->references().curves.size() == 4,
                label("the face's 4 corners, 4 edge middles and center are snap targets"),
                QString::number(ss->references().points.size()) + QStringLiteral(" points"));
        r.check(ss->tool() == interact::SketchTool::Rectangle, label("a new sketch opens in Rectangle"));
        r.screenshot(shot + QStringLiteral("1_face"));
    });
    // ---- A rectangle tapped near two opposite corners: exactly the face.
    steps.push_back([&r] { r.touchTap({at(r, {-10, 10}) + QPointF(9, 7)}); });
    wait(steps, 1);
    steps.push_back([&r] { r.touchTap({at(r, {10, -10}) + QPointF(-8, -6)}); });
    wait(steps, 2);
    steps.push_back([&r, label, shot] {
        const auto* ss = session(r);
        const bool exact = hasPoint(r, {-10, -10}) && hasPoint(r, {10, -10}) && hasPoint(r, {10, 10}) && hasPoint(r, {-10, 10});
        r.check(exact, label("the rectangle snapped to the corners is exactly the face"), pointsText(r));
        const double area = ss && ss->regions().size() == 1 ? ss->regions().front().area : 0;
        r.check(std::abs(area - 400) < 1e-6, label("20 x 20 mm"), QString::number(area));
        r.screenshot(shot + QStringLiteral("2_rectangle_on_corners"));
        if (ss && ss->sketch().lines().size() == 4)
            r.app().undo(); // the rectangle (never the sketch itself)
    });
    wait(steps, 2);
    // ---- A line from the middle of one edge to the middle of the other.
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("tool_line")), label("Line")); });
    wait(steps, 2);
    steps.push_back([&r] { r.touchTap({at(r, {10, 0}) + QPointF(-7, 6)}); });
    wait(steps, 1);
    steps.push_back([&r] { r.touchTap({at(r, {-10, 0}) + QPointF(6, -7)}); });
    wait(steps, 2);
    steps.push_back([&r, label] {
        r.check(hasPoint(r, {10, 0}) && hasPoint(r, {-10, 0}), label("a line from edge middle (10, 0) to edge middle (-10, 0)"),
                pointsText(r));
        r.check(r.tapItem(QStringLiteral("tool_select")), label("Select ends the line"));
    });
    wait(steps, 2);
    // ---- The loupe while a finger places a point.
    steps.push_back([&r, s] {
        r.tapItem(QStringLiteral("tool_line"));
        s->grabWithMs = 0;
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        s->finger = at(r, {3.3, -10}) + QPointF(0, -9); // on the bottom edge, 9 pt inside the face
        r.touchPress(s->finger);
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        s->finger += QPointF(2, 0);
        r.touchMove(s->finger);
    });
    wait(steps, 2);
    steps.push_back([&r, s, c, label, shot] {
        QQuickItem* loupe = r.findItem(QStringLiteral("sketchLoupe"));
        r.check(loupe && loupe->isVisible(), label("the loupe shows while the finger is down"));
        const QRectF box = sceneRect(r, QStringLiteral("sketchLoupe"));
        r.check(!box.isEmpty() && !box.adjusted(-22, -22, 22, 22).contains(s->finger),
                label("the loupe is beside the finger, not under it"), rectText(box));
        const QVariantList safe = c.safeArea.toList();
        const QRectF area(safe.value(3).toDouble(), safe.value(0).toDouble(),
                          c.width - safe.value(3).toDouble() - safe.value(1).toDouble(),
                          c.height - safe.value(0).toDouble() - safe.value(2).toDouble());
        r.check(area.adjusted(-0.5, -0.5, 0.5, 0.5).contains(box), label("the loupe is inside the safe area"), rectText(box));
        const QRectF bar = sceneRect(r, QStringLiteral("sketchToolbar"));
        r.check(!box.intersects(bar), label("the loupe is clear of the sketch tool bar"), rectText(bar));
        QQuickItem* hint = r.findItem(QStringLiteral("sketchLoupeHint"));
        r.check(hint && hint->property("text").toString() == QStringLiteral("On edge"), label("the loupe says On edge"),
                hint ? hint->property("text").toString() : QString());
        r.screenshot(shot + QStringLiteral("3_loupe"));
        s->grabWithMs = grabMs(r, 5);
    });
    steps.push_back([&r, s] { r.touchRelease(s->finger); });
    wait(steps, 2);
    steps.push_back([&r, s, label] {
        QQuickItem* loupe = r.findItem(QStringLiteral("sketchLoupe"));
        r.check(loupe && !loupe->isVisible(), label("the loupe goes when the finger lifts"));
        const double without = grabMs(r, 5);
        info(QStringLiteral("frame (window grab) with the loupe %1 ms, without %2 ms").arg(s->grabWithMs, 0, 'f', 2).arg(without, 0, 'f', 2));
        r.check(s->grabWithMs < without + 12, label("the loupe costs little per frame"),
                QStringLiteral("%1 ms with, %2 ms without").arg(s->grabWithMs, 0, 'f', 2).arg(without, 0, 'f', 2));
        r.check(r.tapItem(QStringLiteral("tool_select")), label("Select"));
        r.app().undo(); // the line
    });
    wait(steps, 2);
    // ---- A small rectangle; its corner dragged onto the face's corner.
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("tool_rectangle")), label("Rectangle")); });
    wait(steps, 2);
    steps.push_back([&r] { r.touchTap({at(r, {-5, -5})}); });
    wait(steps, 1);
    steps.push_back([&r] { r.touchTap({at(r, {5, 5})}); });
    wait(steps, 2);
    steps.push_back([&r, label] {
        r.check(hasPoint(r, {5, 5}) && hasPoint(r, {-5, -5}), label("a 10 x 10 rectangle"), pointsText(r));
        r.check(r.tapItem(QStringLiteral("tool_select")), label("Select"));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        s->finger = at(r, {5, -5});
        r.touchPress(s->finger);
    });
    for (int k = 1; k <= 6; ++k) {
        steps.push_back([&r, s, k] {
            const QPointF from = at(r, {5, -5}), to = at(r, {10, -10}) + QPointF(-7, -8);
            s->finger = from + (to - from) * (k / 6.0);
            r.touchMove(s->finger);
        });
    }
    wait(steps, 1);
    steps.push_back([&r, s, label, shot] {
        QQuickItem* hint = r.findItem(QStringLiteral("sketchLoupeHint"));
        QQuickItem* loupe = r.findItem(QStringLiteral("sketchLoupe"));
        r.check(loupe && loupe->isVisible() && hint && hint->property("text").toString() == QStringLiteral("Corner"),
                label("dragging a point: the loupe says Corner"), hint ? hint->property("text").toString() : QString());
        r.screenshot(shot + QStringLiteral("4_drag_to_corner"));
        r.touchRelease(s->finger);
    });
    wait(steps, 2);
    steps.push_back([&r, label] {
        r.check(hasPoint(r, {10, -10}) && !hasPoint(r, {5, -5}), label("the corner dropped exactly on the face's corner"), pointsText(r));
    });
    // ---- A side's size: a 44 pt tap target around a 30 pt pill; typed.
    steps.push_back([&r] { r.touchTap({at(r, {-5, 0.5})}); });
    wait(steps, 2);
    steps.push_back([&r, s, label] {
        const auto* ss = session(r);
        s->side = ss && ss->selection().size() == 1 ? ss->selection().front() : sketch::kNoEntity;
        r.check(s->side != sketch::kNoEntity && ss->sketch().line(s->side), label("the left side selected"));
        const QString name = QStringLiteral("sizeLabel_") + QString::number(s->side);
        QQuickItem* tap = r.findItem(name + QStringLiteral("_tap"));
        const QRectF pill = sceneRect(r, name);
        r.check(tap && tap->width() >= 44 && tap->height() >= 44, label("the size label's tap area is at least 44 x 44"),
                tap ? QStringLiteral("%1 x %2").arg(tap->width()).arg(tap->height()) : QString());
        r.check(pill.height() >= 30 - 0.5, label("the size pill is 30 pt tall"), rectText(pill));
        r.check(r.tapItem(name), label("tap the size"));
    });
    wait(steps, 2);
    steps.push_back([&r, label] {
        QQuickItem* editor = r.findItem(QStringLiteral("dimensionEditor"));
        r.check(editor && editor->isVisible(), label("the size opens for typing"));
        r.key(Qt::Key_A, Qt::ControlModifier);
        r.type(QStringLiteral("8"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 2);
    steps.push_back([&r, s, label, shot] {
        const auto* ss = session(r);
        const auto* l = ss ? ss->sketch().line(s->side) : nullptr;
        const double length = l ? (ss->sketch().point(l->end)->position - ss->sketch().point(l->start)->position).length() : 0;
        r.check(std::abs(length - 8) < 1e-6, label("the side is 8 mm"), QString::number(length));
        r.screenshot(shot + QStringLiteral("5_sized"));
    });
    // ---- One finger on empty space pans: the view never tilts off the sketch.
    steps.push_back([&r, c] {
        r.touchTap({at(r, c.compact ? Vec2{0, -12.5} : Vec2{-14, 0})}); // empty space: clears the selection
    });
    wait(steps, 2);
    steps.push_back([&r, s, c] {
        s->camera = r.app().interaction().camera();
        s->finger = at(r, c.compact ? Vec2{0, -12.5} : Vec2{-14, 0});
        r.touchPress(s->finger);
    });
    for (int k = 1; k <= 5; ++k)
        steps.push_back([&r, s] {
            s->finger += QPointF(12, 8);
            r.touchMove(s->finger);
        });
    steps.push_back([&r, s] { r.touchRelease(s->finger); });
    wait(steps, 2);
    steps.push_back([&r, s, label] {
        const Camera& now = r.app().interaction().camera();
        r.check(std::abs(now.pitch - s->camera.pitch) < 1e-9 && std::abs(now.yaw - s->camera.yaw) < 1e-9,
                label("a one-finger drag on empty space keeps the view facing the sketch"),
                QStringLiteral("pitch %1 -> %2").arg(s->camera.pitch).arg(now.pitch));
        r.check((now.target - s->camera.target).length() > 1, label("it panned"),
                QString::number((now.target - s->camera.target).length()) + QStringLiteral(" mm"));
    });
    // ---- Face on: back to facing the sketch after an orbit.
    steps.push_back([&r] {
        const Camera& now = r.app().interaction().camera();
        r.app().interaction().setViewAngles(now.yaw + 0.5, now.pitch - 0.5, false);
    });
    wait(steps, 2);
    steps.push_back([&r, label] {
        r.check(std::abs(r.app().interaction().camera().forward().z + 1) > 1e-3, label("orbited off the sketch"));
        r.check(r.tapItem(QStringLiteral("faceSketchButton")), label("Face on"));
    });
    wait(steps, 4);
    steps.push_back([&r, label, shot] {
        const Camera& now = r.app().interaction().camera();
        r.check(std::abs(now.forward().z + 1) < 1e-6, label("Face on faces the sketch again"), QString::number(now.forward().z));
        const double across = std::abs(at(r, {10, 0}).x() - at(r, {-10, 0}).x());
        r.check(across > 0.3 * r.window()->width(), label("and frames the face"), QString::number(across) + QStringLiteral(" pt"));
        r.screenshot(shot + QStringLiteral("6_face_on"));
    });
    // ---- Buttons: pinned Select, six tools in sight, 44 pt targets.
    steps.push_back([&r, c, label] {
        const QRectF strip = sceneRect(r, QStringLiteral("sketchToolPanel"));
        const QRectF scroll = sceneRect(r, QStringLiteral("sketchToolScroll"));
        int inSight = 0;
        QString sizes;
        for (const QString& id : {QStringLiteral("select"), QStringLiteral("trim"), QStringLiteral("line"), QStringLiteral("rectangle"),
                                  QStringLiteral("circle"), QStringLiteral("arc"), QStringLiteral("centerRectangle"),
                                  QStringLiteral("polygon"), QStringLiteral("tangentArc"), QStringLiteral("slot")}) {
            const QRectF b = sceneRect(r, QStringLiteral("tool_") + id);
            if (b.isEmpty())
                continue;
            const bool whole = c.compact ? strip.adjusted(-0.5, -0.5, 0.5, 0.5).contains(b)
                                               && (b.right() <= scroll.right() + 0.5 || id == QStringLiteral("select") || id == QStringLiteral("trim"))
                                         : true;
            inSight += whole ? 1 : 0;
            sizes += id + QStringLiteral(" ") + QString::number(b.width(), 'f', 0) + QStringLiteral("x") + QString::number(b.height(), 'f', 0) + QStringLiteral(" ");
            r.check(b.width() >= 44 - 0.5 && b.height() >= 44 - 0.5, label("tool button at least 44 pt: ") + id, rectText(b));
        }
        info(c.name + QStringLiteral(" tools: ") + sizes);
        r.check(inSight >= 6, label("six tools or more in sight"), QString::number(inSight));
        for (const QString& name : {QStringLiteral("finishSketchButton"), QStringLiteral("faceSketchButton")}) {
            const QRectF b = sceneRect(r, name);
            r.check(b.width() >= 44 - 0.5 && b.height() >= 44 - 0.5, label("at least 44 pt: ") + name, rectText(b));
        }
        if (c.compact) {
            const QRectF select = sceneRect(r, QStringLiteral("tool_select"));
            r.check(!select.isEmpty() && !scroll.intersects(select), label("Select is pinned outside the scrolling strip"),
                    rectText(select) + QStringLiteral(" / ") + rectText(scroll));
            const QRectF finish = sceneRect(r, QStringLiteral("finishSketchButton"));
            r.check(finish.top() > 0.75 * r.window()->height(), label("Finish sketch is near the bottom (by the thumb)"), rectText(finish));
        }
    });
    // ---- Finish.
    steps.push_back([&r, label] { r.check(r.tapItem(QStringLiteral("finishSketchButton")), label("Finish sketch")); });
    wait(steps, 4);
    steps.push_back([&r, label] { r.check(!r.app().sketchMode(), label("back to the model")); });
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    addFlow(out, r, {QStringLiteral("phone"), 402, 874, QVariantList{62, 0, 34, 0}, true});
    out.push_back([&r] { r.app().newDocument(); });
    wait(out, 3);
    addFlow(out, r, {QStringLiteral("ipad"), 1180, 820, QVariantList{24, 0, 20, 0}, false});
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("sketchphone"), 94, steps});

} // namespace
} // namespace os::app
