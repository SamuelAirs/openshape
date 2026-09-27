// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Selecting two bodies with a finger (owner, iPhone and iPad, 2026-09-27:
// "It's also really difficult or impossible to select two or more objects by
// double tapping"). Real touch input through Qt's touch path, with a
// finger's timing (taps of 70-90 ms, 110-160 ms apart, the second a few
// pixels off the first), on an iPad-sized window (1180x820) and an iPhone's
// (402x874 with its safe areas): double-tap one box, double-tap the other,
// both are selected and Union is offered and applied (volume); undo, then a
// double-tap on a selected body takes it out again, and a single tap on
// another body adds it (bodies selected: Shapr3D). A mouse Shift+double-
// click adds a body too. The window goes back to the run's size at the end.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtGui/QPointingDevice>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtGui/private/qhighdpiscaling_p.h>
#include <qpa/qwindowsysteminterface.h>

#include <cmath>
#include <memory>
#include <set>

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

QPointingDevice* finger()
{
    static QPointingDevice* device = [] {
        auto* d = new QPointingDevice(QStringLiteral("OpenShape acceptance finger"), 4244, QInputDevice::DeviceType::TouchScreen,
                                      QPointingDevice::PointerType::Finger,
                                      QInputDevice::Capability::Position | QInputDevice::Capability::Area, 10, 0);
        QWindowSystemInterface::registerInputDevice(d);
        return d;
    }();
    return device;
}

// One finger touching (Pressed), lifting (Released) at `p` (window-local
// logical pixels), through Qt's platform touch path with the time of now.
void touch(AcceptanceRunner& r, QPointF p, QEventPoint::State state, int id)
{
    QWindowSystemInterface::TouchPoint tp;
    tp.id = id;
    tp.state = state;
    const QPointF global = QHighDpi::toNativeGlobalPosition(r.window()->mapToGlobal(p), r.window());
    tp.area = QRectF(global - QPointF(3, 3), QSizeF(6, 6)); // native pixels, like the mouse
    tp.pressure = state == QEventPoint::State::Released ? 0 : 1;
    QWindowSystemInterface::handleTouchEvent<QWindowSystemInterface::SynchronousDelivery>(r.window(), finger(), {tp});
}

// A finger's double-tap: down 70 ms, up, 130 ms later down again `offset`
// px away for 90 ms, up. Real time passes between the frames (the gesture
// recognizer reads the events' timestamps), and the app draws meanwhile.
void doubleTap(AcceptanceRunner& r, QPointF p, QPointF offset = {3, -2})
{
    static int id = 100;
    touch(r, p, QEventPoint::State::Pressed, ++id);
    pause(70);
    touch(r, p, QEventPoint::State::Released, id);
    pause(130);
    touch(r, p + offset, QEventPoint::State::Pressed, ++id);
    pause(90);
    touch(r, p + offset, QEventPoint::State::Released, id);
}

void tap(AcceptanceRunner& r, QPointF p)
{
    static int id = 500;
    touch(r, p, QEventPoint::State::Pressed, ++id);
    pause(80);
    touch(r, p, QEventPoint::State::Released, id);
}

struct Scene {
    Uuid a, b;
};

// Exactly these bodies are selected, as bodies.
bool bodiesSelected(AcceptanceRunner& r, const std::set<Uuid>& ids)
{
    const auto& items = r.app().interaction().selection().items();
    std::set<Uuid> selected;
    for (const auto& item : items) {
        if (item.kind != sel::SelectionKind::Body)
            return false;
        selected.insert(item.bodyId);
    }
    return selected == ids && items.size() == ids.size();
}

