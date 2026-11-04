// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measure's design hooks (docs/MEASURE.md), reached as a user reaches them:
// the View buttons' 1:1 (clicked on a desktop, tapped at an iPad's and in a
// phone's View menu: one model millimeter is the screen's millimeter, at the
// scale the Measure workspace set, and a zoom leaves it); the sketch's Trace
// tool with the mouse, one finger and the pen (a rectangle-like stroke gives
// four lines that extrude, a round one a circle; in pen mode a finger moves
// the view); geometry made from measurements through the AppController (an
// outline and a circle sketch, a sized box: their geometry, volumes and one
// undo step each); and the value boxes' ruler (the value chip's during a
// push/pull and a sketch dimension editor's): it asks for Measure, and a
// measured "35mm" handed back lands in the box, previewed, and ✓ applies it.

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QMetaObject>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
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
    std::vector<QPointF> stroke; // screen points of the stroke under way
    int requestsBefore = 0;
    double volume = 0;
    std::size_t undoIndex = 0;
    sketch::EntityId dimension = sketch::kNoEntity;
    Camera camera;
};
using Shared = std::shared_ptr<State>;

const interact::SketchSession* session(AcceptanceRunner& r)
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

// Points every `step` mm along a polyline in the sketch (both ends).
std::vector<Vec2> along(const std::vector<Vec2>& corners, double step)
{
    std::vector<Vec2> out{corners.front()};
    for (std::size_t i = 1; i < corners.size(); ++i) {
        const Vec2 a = corners[i - 1], b = corners[i];
        const int n = std::max(1, int(std::ceil((b - a).length() / step)));
        for (int k = 1; k <= n; ++k)
            out.push_back(a + (b - a) * (double(k) / n));
    }
    return out;
}

std::vector<Vec2> roundStroke(Vec2 center, double radius, int samples)
{
    std::vector<Vec2> out;
    for (int i = 0; i <= samples + 1; ++i) { // a little past the start, as a hand does
        const double a = 2 * kPi * i / samples;
        out.push_back(center + Vec2{std::cos(a), std::sin(a)} * radius);
    }
    return out;
}

std::vector<Vec2> rectangleStroke(double step)
{
    return along({{-20, -12}, {20, -12}, {20, 12}, {-20, 12}, {-20, -12}}, step);
}

double pixelSize(AcceptanceRunner& r)
{
    const Camera& c = r.app().interaction().camera();
    return c.pixelSize(c.target);
}

bool checked(AcceptanceRunner& r, const QString& name)
{
    const QQuickItem* item = r.findItem(name);
    return item && item->property("checked").toBool();
}

// The ruler opened the Measure workspace for a value (its Ruler); it is
// closed again, as Close does, so the value can be handed back directly
// (the measure scenario takes the whole way, Use included).
void leaveMeasure(AcceptanceRunner& r, const QString& what)
{
    QQuickItem* screen = r.findItem(QStringLiteral("measureScreen"));
    r.check(screen && screen->isVisible() && screen->property("forKind").toString() == r.window()->property("measureKind").toString(),
            what + QStringLiteral(": the Measure workspace opens for it"));
    if (screen)
        QMetaObject::invokeMethod(screen, "close");
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    const QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

int windowInt(AcceptanceRunner& r, const char* name)
{
    return r.window()->property(name).toInt();
}

QVariant callWindow(AcceptanceRunner& r, const char* function, const QVariant& argument)
{
    QVariant result;
    QMetaObject::invokeMethod(r.window(), function, Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, argument));
    return result;
}

