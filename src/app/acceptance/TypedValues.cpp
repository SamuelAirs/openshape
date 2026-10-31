// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Typed values checked against OpenShape3D's keypad and dismissal rules, by
// touch on an iPhone (402x874) and an iPad (1180x820):
// - the keypad's times key works on the value shown (20 mm, x 2: 40 mm);
// - a sketch dimension opened and confirmed as it is keeps its exact value
//   (12 mm shown as 0.472 in stays 12 mm; no history step);
// - an angle unit typed for a length is refused, the message beside the
//   field and never under the keypad, and corrected at once;
// - a tap in the view with a value typed applies it and goes no further:
//   no rectangle is started under the finger; without a value typed it
//   only closes the field; a drag that starts on a side while the field is
//   open moves nothing;
// - the field stays on its dimension when the view zooms;
// - a refused live value's message shows above the keypad.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QVariantList>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <map>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
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

bool overlaps(const QRectF& a, const QRectF& b)
{
    return a.left() < b.right() && b.left() < a.right() && a.top() < b.bottom() && b.top() < a.bottom();
}

bool tapItem(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return false;
    r.touchTap({item->mapToScene(QPointF(item->width() / 2, item->height() / 2))});
    return true;
}

// Keypad keys by objectName, one per character.
bool tapKeys(AcceptanceRunner& r, const QString& keys)
{
    bool all = true;
    for (const QChar c : keys)
        all = tapItem(r, QStringLiteral("keypadKey_") + c) && all;
    return all;
}

bool keypadShown(AcceptanceRunner& r)
{
    QQuickItem* keypad = r.findItem(QStringLiteral("numericKeypad"));
    return keypad && keypad->isVisible();
}

bool editorShown(AcceptanceRunner& r)
{
    QQuickItem* editor = r.findItem(QStringLiteral("dimensionEditor"));
    return editor && editor->isVisible();
}

QString editorText(AcceptanceRunner& r)
{
    QQuickItem* editor = r.findItem(QStringLiteral("dimensionEditor"));
    return editor ? editor->property("text").toString() : QString();
}

const sketch::Sketch* activeSketch(AcceptanceRunner& r)
{
    const auto* session = r.app().interaction().sketchSession();
    return session ? &session->sketch() : nullptr;
}

std::map<int, Vec2> pointPositions(AcceptanceRunner& r)
{
    std::map<int, Vec2> out;
    if (const auto* sk = activeSketch(r))
        for (const auto& [id, p] : sk->points())
            out[int(id)] = p.position;
    return out;
}

double dimensionValue(AcceptanceRunner& r, int id)
{
    const auto* sk = activeSketch(r);
    const auto* c = sk ? sk->constraint(sketch::EntityId(id)) : nullptr;
    return c ? std::abs(c->value) : -1.0;
}

// The sketch's points' extent along x.
double sketchWidth(AcceptanceRunner& r)
{
    double left = 1e9, right = -1e9;
    for (const auto& [id, p] : pointPositions(r)) {
        left = std::min(left, p.x);
        right = std::max(right, p.x);
    }
    return right - left;
}

std::size_t historySteps(AcceptanceRunner& r) { return r.app().interaction().undoStack().index(); }

// The dimension's label, where the sketch shows it now (window coordinates).
std::optional<QPointF> labelCenter(AcceptanceRunner& r, int id)
{
    const auto* session = r.app().interaction().sketchSession();
    if (!session)
        return std::nullopt;
    for (const auto& label : session->labels(r.app().interaction().camera()))
        if (label.kind == interact::SketchLabel::Kind::Dimension && int(label.constraint) == id)
            return QPointF(label.screen.x, label.screen.y);
    return std::nullopt;
}

// A spot in the view above the sketch, below the top row of controls.
QPointF emptySpot(AcceptanceRunner& r) { return QPointF(r.window()->width() / 2.0, r.window()->height() * 0.3); }

struct Config {
    QString name;
    int width = 0;
    int height = 0;
    QVariant safeArea;
    bool phone() const { return width < 600; }
};

struct State {
    double volume = 0;
    QString seed;
    int widthDimension = 0;
    std::size_t steps = 0;
    std::size_t lines = 0;
    std::map<int, Vec2> points;
    QPointF sidePoint; // on the rectangle's left side (window)
};