QString selectionText(AcceptanceRunner& r, const Scene& s)
{
    QStringList parts;
    for (const auto& item : r.app().interaction().selection().items()) {
        const QString body = item.bodyId == s.a ? QStringLiteral("A") : item.bodyId == s.b ? QStringLiteral("B") : QStringLiteral("?");
        parts << (item.kind == sel::SelectionKind::Body   ? QStringLiteral("body ") + body
                  : item.kind == sel::SelectionKind::Face ? QStringLiteral("face ") + body
                                                          : QStringLiteral("other ") + body);
    }
    return parts.isEmpty() ? QStringLiteral("nothing") : parts.join(QStringLiteral(", "));
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

// Two 20 mm boxes overlapping by 5 mm: A from x -18 to 2, B from -3 to 17
// (their union is 14000 mm³).
void makeBoxes(AcceptanceRunner& r, Scene& s)
{
    r.app().newDocument();
    auto& stack = r.app().interaction().undoStack();
    auto& document = r.app().document();
    auto boxA = std::make_unique<doc::BoxFeature>();
    boxA->origin = {-18, -10, 0};
    boxA->size = {20, 20, 20};
    auto boxB = std::make_unique<doc::BoxFeature>();
    boxB->origin = {-3, -10, 0};
    boxB->size = {20, 20, 20};
    auto commandA = std::make_unique<cmd::CreateBodyCommand>("A", std::move(boxA));
    auto commandB = std::make_unique<cmd::CreateBodyCommand>("B", std::move(boxB));
    s.a = commandA->bodyId();
    s.b = commandB->bodyId();
    const bool built = stack.push(std::move(commandA), document).ok() && stack.push(std::move(commandB), document).ok();
    r.app().interaction().documentChanged();
    r.app().interaction().fitAll(false);
    r.check(built && r.app().bodyCount() == 2, "multiselect: two overlapping boxes");
}

// One pass at a window size: double-tap A, double-tap B, Union.
void addPass(Steps& steps, AcceptanceRunner& r, const std::shared_ptr<Scene>& s, int width, int height, const QVariant& safeArea,
             const QString& where)
{
    steps.push_back([&r, width, height, safeArea] {
        r.app().setTouchMode(true);
        r.resizeWindow(width, height);
        r.window()->setProperty("simulatedSafeArea", safeArea);
    });
    wait(steps, 3);
    steps.push_back([&r, s] { makeBoxes(r, *s); });
    wait(steps, 2);
    steps.push_back([&r, where] {
        r.check(r.app().touchMode(), where + QStringLiteral(": the touch layout"));
        doubleTap(r, r.screenPoint(-12, 0, 20)); // A's top, clear of B
    });
    steps.push_back([&r, s, where] {
        r.check(bodiesSelected(r, {s->a}), where + QStringLiteral(": a double-tap selects the first box"), selectionText(r, *s));
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Move"),
                where + QStringLiteral(": its Move arrows"), r.app().operationTitle());
    });
    wait(steps, 3); // a new tap this soon is not part of it anyway, but let the UI settle
    steps.push_back([&r] { doubleTap(r, r.screenPoint(12, 0, 20)); }); // B's top, clear of A
    steps.push_back([&r, s, where] {
        r.check(bodiesSelected(r, {s->a, s->b}), where + QStringLiteral(": a double-tap on the second box adds it"),
                selectionText(r, *s));
        r.check(r.app().bodyCount() == 2 && r.app().interaction().undoStack().size() == 2,
                where + QStringLiteral(": selecting changed nothing in the model"));
        r.check(shown(r, QStringLiteral("barAction_union")) && shown(r, QStringLiteral("barAction_subtract"))
                    && shown(r, QStringLiteral("barAction_intersect")),
                where + QStringLiteral(": Union, Subtract and Intersect are offered"));
        r.screenshot(QStringLiteral("multiselect_") + where + QStringLiteral("_two_bodies"));
        r.check(r.clickItem(QStringLiteral("barAction_union")), where + QStringLiteral(": Union button"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, where] {
        const doc::Body* a = r.app().document().body(s->a);
        const doc::Body* b = r.app().document().body(s->b);
        const double volume = a ? geom::volume(a->shape()) : -1;
        r.check(std::abs(volume - 14000.0) < 1e-3, where + QStringLiteral(": the union is one 14000 mm³ body"),
                AcceptanceRunner::num(volume));
        r.check(b && !b->isVisible(), where + QStringLiteral(": the second box went into it"));
        // Undo: both boxes again, then select both and take one out.
        r.check(r.app().interaction().undo(), where + QStringLiteral(": undo the union"));
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.app().interaction().cancelOperation();
        r.app().interaction().cancelOperation();
        doubleTap(r, r.screenPoint(-12, 0, 20));
    });
    wait(steps, 3);
    steps.push_back([&r, s, where] {
        r.check(bodiesSelected(r, {s->a}), where + QStringLiteral(": A selected again"), selectionText(r, *s));
        tap(r, r.screenPoint(12, 0, 20)); // one tap on B while a body is selected
    });
    wait(steps, 3);
    steps.push_back([&r, s, where] {
        r.check(bodiesSelected(r, {s->a, s->b}), where + QStringLiteral(": a single tap on another body adds it"),
                selectionText(r, *s));
        doubleTap(r, r.screenPoint(-12, 0, 20)); // A again: out
    });
    steps.push_back([&r, s, where] {
        r.check(bodiesSelected(r, {s->b}), where + QStringLiteral(": a double-tap on a selected body takes it out"),
                selectionText(r, *s));
        r.check(!shown(r, QStringLiteral("barAction_union")), where + QStringLiteral(": one body: no Union"));
        r.screenshot(QStringLiteral("multiselect_") + where + QStringLiteral("_one_left"));
    });
    wait(steps, 3);
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<Scene>();
    Steps steps;
    addPass(steps, r, s, 1180, 820, QVariant(), QStringLiteral("ipad"));
    addPass(steps, r, s, 402, 874, QVariantList{62, 0, 34, 0}, QStringLiteral("iphone"));
    // ---- The mouse: double-click, then Shift+double-click the other.
    steps.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    wait(steps, 3);
    steps.push_back([&r, s] {
        makeBoxes(r, *s);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const QPointF p = r.screenPoint(-12, 0, 20);
        r.click(p);
        r.click(p); // within the double-click interval: a double-click
    });
    wait(steps, 4); // past the double-click interval
    steps.push_back([&r, s] {
        r.check(bodiesSelected(r, {s->a}), "mouse: a double-click selects the body", selectionText(r, *s));
        const QPointF p = r.screenPoint(12, 0, 20);
        r.click(p, Qt::ShiftModifier);
        r.click(p, Qt::ShiftModifier);
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(bodiesSelected(r, {s->a, s->b}), "mouse: Shift+double-click adds the second body", selectionText(r, *s));
        r.check(shown(r, QStringLiteral("barAction_union")), "mouse: Union is offered");
        r.click(r.screenPoint(12, 0, 20));
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        const auto& items = r.app().interaction().selection().items();
        r.check(items.size() == 1 && items.front().kind == sel::SelectionKind::Face && items.front().bodyId == s->b,
                "mouse: a plain click still picks a face", selectionText(r, *s));
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("multiselect"), 95, steps});

} // namespace
} // namespace os::app
