// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrude and cut, the owner's phone flow (2026-09-27): a 20 mm cube, its
// top face tapped, Sketch, a rectangle drawn with one finger from (-5,-5) to
// (5,5), Finish, the rectangle tapped and its arrow dragged 5 mm into the
// body: 7500 mm3. Then typed +5 with Cut chosen (Cut goes into the body),
// Flip (out again: a join, 8500 mm3), Cut chosen before typing 5,
// Join pushed in (refused, and the hint says why), the selected profile
// itself dragged (it moves the arrow, not the view), a stray second face
// before Sketch, and the Cut button in sight in the value box's row on a
// small phone and a phone held sideways. On the iPhone 16 Pro layout with
// finger input (Qt's touch path), then on a desktop window with the mouse.

#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "interaction/InteractionController.h"
#include "interaction/Manipulator.h"
#include "interaction/Operation.h"
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
    bool touch = false;
    QVariant safeArea;
};

struct State {
    QPointF last;     // the finger / mouse during a drag
    Vec3 grabWorld;   // the arrow point grabbed
    Vec3 direction;   // the arrow's direction
    double area = 0;  // the drawn rectangle's area
    std::vector<QPointF> path;
    Camera camera;    // before a drag that must not orbit
};

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

QString hintFull(AcceptanceRunner& r)
{
    QQuickItem* line = r.findItem(QStringLiteral("hintText"));
    return line ? line->property("full").toString() : QString();
}

void info(const QString& text)
{
    OS_LOG(Info, App) << "EXTRUDETOUCH " << text.toStdString();
}

// A tap / press / move / release with the configuration's input.
void select(AcceptanceRunner& r, const Config& c, QPointF p)
{
    if (c.touch)
        r.touchTap({p});
    else
        r.click(p);
}
void press(AcceptanceRunner& r, const Config& c, QPointF p)
{
    if (c.touch)
        r.touchPress(p);
    else
        r.mousePress(p);
}
void move(AcceptanceRunner& r, const Config& c, QPointF p)
{
    if (c.touch)
        r.touchMove(p);
    else
        r.mouseMove(p, Qt::LeftButton);
}
void release(AcceptanceRunner& r, const Config& c, QPointF p)
{
    if (c.touch)
        r.touchRelease(p);
    else
        r.mouseRelease(p);
}
bool button(AcceptanceRunner& r, const Config& c, const QString& name)
{
    return c.touch ? r.tapItem(name) : r.clickItem(name);
}

QPointF onScreen(AcceptanceRunner& r, const Vec3& w) { return r.screenPoint(w.x, w.y, w.z); }

// The rectangle drawn in the open sketch: its corners (lo, hi).
std::pair<Vec2, Vec2> drawnRectangle(AcceptanceRunner& r)
{
    Vec2 lo{1e9, 1e9}, hi{-1e9, -1e9};
    const auto* session = r.app().interaction().sketchSession();
    if (!session)
        return {lo, hi};
    const auto& s = session->sketch();
    for (const auto& [id, l] : s.lines())
        for (auto e : {l.start, l.end}) {
            const Vec2 q = s.point(e)->position;
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y)};
        }
    return {lo, hi};
}

// Presses the active arrow (60 % along its shaft) and moves `delta` mm
// along it (negative: against it) in `count` moves; the release is a step later.
void dragArrow(AcceptanceRunner& r, const Config& c, const std::shared_ptr<State>& s, double delta, int count)
{
    const auto* op = r.app().interaction().operation();
    if (!op) {
        r.check(false, c.name + QStringLiteral(": an operation to drag"));
        return;
    }
    const int i = op->activeHandle();
    const interact::LinearManipulator h = op->handle(i);
    const Vec3 anchor = h.anchor(op->handleOffset(i));
    const interact::ArrowStyle style;
    const double px = r.app().interaction().camera().pixelSize(anchor);
    s->direction = h.direction();
    s->grabWorld = anchor + h.direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
    const QPointF grab = onScreen(r, s->grabWorld);
    press(r, c, grab);
    r.check(r.app().manipulatorDragging(), c.name + QStringLiteral(": the arrow is grabbed"));
    for (int k = 1; k <= count; ++k) {
        s->last = onScreen(r, s->grabWorld + s->direction * (delta * k / count));
        move(r, c, s->last);
    }
}

