// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Editing a sketch by dragging (owner, from an iPhone, 2026-09-27: "really
// difficult to edit a sketch. In Shapr3D, you can modify a sketch by
// clicking and dragging unconstrained lines"; "I have no idea how to resize
// rectangles"). Twice: on the desktop with the mouse, then on an iPhone 16
// Pro (402x874, the Dynamic Island's safe area) with one finger. Each time:
// a rectangle side dragged out, a circle's rim dragged (its size), the whole
// rectangle dragged from inside, a side tapped and its size typed, a line's
// end dropped on a corner (joined), the rectangle double-tapped (the whole
// chain, the line too) and deleted, and a tap inside the circle -> Extrude
// -> a measured volume.

#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "document/Body.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/Manipulator.h"
#include "ui/AppController.h"

#include <QtCore/QVariantList>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

struct State {
    bool touch = false;
    QString where;          // "mouse" / "finger": in check descriptions
    double u = 10;          // mm: the layout's unit, a whole number of grid steps
    sketch::EntityId top = sketch::kNoEntity;
    sketch::EntityId circle = sketch::kNoEntity;
    std::size_t pointsBefore = 0;
};
using Shared = std::shared_ptr<State>;

const sketch::Sketch* activeSketch(AcceptanceRunner& r)
{
    const auto* session = r.app().interaction().sketchSession();
    return session ? &session->sketch() : nullptr;
}

interact::SketchSession* session(AcceptanceRunner& r)
{
    return r.app().interaction().sketchSession();
}

// Where the sketch point (x, y) (in units) is on screen.
QPointF at(AcceptanceRunner& r, const Shared& s, double x, double y)
{
    return r.screenPoint(x * s->u, y * s->u, 0);
}

bool hasPoint(AcceptanceRunner& r, const Shared& s, double x, double y)
{
    const auto* sk = activeSketch(r);
    if (!sk)
        return false;
    const Vec2 q{x * s->u, y * s->u};
    return std::any_of(sk->points().begin(), sk->points().end(),
                       [&](const auto& p) { return (p.second.position - q).length() < 1e-6; });
}

QString points(AcceptanceRunner& r)
{
    QString out;
    if (const auto* sk = activeSketch(r))
        for (const auto& [id, p] : sk->points())
            out += QStringLiteral("(%1, %2) ").arg(p.position.x, 0, 'f', 3).arg(p.position.y, 0, 'f', 3);
    return out;
}

sketch::EntityId lineAt(AcceptanceRunner& r, const Shared& s, Vec2 a, Vec2 b)
{
    const auto* sk = activeSketch(r);
    if (!sk)
        return sketch::kNoEntity;
    a = a * s->u;
    b = b * s->u;
    for (const auto& [id, l] : sk->lines()) {
        const Vec2 p = sk->point(l.start)->position, q = sk->point(l.end)->position;
        if (((p - a).length() < 1e-6 && (q - b).length() < 1e-6) || ((p - b).length() < 1e-6 && (q - a).length() < 1e-6))
            return id;
    }
    return sketch::kNoEntity;
}

double lineLength(AcceptanceRunner& r, sketch::EntityId id)
{
    const auto* sk = activeSketch(r);
    const auto* l = sk ? sk->line(id) : nullptr;
    return l ? (sk->point(l->end)->position - sk->point(l->start)->position).length() : -1;
}

void tap(AcceptanceRunner& r, const Shared& s, QPointF p)
{
    if (s->touch)
        r.touchTap({p});
    else
        r.click(p);
}

// A press, four moves and a release, one per step (Qt Quick holds a
// finger's moves until its next frame; the steps give it frames). The ends
// (in units) are read when the drag runs.
void drag(Steps& steps, AcceptanceRunner& r, const Shared& s, std::function<Vec2()> from, std::function<Vec2()> to)
{
    steps.push_back([&r, s, from] {
        const Vec2 f = from();
        const QPointF p = at(r, s, f.x, f.y);
        if (s->touch) {
            r.touchPress(p);
        } else {
            r.mouseMove(p);
            r.mousePress(p);
        }
    });
    for (int i = 1; i <= 4; ++i)
        steps.push_back([&r, s, from, to, i] {
            const Vec2 q = from() + (to() - from()) * (i / 4.0);
            const QPointF p = at(r, s, q.x, q.y);
            if (s->touch)
                r.touchMove(p);
            else
                r.mouseMove(p, Qt::LeftButton);
        });
    steps.push_back([&r, s, to] {
        const Vec2 t = to();
        const QPointF p = at(r, s, t.x, t.y);
        if (s->touch)
            r.touchRelease(p);
        else
            r.mouseRelease(p);
    });
}

void drag(Steps& steps, AcceptanceRunner& r, const Shared& s, Vec2 from, Vec2 to)
{
    drag(steps, r, s, [from] { return from; }, [to] { return to; });
}