// A stroke drawn by one device in the open sketch: the mouse in one step
// (Qt delivers its moves at once), a finger or the pen one move per step
// (Qt Quick hands those on once per frame).
enum class Device { Mouse, Finger, Pen };
void addStroke(Steps& steps, AcceptanceRunner& r, const Shared& s, Device device, std::function<std::vector<Vec2>()> local,
               int moveSteps)
{
    steps.push_back([&r, s, device, local] {
        s->stroke.clear();
        for (const Vec2& p : local())
            s->stroke.push_back(at(r, p));
        if (s->stroke.empty())
            return;
        switch (device) {
        case Device::Mouse:
            r.mouseMove(s->stroke.front());
            r.mousePress(s->stroke.front());
            for (std::size_t i = 1; i < s->stroke.size(); ++i)
                r.mouseMove(s->stroke[i], Qt::LeftButton);
            r.mouseRelease(s->stroke.back());
            break;
        case Device::Finger: r.touchPress(s->stroke.front()); break;
        case Device::Pen:
            r.penHover(s->stroke.front());
            r.penPress(s->stroke.front());
            break;
        }
    });
    if (device == Device::Mouse)
        return;
    for (int k = 1; k <= moveSteps; ++k)
        steps.push_back([&r, s, device, k, moveSteps] {
            if (s->stroke.empty())
                return;
            const std::size_t i = std::min(s->stroke.size() - 1, std::size_t(double(k) / moveSteps * double(s->stroke.size() - 1) + 0.5));
            if (device == Device::Finger)
                r.touchMove(s->stroke[i]);
            else
                r.penMove(s->stroke[i]);
        });
    steps.push_back([&r, s, device] {
        if (s->stroke.empty())
            return;
        if (device == Device::Finger)
            r.touchRelease(s->stroke.back());
        else
            r.penRelease(s->stroke.back());
    });
}

// ---- 1:1 ----------------------------------------------------------------------------------