void addConfig(Steps& steps, AcceptanceRunner& r, const Config& c, const std::shared_ptr<State>& s)
{
    const auto name = [c](const char* what) { return c.name + QStringLiteral(": ") + QString::fromUtf8(what); };
    steps.push_back([&r, c] {
        r.app().newDocument();
        r.app().setTouchMode(true);
        r.app().setDisplayUnit(QStringLiteral("mm"));
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
    });
    wait(steps, 3);
    // ---- The value box: x 2 on the value shown.
    steps.push_back([&r, name] { r.check(tapItem(r, QStringLiteral("emptyAddBox")), name("tap Add a box")); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.app().setTouchMode(true);
        r.app().fitAll();
    });
    wait(steps, 4);
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 20)}); });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), name("the top face offers push/pull"), r.app().operationTitle());
        s->volume = r.bodyVolume();
        s->seed = r.app().operationValueText();
        r.check(tapItem(r, QStringLiteral("valueChipField")), name("tap the value"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(keypadShown(r), name("the keypad is up"));
        r.check(tapItem(r, QStringLiteral("keypadKey_*")) && tapKeys(r, QStringLiteral("2")), name("tap times, 2"));
        QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        const QString text = field ? field->property("text").toString() : QString();
        r.check(text == s->seed + QStringLiteral("*2"), name("times works on the value shown"), text + QStringLiteral(" (was ") + s->seed + QStringLiteral(")"));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), name("tap the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        const double h = r.app().bodyCount() == 1 ? geom::boundingBox(r.body(0).shape()).size().z : -1;
        r.check(std::abs(h - 40) < 1e-6 && std::abs(r.bodyVolume() - 2 * s->volume) < 1e-3, name("20 mm x 2 makes the box 40 mm high"),
                AcceptanceRunner::num(h));
        r.check(!keypadShown(r), name("the keypad goes"));
    });
    // ---- A sketch: a 12 x 8 rectangle from the keypad.
    steps.push_back([&r] {
        r.app().newDocument();
        r.app().setTouchMode(true);
    });
    wait(steps, 2);
    steps.push_back([&r, name] { r.check(tapItem(r, QStringLiteral("emptyStartSketch")), name("tap Start a sketch")); });
    wait(steps, 5);
    steps.push_back([&r, name] { r.check(tapItem(r, QStringLiteral("tool_rectangle")), name("the Rectangle tool")); });
    steps.push_back([&r] { r.touchTap({r.screenPoint(3, 4, 0)}); });
    wait(steps, 2);
    steps.push_back([&r, name] { r.check(tapItem(r, QStringLiteral("sketchInput_width")), name("tap the live width")); });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(tapKeys(r, QStringLiteral("12")) && tapItem(r, QStringLiteral("keypadKey_next")) && tapKeys(r, QStringLiteral("8"))
                    && tapItem(r, QStringLiteral("keypadKey_done")),
                name("tap 1, 2, Next, 8 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        s->widthDimension = 0;
        if (const auto* sk = activeSketch(r))
            for (const auto& [id, constraint] : sk->constraints())
                if (constraint.kind == sketch::ConstraintKind::HorizontalDistance)
                    s->widthDimension = int(id);
        r.check(s->widthDimension != 0 && std::abs(sketchWidth(r) - 12) < 1e-9, name("a rectangle 12 wide"), AcceptanceRunner::num(sketchWidth(r)));
        s->lines = activeSketch(r) ? activeSketch(r)->lines().size() : 0;
        // Inches shown: 12 mm reads 0.472 in.
        r.app().setDisplayUnit(QStringLiteral("in"));
    });
    wait(steps, 2);
    // ---- Confirmed as it is: exactly 12 mm, no step.
    steps.push_back([&r, s, name] {
        s->steps = historySteps(r);
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("tap the width dimension"));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(editorShown(r) && keypadShown(r), name("its field and the keypad"));
        r.check(editorText(r) == QStringLiteral("0.472"), name("the field shows the label's text"), editorText(r));
        r.screenshot(QStringLiteral("typedvalues_") + name("seed").replace(QStringLiteral(": "), QStringLiteral("_")));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), name("tap the check mark without typing"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(!editorShown(r) && !keypadShown(r), name("the field and the keypad go"));
        r.check(std::abs(dimensionValue(r, s->widthDimension) - 12) < 1e-12 && std::abs(sketchWidth(r) - 12) < 1e-9,
                name("the width stays exactly 12 mm (not 0.472 in = 11.9888 mm)"), AcceptanceRunner::num(dimensionValue(r, s->widthDimension)));
        r.check(historySteps(r) == s->steps, name("and no history step"), QStringLiteral("%1 -> %2").arg(s->steps).arg(historySteps(r)));
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("tap the width dimension again"));
    });
    wait(steps, 2);
    // ---- An angle for a length: refused, the message in sight, corrected.
    steps.push_back([&r, name] {
        r.type(QStringLiteral("5deg")); // a keyboard (the keypad has no degrees for a length)
        r.check(editorText(r) == QStringLiteral("5deg"), name("typed 5deg"), editorText(r));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), name("tap the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        QQuickItem* error = r.findItem(QStringLiteral("dimensionError"));
        const QString message = error ? error->property("text").toString() : QString();
        r.check(editorShown(r) && message == QStringLiteral("This is a length: type it in mm, cm, m, in or ft, not in degrees."),
                name("an angle unit is refused for a length, the field stays"), message);
        const QRectF errorRect = sceneRect(r, QStringLiteral("dimensionError"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        const QRectF editor = sceneRect(r, QStringLiteral("dimensionEditor"));
        r.check(!errorRect.isEmpty() && !overlaps(errorRect, pad), name("the keypad does not cover the message"),
                rectText(errorRect) + QStringLiteral(" / ") + rectText(pad));
        r.check(!overlaps(editor, pad), name("nor the field"), rectText(editor) + QStringLiteral(" / ") + rectText(pad));
        r.check(std::abs(sketchWidth(r) - 12) < 1e-9 && historySteps(r) == s->steps, name("the rectangle as it was"),
                AcceptanceRunner::num(sketchWidth(r)));
        r.screenshot(QStringLiteral("typedvalues_") + name("refused").replace(QStringLiteral(": "), QStringLiteral("_")));
        r.check(tapItem(r, QStringLiteral("keypadKey_clear")) && tapKeys(r, QStringLiteral("30")) && tapItem(r, QStringLiteral("keypadKey_mm")),
                name("tap C, 3, 0, mm"));
        r.check(editorText(r) == QStringLiteral("30mm"), name("the field shows 30mm"), editorText(r));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), name("tap the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(!editorShown(r) && std::abs(sketchWidth(r) - 30) < 1e-9, name("30 mm, typed in inches shown"), AcceptanceRunner::num(sketchWidth(r)));
        // ---- A tap in the view with a value typed: applied, and no rectangle started there.
        r.app().setSketchTool(QStringLiteral("rectangle"));
        s->steps = historySteps(r);
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("the Rectangle tool; tap the width dimension"));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(editorShown(r) && keypadShown(r), name("its field and the keypad again"));
        r.check(tapKeys(r, QStringLiteral("20")) && tapItem(r, QStringLiteral("keypadKey_mm")), name("tap 2, 0, mm"));
    });
    // Empty space above the rectangle, off the keypad and the controls.
    steps.push_back([&r] { r.touchTap({emptySpot(r)}); });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(!editorShown(r) && !keypadShown(r), name("the tap closes the field and the keypad"));
        r.check(std::abs(sketchWidth(r) - 20) < 1e-9 && historySteps(r) == s->steps + 1, name("the value typed is applied (20 mm)"),
                AcceptanceRunner::num(sketchWidth(r)));
        r.check(!r.app().sketchDrawing() && activeSketch(r) && activeSketch(r)->lines().size() == s->lines,
                name("the tap draws nothing (no rectangle started under the finger)"),
                QStringLiteral("drawing %1, %2 lines").arg(r.app().sketchDrawing()).arg(activeSketch(r) ? activeSketch(r)->lines().size() : 0));
        s->steps = historySteps(r);
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("tap the width dimension once more"));
    });
    wait(steps, 2);
    steps.push_back([&r] { r.touchTap({emptySpot(r)}); });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(!editorShown(r) && !keypadShown(r), name("without a value typed, a tap only closes the field"));
        r.check(std::abs(sketchWidth(r) - 20) < 1e-9 && historySteps(r) == s->steps && !r.app().sketchDrawing(),
                name("nothing changes, nothing is drawn"), AcceptanceRunner::num(sketchWidth(r)));
        // ---- A drag from a side while the field is open moves nothing.
        r.app().setSketchTool(QStringLiteral("select"));
        s->points = pointPositions(r);
        double left = 1e9, bottom = 1e9, top = -1e9;
        for (const auto& [id, p] : s->points) {
            left = std::min(left, p.x);
            bottom = std::min(bottom, p.y);
            top = std::max(top, p.y);
        }
        s->sidePoint = r.screenPoint(left, (bottom + top) / 2, 0);
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("the Select tool; tap the width dimension"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        r.check(editorShown(r), name("the field is open"));
        r.touchPress(s->sidePoint);
    });
    for (int i = 1; i <= 6; ++i)
        steps.push_back([&r, s, i] { r.touchMove(s->sidePoint + QPointF(-12.0 * i, 0)); });
    steps.push_back([&r, s] { r.touchRelease(s->sidePoint + QPointF(-72, 0)); });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        const auto now = pointPositions(r);
        double moved = 0;
        for (const auto& [id, p] : s->points)
            if (now.count(id))
                moved = std::max(moved, (now.at(id) - p).length());
        r.check(!editorShown(r), name("the finger on the side closes the field"));
        r.check(now.size() == s->points.size() && moved < 1e-9, name("and the drag that follows moves nothing"),
                QStringLiteral("moved ") + AcceptanceRunner::num(moved));
        // ---- The view zooms while the field is open: the field stays on its dimension.
        r.check(tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name("tap the width dimension for the zoom"));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(editorShown(r), name("the field is open for the zoom"));
        r.wheel(emptySpot(r), 2);
    });
    wait(steps, 4);
    steps.push_back([&r, s, name] {
        const QRectF editor = sceneRect(r, QStringLiteral("dimensionEditor"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        const auto label = labelCenter(r, s->widthDimension);
        const bool onLabel = label && std::abs(editor.center().x() - label->x()) < 1.5 && std::abs(editor.center().y() - label->y()) < 1.5;
        // (Where the field on its label would reach the keypad, it moves off it.)
        const bool labelUnderPad =
            label && overlaps(QRectF(*label - QPointF(editor.width() / 2, editor.height() / 2), editor.size()), pad);
        r.check(editorShown(r) && (onLabel || labelUnderPad) && !overlaps(editor, pad), name("after the zoom the field is on its dimension, off the keypad"),
                rectText(editor) + (label ? QStringLiteral(" / label %1,%2").arg(label->x(), 0, 'f', 0).arg(label->y(), 0, 'f', 0) : QString()));
        r.screenshot(QStringLiteral("typedvalues_") + name("zoomed").replace(QStringLiteral(": "), QStringLiteral("_")));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), name("tap the check mark"));
    });
    wait(steps, 2);
    // ---- A polygon's sides refused: the message above the keypad.
    steps.push_back([&r, s, name] {
        r.check(!editorShown(r) && std::abs(sketchWidth(r) - 20) < 1e-9, name("still 20 wide"));
        s->lines = activeSketch(r) ? activeSketch(r)->lines().size() : 0;
        r.app().setSketchTool(QStringLiteral("polygon"));
    });
    steps.push_back([&r] { r.touchTap({emptySpot(r)}); });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(r.app().sketchDrawing(), name("a polygon's center"));
        r.check(tapItem(r, QStringLiteral("sketchInput_sides")), name("tap its sides"));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(tapKeys(r, QStringLiteral("99")) && tapItem(r, QStringLiteral("keypadKey_done")), name("tap 9, 9 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        QQuickItem* error = r.findItem(QStringLiteral("sketchTypingError"));
        const QString message = error && error->isVisible() ? error->property("text").toString() : QString();
        r.check(message == QStringLiteral("The number of sides must be a whole number from 3 to 64."), name("99 sides are refused"), message);
        const QRectF errorRect = sceneRect(r, QStringLiteral("sketchTypingError"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        r.check(keypadShown(r) && !errorRect.isEmpty() && !overlaps(errorRect, pad) && errorRect.bottom() <= pad.top(),
                name("the message shows above the keypad"), rectText(errorRect) + QStringLiteral(" / ") + rectText(pad));
        r.screenshot(QStringLiteral("typedvalues_") + name("sides").replace(QStringLiteral(": "), QStringLiteral("_")));
        r.check(tapItem(r, QStringLiteral("keypadKey_clear")) && tapKeys(r, QStringLiteral("5")) && tapItem(r, QStringLiteral("keypadKey_next")),
                name("tap C, 5, Next"));
    });
    wait(steps, 1);
    // (Next brings a length's keys: tapped once they are shown.)
    steps.push_back([&r, name] {
        r.check(tapKeys(r, QStringLiteral("20")) && tapItem(r, QStringLiteral("keypadKey_done")), name("tap 2, 0 (its size) and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, s, name] {
        const std::size_t lines = activeSketch(r) ? activeSketch(r)->lines().size() : 0;
        r.check(lines == s->lines + 5 && !r.app().sketchDrawing() && !keypadShown(r), name("a five-sided polygon"),
                QStringLiteral("%1 lines, drawing %2, keypad %3, error '%4'")
                    .arg(lines - s->lines)
                    .arg(r.app().sketchDrawing())
                    .arg(keypadShown(r))
                    .arg(r.app().typedValueError()));
        r.app().setDisplayUnit(QStringLiteral("mm"));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), name("Finish sketch"));
    });
    wait(steps, 2);
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    auto state = std::make_shared<State>();
    addConfig(out, r, Config{QStringLiteral("iphone"), 402, 874, QVariantList{62, 0, 34, 0}}, state);
    addConfig(out, r, Config{QStringLiteral("ipad"), 1180, 820, QVariant()}, state);
    out.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setTouchMode(false);
        r.app().setDisplayUnit(QStringLiteral("mm"));
    });
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("typedvalues"), 98, steps});

} // namespace
} // namespace os::app