// The rectangle's bottom-left corner (in units): the point with the least x + y.
Vec2 bottomLeft(AcceptanceRunner& r, const Shared& s)
{
    Vec2 best{0, 0};
    double least = 1e18;
    if (const auto* sk = activeSketch(r))
        for (const auto& [id, l] : sk->lines())
            for (const auto end : {l.start, l.end}) {
                const Vec2 p = sk->point(end)->position;
                if (p.x > -1.5 * s->u && p.x + p.y < least) { // not the free line at x = -5 units
                    least = p.x + p.y;
                    best = p * (1.0 / s->u);
                }
            }
    return best;
}

Steps part(AcceptanceRunner& r, bool touch)
{
    auto s = std::make_shared<State>();
    s->touch = touch;
    s->where = touch ? QStringLiteral("finger") : QStringLiteral("mouse");
    Steps steps;
    if (touch) {
        // A new document on an iPhone 16 Pro in portrait.
        steps.push_back([&r] {
            r.app().newDocument();
            r.app().setTouchMode(true);
            r.resizeWindow(402, 874);
            r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
        });
        wait(steps, 3);
    }
    // ---- A sketch on the ground: a rectangle (-2, 1)..(2, 3) and a circle at (0, -3), radius 2 (units).
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("sketchButton")), s->where + QStringLiteral(": Sketch"));
    });
    wait(steps, 2);
    steps.push_back([&r, s] { r.check(r.clickItem(QStringLiteral("planeTop")), s->where + QStringLiteral(": on the ground")); });
    wait(steps, 4); // the view turns to face the sketch
    steps.push_back([&r, s] {
        r.check(r.app().sketchMode(), s->where + QStringLiteral(": sketching"));
        // The unit: about 28 px on a phone, 40 px on the desktop, in whole grid steps.
        const Camera& camera = r.app().interaction().camera();
        const double pixel = camera.pixelSize({0, 0, 0});
        const double step = interact::snapIncrement(pixel, 10.0);
        s->u = step * std::max(1.0, std::round((s->touch ? 28.0 : 40.0) * pixel / step));
        OS_LOG(Info, App) << "sketchdrag: " << pixel << " mm/px, grid " << step << " mm, unit " << s->u << " mm";
        r.check(r.clickItem(QStringLiteral("tool_rectangle")), s->where + QStringLiteral(": Rectangle tool"));
    });
    drag(steps, r, s, {-2, 1}, {2, 3});
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        r.check(sk && sk->lines().size() == 4 && hasPoint(r, s, -2, 1) && hasPoint(r, s, 2, 3),
                s->where + QStringLiteral(": a rectangle drawn by dragging"), points(r));
        r.check(r.clickItem(QStringLiteral("tool_circle")), s->where + QStringLiteral(": Circle tool"));
    });
    wait(steps, 3);
    steps.push_back([&r, s] { tap(r, s, at(r, s, 0, -3)); });
    wait(steps, 3);
    steps.push_back([&r, s] { tap(r, s, at(r, s, 2, -3)); });
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        r.check(sk && sk->circles().size() == 1 && std::abs(sk->circles().begin()->second.radius - 2 * s->u) < 1e-6,
                s->where + QStringLiteral(": a circle of radius 2 units"),
                sk && !sk->circles().empty() ? AcceptanceRunner::num(sk->circles().begin()->second.radius) : QString());
        if (sk && !sk->circles().empty())
            s->circle = sk->circles().begin()->first;
        r.check(r.clickItem(QStringLiteral("tool_select")), s->where + QStringLiteral(": Select tool"));
        s->top = lineAt(r, s, {-2, 3}, {2, 3});
        r.check(s->top != sketch::kNoEntity, s->where + QStringLiteral(": the top side is found"));
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_1_drawn"));
    });
    wait(steps, 3);

    // ---- Drag the top side up by one: the rectangle grows.
    drag(steps, r, s, {0, 3}, {0, 4});
    steps.push_back([&r, s] {
        r.check(hasPoint(r, s, -2, 4) && hasPoint(r, s, 2, 4) && hasPoint(r, s, -2, 1) && hasPoint(r, s, 2, 1),
                s->where + QStringLiteral(": dragging the top side moved it (the bottom stayed)"), points(r));
        r.check(r.app().undoText() == QStringLiteral("Move line"), s->where + QStringLiteral(": one step, Move line"),
                r.app().undoText());
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_2_side_dragged"));
    });
    wait(steps, 3);

    // ---- The circle's rim, from its bottom, out by one: its radius is 3 units.
    drag(steps, r, s, {0, -5}, {0, -6});
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        const auto* c = sk ? sk->circle(s->circle) : nullptr;
        const Vec2 center = c ? sk->point(c->center)->position : Vec2{};
        r.check(c && std::abs(c->radius - 3 * s->u) < 1e-6 && (center - Vec2{0, -3 * s->u}).length() < 1e-6,
                s->where + QStringLiteral(": dragging the rim resized the circle, its center stayed"),
                c ? AcceptanceRunner::num(c->radius) : QString());
        r.check(r.app().undoText() == QStringLiteral("Resize circle"), s->where + QStringLiteral(": Resize circle"),
                r.app().undoText());
    });
    wait(steps, 3);

    // ---- From inside the rectangle, right by one: the whole shape moves.
    drag(steps, r, s, {0, 2.5}, {1, 2.5});
    steps.push_back([&r, s] {
        r.check(hasPoint(r, s, -1, 1) && hasPoint(r, s, 3, 1) && hasPoint(r, s, 3, 4) && hasPoint(r, s, -1, 4),
                s->where + QStringLiteral(": dragging inside moved the whole rectangle"), points(r));
        r.check(r.app().undoText() == QStringLiteral("Move shape"), s->where + QStringLiteral(": Move shape"), r.app().undoText());
        const auto* sk = activeSketch(r);
        const auto* c = sk ? sk->circle(s->circle) : nullptr;
        r.check(c && (sk->point(c->center)->position - Vec2{0, -3 * s->u}).length() < 1e-6,
                s->where + QStringLiteral(": the circle stayed"));
        r.check(std::abs(r.app().interaction().camera().forward().z + 1) < 1e-9,
                s->where + QStringLiteral(": the view did not orbit"));
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_3_shape_moved"));
    });
    wait(steps, 3);

    // ---- Tap the top side, then its size, and type 5 units: the rectangle is 5 wide.
    steps.push_back([&r, s] { tap(r, s, at(r, s, 1, 4)); });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        r.check(ss && ss->selection() == std::vector<sketch::EntityId>{s->top}, s->where + QStringLiteral(": the top side is selected"),
                ss ? QString::number(ss->selection().size()) : QString());
        QQuickItem* size = r.findItem(QStringLiteral("sizeLabel_%1").arg(int(s->top)));
        QString text;
        if (ss)
            for (const auto& label : ss->labels(r.app().interaction().camera()))
                if (label.kind == interact::SketchLabel::Kind::Size && label.entity == s->top)
                    text = QString::fromStdString(label.text);
        r.check(size && size->isVisible(), s->where + QStringLiteral(": its length shows as a size to tap"), text);
        r.check(text == QString::number(4 * s->u), s->where + QStringLiteral(": the size reads 4 units"), text);
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_3b_size_shown"));
        if (size) {
            const QPointF c = size->mapToScene(QPointF(size->width() / 2, size->height() / 2));
            if (s->touch)
                r.touchTap({c});
            else
                r.click(c);
        }
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        QQuickItem* editor = r.findItem(QStringLiteral("dimensionEditor"));
        r.check(editor && editor->isVisible() && editor->hasActiveFocus(), s->where + QStringLiteral(": tapping the size opens its editor"));
        r.type(QString::number(5 * s->u));
        r.key(Qt::Key_Return);
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        std::size_t distances = 0;
        if (sk)
            for (const auto& [id, c] : sk->constraints())
                distances += c.kind == sketch::ConstraintKind::Distance ? 1 : 0;
        r.check(std::abs(lineLength(r, s->top) - 5 * s->u) < 1e-6 && distances == 1,
                s->where + QStringLiteral(": typing the size set the side's length (a dimension now)"),
                AcceptanceRunner::num(lineLength(r, s->top)));
        double bottom = -1; // the other horizontal side's length
        if (sk)
            for (const auto& [id, l] : sk->lines())
                if (id != s->top && std::abs(sk->point(l.start)->position.y - s->u) < 1e-6
                    && std::abs(sk->point(l.end)->position.y - s->u) < 1e-6)
                    bottom = lineLength(r, id);
        r.check(std::abs(bottom - 5 * s->u) < 1e-6, s->where + QStringLiteral(": the rectangle is 5 units wide"), points(r));
        r.check(r.app().undoText() == QStringLiteral("Length"), s->where + QStringLiteral(": Length"), r.app().undoText());
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_4_sized"));
    });
    wait(steps, 3);

    // ---- A line from (-5, 5) to (-5, 2); its lower end dropped on the rectangle's corner joins them.
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("tool_line")), s->where + QStringLiteral(": Line tool"));
        tap(r, s, at(r, s, -5, 5));
    });
    wait(steps, 3);
    steps.push_back([&r, s] { tap(r, s, at(r, s, -5, 2)); });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("tool_select")), s->where + QStringLiteral(": Select again"));
        const auto* sk = activeSketch(r);
        r.check(sk && sk->lines().size() == 5, s->where + QStringLiteral(": a line beside the rectangle"),
                sk ? QString::number(sk->lines().size()) : QString());
        s->pointsBefore = sk ? sk->points().size() : 0;
    });
    wait(steps, 3);
    drag(steps, r, s, [] { return Vec2{-5, 2}; }, [&r, s] { return bottomLeft(r, s) + Vec2{0.1, 0.1}; });
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        const Vec2 corner = bottomLeft(r, s) * s->u;
        // The line's top end is still at y = 5 units (it stays vertical, so it
        // follows the corner sideways); its other end is the corner itself.
        bool joined = false;
        if (sk)
            for (const auto& [id, l] : sk->lines())
                for (const auto [top, bottom] : {std::pair{l.start, l.end}, std::pair{l.end, l.start}})
                    if (std::abs(sk->point(top)->position.y - 5 * s->u) < 1e-6)
                        joined = (sk->point(bottom)->position - corner).length() < 1e-6;
        r.check(joined && sk->points().size() + 1 == s->pointsBefore,
                s->where + QStringLiteral(": the dropped end joined the corner (one point now)"), points(r));
        r.check(r.app().undoText() == QStringLiteral("Connect point"), s->where + QStringLiteral(": Connect point"),
                r.app().undoText());
    });
    wait(steps, 3);

    // ---- Double-tap the left side: the whole rectangle and the joined line; Delete removes them.
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        // The left side is the vertical line nearest x = -1 unit.
        Vec2 mid{-s->u, 2.5 * s->u};
        if (sk)
            for (const auto& [id, l] : sk->lines()) {
                const Vec2 a = sk->point(l.start)->position, b = sk->point(l.end)->position;
                if (std::abs(a.x - b.x) < 1e-6 && a.x > -1.5 * s->u && a.x < mid.x + 1.5 * s->u)
                    mid = (a + b) * 0.5;
            }
        const QPointF p = r.screenPoint(mid.x, mid.y, 0);
        tap(r, s, p);
        tap(r, s, p); // at once: a double-tap
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        const auto* sk = activeSketch(r);
        const bool chain = ss && sk && ss->selection().size() == 5
                        && std::all_of(ss->selection().begin(), ss->selection().end(), [&](sketch::EntityId id) { return sk->line(id); });
        r.check(chain, s->where + QStringLiteral(": a double-tap selects the rectangle and the line joined to it"),
                ss ? QString::number(ss->selection().size()) : QString());
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_5_chain"));
        r.check(r.clickItem(QStringLiteral("sketchAction_delete")), s->where + QStringLiteral(": Delete"));
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* sk = activeSketch(r);
        r.check(sk && sk->lines().empty() && sk->circles().size() == 1, s->where + QStringLiteral(": the rectangle and the line are gone, the circle stays"));
    });
    wait(steps, 3);

    // ---- Tap inside the circle -> Extrude, 10 mm.
    steps.push_back([&r, s] { tap(r, s, at(r, s, 1.2, -4.2)); }); // clear of the center point (a finger reaches 20 px)
    wait(steps, 2);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        r.check(ss && ss->selectedRegion().has_value(), s->where + QStringLiteral(": a tap inside the circle selects the shape"));
        r.check(r.clickItem(QStringLiteral("sketchAction_extrude")), s->where + QStringLiteral(": Extrude"));
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(!r.app().sketchMode(), s->where + QStringLiteral(": Extrude finished the sketch"));
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), s->where + QStringLiteral(": extruding the circle"),
                r.app().operationTitle());
        r.type(QStringLiteral("10"));
    });
    steps.push_back([&r] { r.key(Qt::Key_Return); });
    wait(steps, 3);
    steps.push_back([&r, s] {
        const double expected = kPi * 9 * s->u * s->u * 10;
        r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - expected) < 1e-3 * expected,
                s->where + QStringLiteral(": a cylinder of radius 3 units, 10 high: ") + AcceptanceRunner::num(expected),
                AcceptanceRunner::num(r.app().bodyCount() ? r.bodyVolume() : 0));
        r.screenshot(QStringLiteral("sketchdrag_") + s->where + QStringLiteral("_6_extruded"));
    });
    if (touch) {
        steps.push_back([&r] {
            r.window()->setProperty("simulatedSafeArea", QVariant());
            r.app().setTouchMode(false);
            const QSize size = r.initialWindowSize();
            r.resizeWindow(size.width(), size.height());
        });
        wait(steps, 3);
    }
    return steps;
}

Steps steps(AcceptanceRunner& r)
{
    Steps all = part(r, false);
    for (auto& step : part(r, true))
        all.push_back(std::move(step));
    return all;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("sketchdrag"), 60, steps});

} // namespace
} // namespace os::app