void actualSize(Steps& steps, AcceptanceRunner& r)
{
    // Desktop, the mouse.
    steps.push_back([&r] {
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(r.app().bodyCount() == 1, "1:1: a box to look at");
        r.app().setScreenScale(5.0, false); // as the Measure workspace sets it
        r.check(!r.app().actualSize() && !checked(r, QStringLiteral("viewActualSize")), "1:1: off at first");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(r.app().perspective(), "1:1: the view starts in perspective");
        r.check(r.clickItem(QStringLiteral("viewActualSize")), "1:1: the View buttons' 1:1 (click)");
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const Camera& c = r.app().interaction().camera();
        r.check(r.app().actualSize() && checked(r, QStringLiteral("viewActualSize")), "1:1: on, the button checked");
        r.check(c.projection == Camera::Projection::Orthographic, "1:1: orthographic");
        r.check(std::abs(pixelSize(r) - 0.2) < 1e-9, "1:1: one pixel is 1/5 mm at 5 px/mm (the scale set)",
                AcceptanceRunner::num(pixelSize(r)));
        const QQuickItem* status = r.findItem(QStringLiteral("actualSizeStatus"));
        r.check(status && status->isVisible() && status->property("text").toString().contains(QStringLiteral("estimated")),
                "1:1: the status line says the scale is estimated",
                status ? status->property("text").toString() : QString());
        r.screenshot(QStringLiteral("measuredesign_1to1_desktop"));
        r.app().setScreenScale(4.0, true); // calibrated with a card
    });
    steps.push_back([&r] {
        const QQuickItem* status = r.findItem(QStringLiteral("actualSizeStatus"));
        r.check(status && status->property("text").toString() == QStringLiteral("Actual size"),
                "1:1: calibrated, it says only Actual size", status ? status->property("text").toString() : QString());
        r.check(std::abs(pixelSize(r) - 0.25) < 1e-9, "1:1: a new scale keeps it at 1:1", AcceptanceRunner::num(pixelSize(r)));
        r.wheel({r.window()->width() * 0.5, r.window()->height() * 0.5}, 1);
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!r.app().actualSize() && !checked(r, QStringLiteral("viewActualSize")), "1:1: the wheel leaves it (unchecked)");
        r.check(r.app().perspective(), "1:1: perspective is back");
        r.check(!shown(r, QStringLiteral("actualSizeStatus")), "1:1: the status line goes");
        r.check(r.clickItem(QStringLiteral("viewActualSize")), "1:1: on again");
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(r.app().actualSize(), "1:1: on again");
        r.check(r.clickItem(QStringLiteral("viewFit")), "1:1: Fit");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(!r.app().actualSize() && r.app().perspective(), "1:1: Fit (a zoom) leaves it");
        r.check(r.clickItem(QStringLiteral("viewActualSize")), "1:1: on once more");
    });
    wait(steps, 1);
    steps.push_back([&r] { r.check(r.clickItem(QStringLiteral("viewActualSize")), "1:1: off with the button"); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!r.app().actualSize() && r.app().perspective(), "1:1: off: perspective as before");
        // ---- An iPad (touch, the regular layout).
        r.app().setTouchMode(true);
        r.resizeWindow(1180, 820);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.app().setScreenScale(132.0 / 25.4, false);
        r.check(r.tapItem(QStringLiteral("viewActualSize")), "1:1 iPad: tap 1:1");
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(r.app().actualSize(), "1:1 iPad: on");
        r.check(std::abs(pixelSize(r) - 25.4 / 132.0) < 1e-9, "1:1 iPad: one point is 25.4/132 mm", AcceptanceRunner::num(pixelSize(r)));
        r.screenshot(QStringLiteral("measuredesign_1to1_ipad"));
        r.check(r.tapItem(QStringLiteral("viewActualSize")), "1:1 iPad: tap again: off");
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(!r.app().actualSize(), "1:1 iPad: off");
        // ---- A phone: in the View menu.
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.app().setScreenScale(460.0 / 3.0 / 25.4, true);
        r.check(r.tapItem(QStringLiteral("viewMenuButton")), "1:1 phone: the View button");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const QQuickItem* button = r.findItem(QStringLiteral("viewActualSize"));
        r.check(button && button->isVisible() && button->property("text").toString() == QStringLiteral("Actual size 1:1"),
                "1:1 phone: the menu offers Actual size 1:1", button ? button->property("text").toString() : QString());
        r.screenshot(QStringLiteral("measuredesign_1to1_phone_menu"));
        r.check(r.tapItem(QStringLiteral("viewActualSize")), "1:1 phone: tap Actual size 1:1");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().actualSize(), "1:1 phone: on");
        r.check(!shown(r, QStringLiteral("viewPanel")), "1:1 phone: the menu closed");
        r.check(std::abs(pixelSize(r) - 3 * 25.4 / 460.0) < 1e-9, "1:1 phone: one point is 3 x 25.4/460 mm",
                AcceptanceRunner::num(pixelSize(r)));
        r.screenshot(QStringLiteral("measuredesign_1to1_phone"));
        r.check(r.tapItem(QStringLiteral("viewMenuButton")), "1:1 phone: the View button again");
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("viewFit")), "1:1 phone: Fit"); });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(!r.app().actualSize(), "1:1 phone: Fit leaves it");
        // The value chip's ruler on a phone: first in its row of actions.
        r.touchTap({r.screenPoint(0, 0, 20)});
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "ruler phone: the top face offers push/pull",
                r.app().operationTitle());
        const int before = windowInt(r, "measureRequests");
        r.check(shown(r, QStringLiteral("valueChipMeasure")), "ruler phone: the chip shows the ruler");
        r.check(r.tapItem(QStringLiteral("valueChipMeasure")), "ruler phone: tap the ruler");
        r.check(windowInt(r, "measureRequests") == before + 1, "ruler phone: it asks for Measure");
        r.screenshot(QStringLiteral("measuredesign_ruler_phone"));
        leaveMeasure(r, QStringLiteral("ruler phone"));
        r.app().cancelOperation();
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setTouchMode(false);
    });
    wait(steps, 3);
}

