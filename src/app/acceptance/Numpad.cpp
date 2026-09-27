// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Values typed with a finger or the Apple Pencil (the owner, iPhone and iPad,
// 2026-09-27: "there is no easy way for me to type on iPad"; "as I type in
// 100, the model snaps to 1mm, then 10mm, and finally 100"). On an iPhone in
// portrait (402x874, Dynamic Island) and an iPad (1180x820), by touch: a
// box's top face is tapped, then its value; the app's numeric keypad comes
// up instead of the system keyboard (the field takes no input method), with
// keys of 44 pt and more, docked along the bottom of the phone (the value
// box at the top, the face in sight between them) and beside the value box
// on the iPad, clear of the face, the value box and the controls. 1, 0, 0
// tapped quickly previews nothing until the check mark, which makes the box
// 100 mm high. A hardware keyboard still types into the field (25, Enter).
// The Model panel's Box step takes a height from the keypad; a rectangle's
// live width and height (Next between them) and then its width dimension
// are typed on it in a sketch.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/NumericKeypad.h"
#include "ui/AppController.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtGui/QGuiApplication>
#include <QtGui/QInputMethodQueryEvent>
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

QRectF projected(AcceptanceRunner& r, const std::vector<Vec3>& points)
{
    double left = 1e9, top = 1e9, right = -1e9, bottom = -1e9;
    for (const Vec3& p : points) {
        const QPointF s = r.screenPoint(p.x, p.y, p.z);
        left = std::min(left, s.x());
        top = std::min(top, s.y());
        right = std::max(right, s.x());
        bottom = std::max(bottom, s.y());
    }
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

// A finger's tap on the middle of a QML item.
bool tapItem(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return false;
    r.touchTap({item->mapToScene(QPointF(item->width() / 2, item->height() / 2))});
    return true;
}

// Taps keypad keys, one per character ("-" is minus, "." the decimal point).
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

// The focused item would open the system keyboard (it takes input-method text).
bool systemKeyboardWanted()
{
    QObject* focus = QGuiApplication::focusObject();
    if (!focus)
        return false;
    QInputMethodQueryEvent query(Qt::ImEnabled);
    QCoreApplication::sendEvent(focus, &query);
    return query.value(Qt::ImEnabled).toBool();
}

QString focusName()
{
    QObject* focus = QGuiApplication::focusObject();
    return focus ? focus->objectName() + QStringLiteral(" (") + QString::fromLatin1(focus->metaObject()->className()) + QStringLiteral(")")
                 : QStringLiteral("none");
}

struct Config {
    QString name;
    int width = 0;
    int height = 0;
    QVariant safeArea;
    double safe[4] = {0, 0, 0, 0};
    bool phone() const { return width < 600; }
};

struct State {
    double volume = 0;
    double height = 0;
    QString valueBefore;
    QString boxStep; // the Box step's id in the Model panel
    int widthDimension = 0;
};

double bodyHeight(AcceptanceRunner& r)
{
    return r.app().bodyCount() == 0 ? -1.0 : geom::boundingBox(r.body(0).shape()).size().z;
}

// The sketch's points' extent (x and y).
QSizeF sketchExtent(AcceptanceRunner& r)
{
    const auto* session = r.app().interaction().sketchSession();
    if (!session || session->sketch().points().empty())
        return {};
    double left = 1e9, right = -1e9, bottom = 1e9, top = -1e9;
    for (const auto& [id, p] : session->sketch().points()) {
        left = std::min(left, p.position.x);
        right = std::max(right, p.position.x);
        bottom = std::min(bottom, p.position.y);
        top = std::max(top, p.position.y);
    }
    return QSizeF(right - left, top - bottom);
}

// The keypad is on screen inside the safe area, with keys of 44 pt and more.
void checkKeypad(AcceptanceRunner& r, const Config& c, const QString& what)
{
    const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
    r.check(keypadShown(r), c.name + QStringLiteral(": the keypad comes up for ") + what);
    const double w = r.window()->width();
    const double h = r.window()->height();
    const QRectF safe(c.safe[3], c.safe[0], w - c.safe[3] - c.safe[1], h - c.safe[0] - c.safe[2]);
    r.check(safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(pad), c.name + QStringLiteral(": the keypad is inside the safe area (") + what + QStringLiteral(")"),
            rectText(pad));
    double smallest = 1e9;
    int keys = 0;
    for (const char* id : {"0", "1", "5", "9", "back", "clear", "done", ".", "+", "mm", "in"}) {
        const QRectF key = sceneRect(r, QStringLiteral("keypadKey_") + QString::fromLatin1(id));
        if (key.isEmpty())
            continue;
        ++keys;
        smallest = std::min({smallest, key.width(), key.height()});
    }
    r.check(keys >= 4 && smallest >= 44, c.name + QStringLiteral(": the keys are 44 pt or more (") + what + QStringLiteral(")"),
            QStringLiteral("%1 keys, smallest %2").arg(keys).arg(smallest));
    r.check(!systemKeyboardWanted(), c.name + QStringLiteral(": the system keyboard stays down (") + what + QStringLiteral(")"), focusName());
    if (c.phone())
        r.check(std::abs(pad.bottom() - (h - c.safe[2])) <= 8 && pad.width() >= w - c.safe[1] - c.safe[3] - 10,
                c.name + QStringLiteral(": the keypad is docked along the bottom (") + what + QStringLiteral(")"), rectText(pad));
}

void addConfig(Steps& steps, AcceptanceRunner& r, const Config& c, const std::shared_ptr<State>& s)
{
    steps.push_back([&r, c] {
        r.app().newDocument();
        r.app().setTouchMode(true);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
    });
    wait(steps, 3);
    steps.push_back([&r, c] {
        r.check(r.window()->width() == c.width && r.window()->height() == c.height, c.name + QStringLiteral(": the window size"),
                QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
        r.check(tapItem(r, QStringLiteral("emptyAddBox")), c.name + QStringLiteral(": tap Add a box"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().bodyCount() == 1, c.name + QStringLiteral(": a box"));
        r.app().setTouchMode(true);
        r.app().fitAll();
    });
    wait(steps, 4);
    // ---- The value box: 1, 0, 0 and the check mark.
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 20)}); });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), c.name + QStringLiteral(": the top face offers push/pull"),
                r.app().operationTitle());
        s->volume = r.bodyVolume();
        s->valueBefore = r.app().operationValueText();
        r.check(tapItem(r, QStringLiteral("valueChipField")), c.name + QStringLiteral(": tap the value"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        checkKeypad(r, c, QStringLiteral("the value box"));
        QQuickItem* chip = r.findItem(QStringLiteral("valueChip"));
        QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        r.check(chip && chip->property("typing").toBool() && field && field->hasActiveFocus(),
                c.name + QStringLiteral(": the value field has the focus (the keypad types into it)"));
        r.check(field && field->property("readOnly").toBool(), c.name + QStringLiteral(": the field itself takes no keyboard text on touch"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        const QRectF chipRect = sceneRect(r, QStringLiteral("valueChip"));
        r.check(!overlaps(pad, chipRect), c.name + QStringLiteral(": the keypad leaves the value box clear"),
                rectText(pad) + QStringLiteral(" / ") + rectText(chipRect));
        const QRectF face = projected(r, {{-10, -10, 20}, {10, -10, 20}, {10, 10, 20}, {-10, 10, 20}});
        r.check(!overlaps(pad, face), c.name + QStringLiteral(": the keypad keeps clear of the selected face"),
                rectText(pad) + QStringLiteral(" / ") + rectText(face));
        r.check(!overlaps(chipRect, face), c.name + QStringLiteral(": so does the value box"),
                rectText(chipRect) + QStringLiteral(" / ") + rectText(face));
        if (const auto keep = r.app().interaction().keepClearRect()) {
            const QRectF k(QPointF(keep->left, keep->top), QPointF(keep->right, keep->bottom));
            r.check(!overlaps(pad, k), c.name + QStringLiteral(": the keypad is off the face and its arrow"),
                    rectText(pad) + QStringLiteral(" / ") + rectText(k));
        }
        if (c.phone()) {
            const QString spot = chip ? chip->property("placement").toMap().value(QStringLiteral("spot")).toString() : QString();
            r.check(spot == QStringLiteral("dockTop") && chipRect.bottom() < pad.top(),
                    c.name + QStringLiteral(": the value box is at the top, above the keypad"), spot + QStringLiteral(" ") + rectText(chipRect));
        } else {
            // Beside the value box (and the value box keeps off it).
            const double gap = std::max({pad.left() - chipRect.right(), chipRect.left() - pad.right(), pad.top() - chipRect.bottom(),
                                         chipRect.top() - pad.bottom()});
            r.check(gap >= 0 && gap <= interact::kKeypadGap + 40, c.name + QStringLiteral(": the keypad is next to the value box"),
                    QString::number(gap, 'f', 1));
            for (const char* control : {"topBar", "createPanel", "historyPanel", "viewPanel", "axisTriad", "statusColumn"}) {
                const QRectF other = sceneRect(r, QString::fromLatin1(control));
                r.check(other.isEmpty() || !overlaps(pad, other),
                        c.name + QStringLiteral(": the keypad covers no control (") + QString::fromLatin1(control) + QStringLiteral(")"),
                        rectText(pad) + QStringLiteral(" / ") + rectText(other));
            }
        }
        r.screenshot(QStringLiteral("numpad_") + c.name + QStringLiteral("_value"));
        // 1, 0, 0 in quick succession: nothing is previewed yet ...
        r.check(tapKeys(r, QStringLiteral("100")), c.name + QStringLiteral(": tap 1, 0, 0"));
        r.check(field && field->property("text").toString() == QStringLiteral("100"), c.name + QStringLiteral(": the field shows 100"),
                field ? field->property("text").toString() : QString());
        r.check(r.app().typingPending() && r.app().operationValueText() == s->valueBefore,
                c.name + QStringLiteral(": typed quickly, the model keeps its value until typing pauses"), r.app().operationValueText());
        // ... and the check mark applies exactly what was typed.
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap the keypad's check mark"));
    });
    steps.push_back([&r, c, s] {
        const double h = bodyHeight(r);
        r.check(std::abs(h - 100) < 1e-6 && std::abs(r.bodyVolume() - s->volume * 5) < 1e-3,
                c.name + QStringLiteral(": 1, 0, 0 and the check mark make the box 100 mm high"),
                AcceptanceRunner::num(h) + QStringLiteral(" / volume ") + AcceptanceRunner::num(r.bodyVolume()));
        r.check(!keypadShown(r), c.name + QStringLiteral(": the keypad goes with the check mark"));
        r.app().fitAll();
    });
    wait(steps, 4);
    // ---- A hardware keyboard still types into the value (an iPad's keyboard case).
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 100)}); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), c.name + QStringLiteral(": the top face again"),
                r.app().operationTitle());
        r.check(tapItem(r, QStringLiteral("valueChipField")), c.name + QStringLiteral(": tap the value again"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(keypadShown(r), c.name + QStringLiteral(": the keypad is up"));
        r.type(QStringLiteral("25"));
        QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        r.check(field && field->property("text").toString() == QStringLiteral("25"), c.name + QStringLiteral(": hardware keys type into the field"),
                field ? field->property("text").toString() : QString());
        r.key(Qt::Key_Return);
    });
    steps.push_back([&r, c, s] {
        const double h = bodyHeight(r);
        r.check(std::abs(h - 25) < 1e-6, c.name + QStringLiteral(": 25 and Enter from the keyboard make it 25 mm high"), AcceptanceRunner::num(h));
        r.check(!keypadShown(r), c.name + QStringLiteral(": Enter puts the keypad away"));
        s->height = h;
        if (c.phone())
            r.check(r.clickItem(QStringLiteral("modelPanelButton")), c.name + QStringLiteral(": the Model button"));
    });
    wait(steps, 3);
    // ---- The Model panel: the Box step's height.
    steps.push_back([&r, c, s] {
        s->boxStep.clear();
        for (const QVariant& row : r.app().history()) {
            const QVariantMap map = row.toMap();
            if (map.value(QStringLiteral("kind")).toString() != QStringLiteral("feature"))
                continue;
            for (const QVariant& p : map.value(QStringLiteral("parameters")).toList())
                if (p.toMap().value(QStringLiteral("key")).toString() == QStringLiteral("height"))
                    s->boxStep = map.value(QStringLiteral("id")).toString();
        }
        r.check(!s->boxStep.isEmpty() && tapItem(r, QStringLiteral("historyRow_") + s->boxStep),
                c.name + QStringLiteral(": tap the Box step in the Model panel"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        r.check(tapItem(r, QStringLiteral("historyParam_") + s->boxStep + QStringLiteral("_height")),
                c.name + QStringLiteral(": tap its Height"));
    });
    wait(steps, 3);
    steps.push_back([&r, c, s] {
        checkKeypad(r, c, QStringLiteral("a Model panel number"));
        const QString name = QStringLiteral("historyParam_") + s->boxStep + QStringLiteral("_height");
        QQuickItem* field = r.findItem(name);
        const QRectF fieldRect = sceneRect(r, name);
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        r.check(field && field->hasActiveFocus() && field->property("readOnly").toBool(),
                c.name + QStringLiteral(": the Height field has the focus, the keypad types"));
        r.check(!fieldRect.isEmpty() && !overlaps(pad, fieldRect), c.name + QStringLiteral(": the keypad leaves the Height field in sight"),
                rectText(pad) + QStringLiteral(" / ") + rectText(fieldRect));
        if (!c.phone()) {
            const QRectF panel = sceneRect(r, QStringLiteral("historyPanel"));
            r.check(!overlaps(pad, panel), c.name + QStringLiteral(": beside the Model panel"), rectText(pad) + QStringLiteral(" / ") + rectText(panel));
        }
        r.screenshot(QStringLiteral("numpad_") + c.name + QStringLiteral("_model"));
        r.check(tapKeys(r, QStringLiteral("30")) && tapItem(r, QStringLiteral("keypadKey_done")),
                c.name + QStringLiteral(": tap 3, 0 and the check mark"));
    });
    wait(steps, 1);
    steps.push_back([&r, c, s] {
        const double h = bodyHeight(r);
        r.check(std::abs(h - (s->height + 10)) < 1e-6, c.name + QStringLiteral(": the box step is 30 high: 10 mm more"),
                AcceptanceRunner::num(h) + QStringLiteral(" (was ") + AcceptanceRunner::num(s->height) + QStringLiteral(")"));
        r.check(!keypadShown(r), c.name + QStringLiteral(": the keypad goes"));
        if (c.phone())
            r.check(r.clickItem(QStringLiteral("historyPanelHide")), c.name + QStringLiteral(": close the Model panel"));
    });
    wait(steps, 2);
    // ---- A sketch: a rectangle's live values, then its width dimension.
    steps.push_back([&r] {
        r.app().newDocument();
        r.app().setTouchMode(true);
    });
    wait(steps, 2);
    steps.push_back([&r, c] { r.check(tapItem(r, QStringLiteral("emptyStartSketch")), c.name + QStringLiteral(": tap Start a sketch")); });
    wait(steps, 5);
    steps.push_back([&r, c] {
        r.check(r.app().sketchMode(), c.name + QStringLiteral(": sketching"));
        r.check(tapItem(r, QStringLiteral("tool_rectangle")), c.name + QStringLiteral(": the Rectangle tool"));
    });
    steps.push_back([&r] { r.touchTap({r.screenPoint(3, 4, 0)}); });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.check(r.app().sketchDrawing(), c.name + QStringLiteral(": a tap starts the rectangle"));
        r.check(tapItem(r, QStringLiteral("sketchInput_width")), c.name + QStringLiteral(": tap its live width"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        checkKeypad(r, c, QStringLiteral("a live value"));
        QQuickItem* keypad = r.findItem(QStringLiteral("numericKeypad"));
        r.check(keypad && keypad->hasActiveFocus(), c.name + QStringLiteral(": the keypad takes the keys (no field)"), focusName());
        const QRectF display = sceneRect(r, QStringLiteral("keypadDisplay"));
        r.check(!display.isEmpty(), c.name + QStringLiteral(": the keypad shows the value typed"));
        r.screenshot(QStringLiteral("numpad_") + c.name + QStringLiteral("_live"));
        r.check(tapKeys(r, QStringLiteral("12")), c.name + QStringLiteral(": tap 1, 2 (width)"));
        r.check(tapItem(r, QStringLiteral("keypadKey_next")), c.name + QStringLiteral(": tap Next"));
        r.check(tapKeys(r, QStringLiteral("8")), c.name + QStringLiteral(": tap 8 (height)"));
        r.check(tapItem(r, QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, s] {
        const QSizeF extent = sketchExtent(r);
        r.check(std::abs(extent.width() - 12) < 1e-6 && std::abs(extent.height() - 8) < 1e-6,
                c.name + QStringLiteral(": a 12 x 8 rectangle from the keypad"),
                AcceptanceRunner::num(extent.width()) + QStringLiteral(" x ") + AcceptanceRunner::num(extent.height()));
        r.check(!r.app().sketchDrawing() && !keypadShown(r), c.name + QStringLiteral(": the rectangle is done, the keypad goes"));
        s->widthDimension = 0;
        if (const auto* session = r.app().interaction().sketchSession())
            for (const auto& [id, constraint] : session->sketch().constraints())
                if (constraint.kind == sketch::ConstraintKind::HorizontalDistance)
                    s->widthDimension = int(id);
        r.check(s->widthDimension != 0 && tapItem(r, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)),
                c.name + QStringLiteral(": tap the width dimension"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        checkKeypad(r, c, QStringLiteral("a dimension"));
        const QRectF editor = sceneRect(r, QStringLiteral("dimensionEditor"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        r.check(!editor.isEmpty() && !overlaps(pad, editor), c.name + QStringLiteral(": the keypad leaves the dimension's field in sight"),
                rectText(pad) + QStringLiteral(" / ") + rectText(editor));
        if (!c.phone()) {
            // Beside the value, off the rectangle it sizes.
            const auto sketch = r.app().interaction().sketchScreenRect();
            const QRectF drawn = sketch ? QRectF(QPointF(sketch->left, sketch->top), QPointF(sketch->right, sketch->bottom)) : QRectF();
            r.check(sketch && !overlaps(pad, drawn), c.name + QStringLiteral(": the keypad keeps clear of the rectangle"),
                    rectText(pad) + QStringLiteral(" / ") + rectText(drawn));
        }
        r.screenshot(QStringLiteral("numpad_") + c.name + QStringLiteral("_dimension"));
        r.check(tapKeys(r, QStringLiteral("15")) && tapItem(r, QStringLiteral("keypadKey_done")),
                c.name + QStringLiteral(": tap 1, 5 and the check mark"));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        const QSizeF extent = sketchExtent(r);
        r.check(std::abs(extent.width() - 15) < 1e-6 && std::abs(extent.height() - 8) < 1e-6,
                c.name + QStringLiteral(": the width dimension is 15 now"),
                AcceptanceRunner::num(extent.width()) + QStringLiteral(" x ") + AcceptanceRunner::num(extent.height()));
        r.check(!keypadShown(r), c.name + QStringLiteral(": the keypad goes"));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), c.name + QStringLiteral(": Finish sketch"));
    });
    wait(steps, 2);
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    auto state = std::make_shared<State>();
    Config phone{QStringLiteral("iphone"), 402, 874, QVariantList{62, 0, 34, 0}, {62, 0, 34, 0}};
    Config tablet{QStringLiteral("ipad"), 1180, 820, QVariant(), {0, 0, 0, 0}};
    addConfig(out, r, phone, state);
    addConfig(out, r, tablet, state);
    out.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setTouchMode(false);
    });
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("numpad"), 97, steps});

} // namespace
} // namespace os::app