bool actionChecked(AcceptanceRunner& r, const QString& id)
{
    QQuickItem* b = r.findItem(QStringLiteral("action_") + id);
    return b && b->isVisible() && b->property("checked").toBool();
}

void addFlow(Steps& steps, AcceptanceRunner& r, const Config& c)
{
    auto s = std::make_shared<State>();
    const QString shot = QStringLiteral("extrudetouch_") + c.name + QStringLiteral("_");
    steps.push_back([&r, c] {
        r.app().setTouchMode(c.touch);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
    });
    wait(steps, 3);
    steps.push_back([&r, c] { r.check(button(r, c, QStringLiteral("tool_box")), c.name + QStringLiteral(": Box")); });
    wait(steps, 3);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);
    // ---- The top face, Sketch on it: framed in the free part of the view.
    steps.push_back([&r, c] { select(r, c, r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), c.name + QStringLiteral(": the top face"), r.app().operationTitle());
        r.check(button(r, c, QStringLiteral("action_sketch")), c.name + QStringLiteral(": Sketch in the value box"));
    });
    wait(steps, 5);
    steps.push_back([&r, c, shot] {
        r.check(r.app().sketchMode(), c.name + QStringLiteral(": sketching on the top face"));
        const QVariantList frame = r.app().frameInsets();
        info(c.name + QStringLiteral(" frame insets ") + QString::number(frame.value(0).toDouble()) + QStringLiteral(" ")
             + QString::number(frame.value(1).toDouble()) + QStringLiteral(" ") + QString::number(frame.value(2).toDouble())
             + QStringLiteral(" ") + QString::number(frame.value(3).toDouble()));
        const double pxPerMm = 1 / r.app().interaction().camera().pixelSize({0, 0, 20});
        r.check(pxPerMm > 12, c.name + QStringLiteral(": the face is framed (1 mm steps)"), QString::number(pxPerMm) + QStringLiteral(" px/mm"));
        // The face clear of the tool bar above and the tools below.
        const QPointF a = r.screenPoint(-10, -10, 20), b = r.screenPoint(10, 10, 20);
        const QRectF face = QRectF(a, b).normalized();
        const QRectF bar = sceneRect(r, QStringLiteral("sketchToolbar"));
        const QRectF tools = sceneRect(r, QStringLiteral("sketchToolPanel"));
        r.check(face.top() > bar.bottom() && (!c.touch || face.bottom() < tools.top()) && face.left() >= 0
                    && face.right() <= r.window()->width(),
                c.name + QStringLiteral(": the face is in the free part of the view"),
                rectText(face) + QStringLiteral(" bar ") + rectText(bar) + QStringLiteral(" tools ") + rectText(tools));
        r.screenshot(shot + QStringLiteral("1_sketch"));
        // One drag from (-5,-5) to (5,5) with the Rectangle tool.
        const auto* session = r.app().interaction().sketchSession();
        if (!session)
            return;
        const sketch::Plane plane = session->sketch().plane();
        const QPointF from = onScreen(r, plane.toWorld({-5, -5})), to = onScreen(r, plane.toWorld({5, 5}));
        press(r, c, from);
        for (int i = 1; i <= 8; ++i)
            move(r, c, from + (to - from) * (i / 8.0));
        release(r, c, to);
    });
    wait(steps, 2);
    steps.push_back([&r, c, s, shot] {
        const auto [lo, hi] = drawnRectangle(r);
        s->area = (hi.x - lo.x) * (hi.y - lo.y);
        r.check(std::abs(lo.x + 5) < 1e-9 && std::abs(lo.y + 5) < 1e-9 && std::abs(hi.x - 5) < 1e-9 && std::abs(hi.y - 5) < 1e-9,
                c.name + QStringLiteral(": a drag from (-5,-5) to (5,5) makes exactly that rectangle"),
                QStringLiteral("(%1, %2) .. (%3, %4)").arg(lo.x).arg(lo.y).arg(hi.x).arg(hi.y));
        r.screenshot(shot + QStringLiteral("2_rectangle"));
        r.check(button(r, c, QStringLiteral("finishSketchButton")), c.name + QStringLiteral(": Finish sketch"));
    });
    wait(steps, 5);
    // ---- The rectangle tapped; its arrow dragged 5 mm into the body.
    steps.push_back([&r, c] { select(r, c, r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, c, s, shot] {
        auto& app = r.app();
        r.check(app.operationTitle() == QStringLiteral("Extrude"), c.name + QStringLiteral(": the rectangle arms Extrude"), app.operationTitle());
        const QString hint = hintFull(r);
        r.check(hint.contains(QStringLiteral("into it to cut")), c.name + QStringLiteral(": the hint says pushing in cuts"), hint);
        const auto* op = app.interaction().operation();
        if (op)
            r.check((op->handle(0).base() - Vec3{0, 0, 20}).length() < 0.5,
                    c.name + QStringLiteral(": the arrow starts where the rectangle was tapped"));
        r.screenshot(shot + QStringLiteral("3_profile"));
        dragArrow(r, c, s, -5.0, 10);
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] { release(r, c, s->last); });
    wait(steps, 2);
    steps.push_back([&r, c, shot] {
        auto& app = r.app();
        r.check(app.operationValueLabel() == QStringLiteral("Cut depth"), c.name + QStringLiteral(": the value says Cut depth"),
                app.operationValueLabel());
        r.check(app.operationValueText().startsWith(QStringLiteral("-5")), c.name + QStringLiteral(": 5 mm into the body"),
                app.operationValueText());
        r.check(actionChecked(r, QStringLiteral("mode:cut")), c.name + QStringLiteral(": Cut is chosen by the inward drag"));
        r.screenshot(shot + QStringLiteral("4_dragged_in"));
        r.check(button(r, c, QStringLiteral("valueChipApply")), c.name + QStringLiteral(": ✓"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - 7500) < 1e-3,
                c.name + QStringLiteral(": 20 mm cube minus 10 x 10 x 5 = 7500"), AcceptanceRunner::num(r.bodyVolume()));
        r.check(button(r, c, QStringLiteral("undoButton")), c.name + QStringLiteral(": Undo"));
    });
    wait(steps, 2);
    // ---- Typed 5, then Cut: into the body.
    steps.push_back([&r, c] {
        r.check(std::abs(r.bodyVolume() - 8000) < 1e-6, c.name + QStringLiteral(": Undo restores the cube"), AcceptanceRunner::num(r.bodyVolume()));
        select(r, c, r.screenPoint(0, 0, 20));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(button(r, c, QStringLiteral("valueChipField")), c.name + QStringLiteral(": the value field"));
        r.type(QStringLiteral("5"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().operationValueLabel() == QStringLiteral("Height"), c.name + QStringLiteral(": +5 is a height"),
                r.app().operationValueLabel());
        r.key(Qt::Key_Escape); // out of the field; the value stays
    });
    wait(steps, 2);
    steps.push_back([&r, c] { r.check(button(r, c, QStringLiteral("action_mode:cut")), c.name + QStringLiteral(": Cut in the value box")); });
    wait(steps, 2);
    steps.push_back([&r, c, shot] {
        auto& app = r.app();
        r.check(app.operationCanCommit() && app.operationValueText().startsWith(QStringLiteral("-5")),
                c.name + QStringLiteral(": Cut turns +5 into the body"), app.operationValueText() + QStringLiteral(" ") + app.operationError());
        r.check(hintFull(r).startsWith(QStringLiteral("Cuts it out of the body")), c.name + QStringLiteral(": the hint says it cuts"),
                hintFull(r));
        r.screenshot(shot + QStringLiteral("5_plus5_cut"));
        // Flip: out of the body again, an automatic Join.
        r.check(button(r, c, QStringLiteral("action_flip")), c.name + QStringLiteral(": Flip in the value box"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        auto& app = r.app();
        // The phone's compact row scrolls sideways: Flip was brought into sight there.
        const QRectF flip = sceneRect(r, QStringLiteral("action_flip"));
        const QRectF row = c.touch ? sceneRect(r, QStringLiteral("valueChipActions")) : QRectF(0, 0, r.window()->width(), r.window()->height());
        r.check(!flip.isEmpty() && row.adjusted(-0.5, -0.5, 0.5, 0.5).contains(flip),
                c.name + QStringLiteral(": Flip is in sight"), rectText(flip) + QStringLiteral(" in ") + rectText(row));
        r.check(app.operationValueText().startsWith(QStringLiteral("5")) && app.operationValueLabel() == QStringLiteral("Height"),
                c.name + QStringLiteral(": Flip turns the cut outward (+5, Height)"),
                app.operationValueLabel() + QStringLiteral(" ") + app.operationValueText());
        r.check(actionChecked(r, QStringLiteral("mode:join")) && app.operationCanCommit(),
                c.name + QStringLiteral(": out of the body joins"), app.operationError());
        r.check(hintFull(r).startsWith(QStringLiteral("Adds it to the body")), c.name + QStringLiteral(": the hint says it adds"),
                hintFull(r));
        r.check(button(r, c, QStringLiteral("valueChipApply")), c.name + QStringLiteral(": ✓ (flipped)"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(std::abs(r.bodyVolume() - 8500) < 1e-3, c.name + QStringLiteral(": flipped, 10 x 10 x 5 joined = 8500"),
                AcceptanceRunner::num(r.bodyVolume()));
        r.check(button(r, c, QStringLiteral("undoButton")), c.name + QStringLiteral(": Undo (flipped)"));
    });
    wait(steps, 2);
    // ---- Cut chosen first, then 5 typed (no minus sign): into the body.
    steps.push_back([&r, c] { select(r, c, r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, c] { r.check(button(r, c, QStringLiteral("action_mode:cut")), c.name + QStringLiteral(": Cut before a value")); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(button(r, c, QStringLiteral("valueChipField")), c.name + QStringLiteral(": the value field (Cut chosen)"));
        r.type(QStringLiteral("5"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        auto& app = r.app();
        r.check(app.operationValueLabel() == QStringLiteral("Cut depth") && app.operationValueText().startsWith(QStringLiteral("-5")),
                c.name + QStringLiteral(": 5 typed with Cut chosen goes in"), app.operationValueLabel() + QStringLiteral(" ") + app.operationValueText());
        r.key(Qt::Key_Escape); // out of the field; the value stays
    });
    wait(steps, 2);
    steps.push_back([&r, c] { r.check(button(r, c, QStringLiteral("valueChipApply")), c.name + QStringLiteral(": ✓ (Cut chosen)")); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(std::abs(r.bodyVolume() - 7500) < 1e-3, c.name + QStringLiteral(": typed 5 with Cut cuts 5 deep"),
                AcceptanceRunner::num(r.bodyVolume()));
        r.check(button(r, c, QStringLiteral("undoButton")), c.name + QStringLiteral(": Undo (Cut chosen)"));
    });
    wait(steps, 2);
    // ---- Join chosen, then pushed in: refused, and the hint says why.
    steps.push_back([&r, c] { select(r, c, r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, c] { r.check(button(r, c, QStringLiteral("action_mode:join")), c.name + QStringLiteral(": Join in the value box")); });
    wait(steps, 2);
    steps.push_back([&r, c, s] { dragArrow(r, c, s, -4.0, 8); });
    wait(steps, 2);
    steps.push_back([&r, c, s] { release(r, c, s->last); });
    wait(steps, 2);
    steps.push_back([&r, c, shot] {
        auto& app = r.app();
        const QString refusal =
            QStringLiteral("This extrusion stays inside the body, so nothing would be added. Pull it outward, or choose Cut.");
        r.check(!app.operationCanCommit() && app.operationError() == refusal, c.name + QStringLiteral(": Join pushed in is refused"),
                app.operationError());
        r.check(hintFull(r) == refusal, c.name + QStringLiteral(": the hint says why (not '✓ applies')"), hintFull(r));
        r.screenshot(shot + QStringLiteral("6_join_refused"));
        r.check(button(r, c, QStringLiteral("valueChipCancel")), c.name + QStringLiteral(": ✕"));
    });
    wait(steps, 2);
    if (c.touch) {
        // ---- A finger dragging the selected rectangle itself pushes it in.
        steps.push_back([&r, c] { select(r, c, r.screenPoint(0, 0, 20)); });
        wait(steps, 2);
        steps.push_back([&r, c, s] {
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), c.name + QStringLiteral(": the rectangle again"));
            s->camera = r.app().interaction().camera();
            s->last = r.screenPoint(-4, -4, 20); // beside the arrow on screen
            press(r, c, s->last);
            for (int i = 1; i <= 8; ++i)
                move(r, c, s->last + QPointF(0, 5 * i));
            s->last += QPointF(0, 40);
        });
        wait(steps, 2);
        steps.push_back([&r, c, s] { release(r, c, s->last); });
        wait(steps, 2);
        steps.push_back([&r, c, s, shot] {
            auto& app = r.app();
            const Camera& now = app.interaction().camera();
            r.check(std::abs(now.yaw - s->camera.yaw) < 1e-9 && std::abs(now.pitch - s->camera.pitch) < 1e-9,
                    c.name + QStringLiteral(": dragging the rectangle does not orbit"));
            r.check(app.operationValueLabel() == QStringLiteral("Cut depth") && app.operationValueText().startsWith(QStringLiteral("-")),
                    c.name + QStringLiteral(": dragging the rectangle down pushes it in"), app.operationValueText());
            r.screenshot(shot + QStringLiteral("7_region_drag"));
            r.check(button(r, c, QStringLiteral("valueChipCancel")), c.name + QStringLiteral(": ✕ (region drag)"));
        });
        wait(steps, 2);
    }
    // ---- A stray face first: Sketch goes on the last one tapped.
    steps.push_back([&r, c] {
        if (!r.app().interaction().selection().empty())
            select(r, c, r.screenPoint(60, 60, 60)); // empty space
    });
    wait(steps, 2);
    steps.push_back([&r, c] { select(r, c, r.screenPoint(0, -10, 10)); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        // A second face: taps add on touch, Shift-click with a mouse.
        if (c.touch)
            r.touchTap({r.screenPoint(6, 6, 20)});
        else
            r.click(r.screenPoint(6, 6, 20), Qt::ShiftModifier);
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().interaction().selection().size() == 2, c.name + QStringLiteral(": two faces selected"),
                QString::number(r.app().interaction().selection().size()));
        r.check(button(r, c, QStringLiteral("sketchButton")), c.name + QStringLiteral(": Sketch with two faces selected"));
    });
    wait(steps, 5);
    steps.push_back([&r, c] {
        const auto* session = r.app().interaction().sketchSession();
        r.check(session && session->sketch().plane().normal().z > 0.999,
                c.name + QStringLiteral(": the sketch is on the face tapped last (the top)"));
        r.check(button(r, c, QStringLiteral("finishSketchButton")), c.name + QStringLiteral(": Finish sketch (empty)"));
    });
    wait(steps, 4);
}

// The value box's row with a cut: New body, Join and Cut first, all in sight.
void addRowCheck(Steps& steps, AcceptanceRunner& r, const QString& name, int width, int height, const QVariantList& safe)
{
    steps.push_back([&r, width, height, safe] {
        r.app().setTouchMode(true);
        r.resizeWindow(width, height);
        r.window()->setProperty("simulatedSafeArea", safe);
    });
    wait(steps, 3);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);
    steps.push_back([&r] {
        if (!r.app().interaction().selection().empty())
            r.touchTap({r.screenPoint(60, 60, 60)});
    });
    wait(steps, 2);
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 20)}); });
    wait(steps, 2);
    steps.push_back([&r] { r.app().setValueText(QStringLiteral("-4")); });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), name + QStringLiteral(": extruding"), r.app().operationTitle());
        const QRectF row = sceneRect(r, QStringLiteral("valueChipActions"));
        for (const QString& id : {QStringLiteral("mode:new"), QStringLiteral("mode:join"), QStringLiteral("mode:cut")}) {
            const QRectF b = sceneRect(r, QStringLiteral("action_") + id);
            r.check(!b.isEmpty() && row.adjusted(-0.5, -0.5, 0.5, 0.5).contains(b),
                    name + QStringLiteral(": ") + id + QStringLiteral(" fully in sight in the value box"),
                    rectText(b) + QStringLiteral(" in ") + rectText(row));
        }
        r.screenshot(QStringLiteral("extrudetouch_row_") + name);
    });
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    const Config phone{QStringLiteral("phone"), 402, 874, true, QVariantList{62, 0, 34, 0}};
    addFlow(out, r, phone);
    addRowCheck(out, r, QStringLiteral("iphoneSE_375x667"), 375, 667, QVariantList{20, 0, 0, 0});
    addRowCheck(out, r, QStringLiteral("landscape_874x402"), 874, 402, QVariantList{0, 62, 21, 62});
    out.push_back([&r] { r.app().newDocument(); });
    wait(out, 3);
    addFlow(out, r, {QStringLiteral("desktop"), 1400, 900, false, QVariant()});
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("extrudetouch"), 93, steps});

} // namespace
} // namespace os::app