// ---- Trace --------------------------------------------------------------------------------

void startSketch(Steps& steps, AcceptanceRunner& r, const char* what)
{
    steps.push_back([&r, what] {
        r.app().newDocument();
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
        r.check(r.app().sketchMode(), QString::fromUtf8(what) + QStringLiteral(": K starts a sketch"));
    });
    wait(steps, 4);
}

void trace(Steps& steps, AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    // ---- The mouse: a rectangle-like stroke and a round one.
    startSketch(steps, r, "trace (mouse)");
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("tool_trace")), "trace: the Trace tool button");
        r.check(r.app().sketchTool() == QStringLiteral("trace"), "trace: Trace is on", r.app().sketchTool());
    });
    addStroke(steps, r, s, Device::Mouse, [] { return rectangleStroke(0.8); }, 0);
    steps.push_back([&r] {
        const auto* ss = session(r);
        r.check(ss && ss->sketch().lines().size() == 4, "trace (mouse): the stroke became four lines",
                ss ? QString::number(ss->sketch().lines().size()) : QString());
        r.check(ss && ss->regions().size() == 1, "trace (mouse): a closed profile");
        r.check(r.app().undoText().contains(QStringLiteral("Trace")), "trace: one step, Trace", r.app().undoText());
    });
    addStroke(steps, r, s, Device::Mouse, [] { return roundStroke({33, 0}, 8, 120); }, 0);
    steps.push_back([&r] {
        const auto* ss = session(r);
        r.check(ss && ss->sketch().circles().size() == 1, "trace (mouse): the round stroke became a circle");
        if (ss && !ss->sketch().circles().empty())
            r.check(std::abs(ss->sketch().circles().begin()->second.radius - 8) < 0.5, "trace (mouse): of radius 8 mm",
                    AcceptanceRunner::num(ss->sketch().circles().begin()->second.radius));
        r.screenshot(QStringLiteral("measuredesign_trace_mouse"));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), "trace: Finish sketch");
    });
    wait(steps, 5);
    steps.push_back([&r] {
        r.click(r.screenPoint(5, 5, 0));
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "trace: the traced rectangle extrudes", r.app().operationTitle());
        r.type(QStringLiteral("10"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().bodyCount() == 1, "trace: a body");
        const double v = r.bodyVolume();
        r.check(std::abs(v - 40 * 24 * 10) < 0.03 * 40 * 24 * 10, "trace: about 40 x 24 x 10 mm (within the trace's tolerance)",
                AcceptanceRunner::num(v));
        r.screenshot(QStringLiteral("measuredesign_trace_extruded"));
    });

    // ---- One finger (the touch layout).
    steps.push_back([&r] { r.app().setTouchMode(true); });
    startSketch(steps, r, "trace (finger)");
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("tool_trace")), "trace (finger): tap Trace"); });
    wait(steps, 1);
    addStroke(steps, r, s, Device::Finger, [] { return rectangleStroke(1.0); }, 32);
    steps.push_back([&r] {
        const auto* ss = session(r);
        r.check(ss && ss->sketch().lines().size() == 4 && ss->regions().size() == 1,
                "trace (finger): one finger traced four lines, closed", ss ? QString::number(ss->sketch().lines().size()) : QString());
        r.check(!r.app().sketchLoupe().value(QStringLiteral("visible")).toBool(), "trace (finger): no loupe after it");
        r.screenshot(QStringLiteral("measuredesign_trace_finger"));
    });

    // ---- The pen: a circle; then in pen mode a finger moves the view.
    startSketch(steps, r, "trace (pen)");
    steps.push_back([&r] { r.check(r.tapItem(QStringLiteral("tool_trace")), "trace (pen): tap Trace"); });
    wait(steps, 1);
    addStroke(steps, r, s, Device::Pen, [] { return roundStroke({0, 0}, 12, 72); }, 36);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        r.check(r.app().penMode(), "trace (pen): pen mode is on");
        r.check(ss && ss->sketch().circles().size() == 1, "trace (pen): the pen traced a circle");
        if (ss && !ss->sketch().circles().empty())
            r.check(std::abs(ss->sketch().circles().begin()->second.radius - 12) < 0.5, "trace (pen): of radius 12 mm",
                    AcceptanceRunner::num(ss->sketch().circles().begin()->second.radius));
        r.screenshot(QStringLiteral("measuredesign_trace_pen"));
        s->camera = r.app().interaction().camera();
        s->undoIndex = r.app().undoStack().index();
    });
    wait(steps, 4); // past the palm-rejection grace after the pen lifted (PalmRejection::kGrace)
    addStroke(steps, r, s, Device::Finger, [] { return along({{-25, -18}, {5, -18}}, 1.0); }, 8);
    wait(steps, 1);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        const Camera& c = r.app().interaction().camera();
        const bool moved = (c.backward() - s->camera.backward()).length() > 1e-6 || (c.target - s->camera.target).length() > 1e-6;
        r.check(moved, "trace (pen): in pen mode a finger moves the view");
        r.check(ss && ss->sketch().circles().size() == 1 && ss->sketch().lines().empty() && r.app().undoStack().index() == s->undoIndex,
                "trace (pen): and traces nothing");
        r.app().setPenMode(false);
        r.app().setTouchMode(false);
        r.app().finishSketch();
    });
    wait(steps, 4);
}

// ---- Geometry from measurements -----------------------------------------------------------

void measuredGeometry(Steps& steps, AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    steps.push_back([&r, s] {
        r.app().newDocument();
        s->undoIndex = r.app().undoStack().index();
        const QVariantList points{QPointF(-25, -15), QVariantMap{{QStringLiteral("x"), 25}, {QStringLiteral("y"), -15}},
                                  QPointF(25, 15), QPointF(-25, 15)};
        r.check(r.app().createOutlineSketch(points, true), "outline: createOutlineSketch");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        r.check(r.app().sketchMode() && ss, "outline: the new sketch is open");
        r.check(ss && ss->sketch().lines().size() == 4 && ss->regions().size() == 1, "outline: four lines, closed");
        r.check(r.app().sketchTool() == QStringLiteral("select"), "outline: the Select tool", r.app().sketchTool());
        r.check(r.app().undoStack().index() == s->undoIndex + 1, "outline: one undo step");
        r.check(r.app().interaction().camera().backward().z > 0.999, "outline: the view faces it");
        const QPointF a = at(r, {-25, -15}), b = at(r, {25, 15});
        r.check(std::abs(b.x() - a.x()) > 0.2 * r.window()->width(), "outline: framed on it",
                AcceptanceRunner::num(std::abs(b.x() - a.x())));
        r.screenshot(QStringLiteral("measuredesign_outline"));
        r.click(at(r, {6, 4})); // a tap inside: Extrude offered
    });
    wait(steps, 1);
    steps.push_back([&r] {
        const auto* ss = session(r);
        r.check(ss && ss->selectedRegion().has_value(), "outline: a click inside selects the shape");
        r.check(r.clickItem(QStringLiteral("sketchAction_extrude")), "outline: Extrude");
    });
    wait(steps, 5);
    steps.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "outline: extruding", r.app().operationTitle());
        r.type(QStringLiteral("10"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - 50 * 30 * 10) < 1e-3,
                "outline: extruded 10 mm: 15000 mm3", AcceptanceRunner::num(r.bodyVolume()));
        r.app().undo(); // the extrusion
        r.app().undo(); // the sketch
        r.check(r.app().bodyCount() == 0 && r.app().document().sketches().empty() && r.app().undoStack().index() == s->undoIndex,
                "outline: two undos remove the extrusion and the sketch");
    });
    wait(steps, 2);
    // A circle sketch (a coin measured on the screen).
    steps.push_back([&r, s] {
        r.app().newDocument();
        s->undoIndex = r.app().undoStack().index();
        r.check(r.app().createCircleSketch(0, 0, 24.26), "circle: createCircleSketch");
        r.check(!r.app().createCircleSketch(0, 0, 0), "circle: a zero diameter is refused");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        r.check(ss && ss->sketch().circles().size() == 1, "circle: one circle");
        bool dimensioned = false;
        if (ss)
            for (const auto& [id, c] : ss->sketch().constraints())
                dimensioned |= c.kind == sketch::ConstraintKind::Diameter && std::abs(c.value - 24.26) < 1e-9;
        r.check(dimensioned, "circle: its diameter dimension is 24.26 mm");
        r.check(r.app().undoStack().index() == s->undoIndex + 1, "circle: one undo step (the refused one added none)");
        const double across = std::abs(at(r, {12.13, 0}).x() - at(r, {-12.13, 0}).x());
        r.check(across > 0.2 * std::min(r.window()->width(), r.window()->height()) && across < r.window()->height(),
                "circle: framed on the whole circle", AcceptanceRunner::num(across));
        r.screenshot(QStringLiteral("measuredesign_circle"));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), "circle: Finish sketch");
    });
    wait(steps, 5);
    steps.push_back([&r] {
        r.click(r.screenPoint(4, 3, 0));
        r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "circle: the circle extrudes", r.app().operationTitle());
        r.type(QStringLiteral("5"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 2);
    steps.push_back([&r] {
        const double expected = kPi * 12.13 * 12.13 * 5;
        r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - expected) < 1e-3 * expected, "circle: a 24.26 x 5 mm disc",
                AcceptanceRunner::num(r.bodyVolume()));
    });
    // A sized box (measured in AR).
    steps.push_back([&r, s] {
        r.app().newDocument();
        s->undoIndex = r.app().undoStack().index();
        r.check(r.app().createSizedBox(60, 35, 12.5), "box: createSizedBox");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - 60 * 35 * 12.5) < 1e-6, "box: 60 x 35 x 12.5 mm",
                AcceptanceRunner::num(r.bodyVolume()));
        const auto bb = geom::boundingBox(r.body(0).shape());
        r.check(std::abs(bb.max.x - bb.min.x - 60) < 1e-6 && std::abs(bb.max.y - bb.min.y - 35) < 1e-6
                    && std::abs(bb.max.z - bb.min.z - 12.5) < 1e-6,
                "box: width along X, depth along Y, height along Z");
        r.check(r.app().interaction().selection().singleBody().has_value(), "box: selected");
        r.screenshot(QStringLiteral("measuredesign_box"));
        r.app().undo();
        r.check(r.app().bodyCount() == 0 && r.app().undoStack().index() == s->undoIndex, "box: one undo removes it");
    });
}

// ---- The value boxes' ruler ---------------------------------------------------------------

void ruler(Steps& steps, AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    steps.push_back([&r] {
        r.app().newDocument();
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.app().fitAll(); });
    wait(steps, 4);
    steps.push_back([&r] { r.click(r.screenPoint(0, 0, 20)); });
    wait(steps, 2);
    steps.push_back([&r, s] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "ruler: the top face offers push/pull", r.app().operationTitle());
        r.check(shown(r, QStringLiteral("valueChipMeasure")), "ruler: the value chip shows the ruler");
        s->volume = r.bodyVolume();
        s->requestsBefore = windowInt(r, "measureRequests");
        r.screenshot(QStringLiteral("measuredesign_ruler_chip"));
        r.check(r.clickItem(QStringLiteral("valueChipMeasure")), "ruler: click the ruler");
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(windowInt(r, "measureRequests") == s->requestsBefore + 1, "ruler: it asks for Measure");
        r.check(r.window()->property("measureKind").toString() == QStringLiteral("length"), "ruler: for a length",
                r.window()->property("measureKind").toString());
        r.check(r.app().operationActive(), "ruler: the push/pull stays");
        leaveMeasure(r, QStringLiteral("ruler"));
        const QVariant took = callWindow(r, "useMeasuredValue", QStringLiteral("35mm"));
        r.check(took.toBool(), "ruler: the measured 35mm comes back to the value box");
    });
    wait(steps, 2);
    steps.push_back([&r, s] {
        const QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        const QString text = field ? field->property("text").toString() : QString();
        r.check(text.contains(QStringLiteral("35")), "ruler: the box shows 35", text);
        r.check(std::abs(r.bodyVolume() - s->volume) < 1e-6, "ruler: not applied yet (the user confirms)");
        r.screenshot(QStringLiteral("measuredesign_ruler_value"));
        r.check(r.clickItem(QStringLiteral("valueChipApply")), "ruler: ✓");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(std::abs(r.bodyVolume() - 20 * 20 * 35) < 1e-3, "ruler: the cube is 35 mm tall: 20 x 20 x 35", AcceptanceRunner::num(r.bodyVolume()));
    });

    // A sketch dimension editor's ruler.
    startSketch(steps, r, "ruler (dimension)");
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("tool_rectangle")), "ruler (dimension): Rectangle");
        r.click(at(r, {0, 0}));
        r.mouseMove(at(r, {14, 7}));
        r.type(QStringLiteral("40"));
        r.key(Qt::Key_Tab);
        r.type(QStringLiteral("20"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        s->dimension = sketch::kNoEntity;
        if (const auto* ss = session(r))
            for (const auto& [id, c] : ss->sketch().constraints())
                if (c.isDimension() && std::abs(std::abs(c.value) - 40) < 1e-9)
                    s->dimension = id;
        r.check(s->dimension != sketch::kNoEntity, "ruler (dimension): the 40 mm width dimension");
        r.check(r.clickItem(QStringLiteral("dimensionLabel_%1_tap").arg(s->dimension)), "ruler (dimension): click it");
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(shown(r, QStringLiteral("dimensionEditor")), "ruler (dimension): its editor opens");
        r.check(shown(r, QStringLiteral("dimensionMeasure")), "ruler (dimension): with a ruler beside it");
        s->requestsBefore = windowInt(r, "measureRequests");
        r.screenshot(QStringLiteral("measuredesign_ruler_dimension"));
        r.check(r.clickItem(QStringLiteral("dimensionMeasure")), "ruler (dimension): click the ruler");
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        r.check(windowInt(r, "measureRequests") == s->requestsBefore + 1, "ruler (dimension): it asks for Measure");
        leaveMeasure(r, QStringLiteral("ruler (dimension)"));
        r.check(callWindow(r, "useMeasuredValue", QStringLiteral("30mm")).toBool(), "ruler (dimension): 30mm comes back");
        QQuickItem* editor = r.findItem(QStringLiteral("dimensionEditor"));
        r.check(editor && editor->isVisible() && editor->property("text").toString() == QStringLiteral("30mm"),
                "ruler (dimension): in the editor", editor ? editor->property("text").toString() : QString());
        if (editor)
            editor->forceActiveFocus();
        r.key(Qt::Key_Return);
    });
    wait(steps, 1);
    steps.push_back([&r, s] {
        const auto* ss = session(r);
        const auto* c = ss ? ss->sketch().constraint(s->dimension) : nullptr;
        r.check(c && std::abs(std::abs(c->value) - 30) < 1e-9, "ruler (dimension): Enter applies 30 mm",
                c ? AcceptanceRunner::num(c->value) : QString());
        r.app().finishSketch();
    });
    wait(steps, 4);
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    actualSize(out, r);
    trace(out, r);
    measuredGeometry(out, r);
    ruler(out, r);
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("measuredesign"), 64, steps});

} // namespace
} // namespace os::app
