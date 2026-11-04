// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Measurements kept in the project, typed by name in any value box
// (docs/MEASURE.md, "Measurements in the project"), on the desktop (mouse
// and keyboard), an iPhone (402x874, the keypad) and an iPad (1180x820):
// - added through the API (the Measure workspace's way: display names
//   become value names, a taken name gets _2) and with the Model panel's
//   "+" (a click, a tap);
// - "shelf_depth - 2" typed into a push/pull's value box (the keyboard; on
//   touch the keypad's name key, which lists lengths only);
// - lengths by name in a rectangle's live values and its width dimension;
//   an angle's name in a rotation;
// - renamed in the panel, the old name refused; a new value typed with a
//   name; deleted; undo and redo; saved and reopened.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

constexpr double kPiScenario = 3.14159265358979323846;

void wait(Steps& steps, int count)
{
    for (int i = 0; i < count; ++i)
        steps.push_back([] {});
}

struct Config {
    QString name;
    int width = 0; // 0: the size the run started with (the desktop)
    int height = 0;
    QVariant safeArea;
    bool touch = false;
    bool phone() const { return width > 0 && width < 600; }
};

struct State {
    QString project;
    double volume = 0;
    int widthDimension = 0;
    QPointF sketchStart; // where the rectangle starts (window)
};

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

QString textOf(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item ? item->property("text").toString() : QString();
}

// A click (the desktop) or a tap (touch) on an item.
bool press(AcceptanceRunner& r, const Config& c, const QString& name) { return c.touch ? r.tapItem(name) : r.clickItem(name); }


bool keypadShown(AcceptanceRunner& r) { return shown(r, QStringLiteral("numericKeypad")); }

// The project's measurement by name: its value, or NaN.
double measured(AcceptanceRunner& r, const QString& name)
{
    for (const QVariant& m : r.app().projectMeasurements()) {
        const QVariantMap map = m.toMap();
        if (map.value(QStringLiteral("name")).toString() == name)
            return map.value(QStringLiteral("value")).toDouble();
    }
    return std::nan("");
}

QString names(AcceptanceRunner& r)
{
    QStringList out;
    for (const QVariant& m : r.app().projectMeasurements())
        out << m.toMap().value(QStringLiteral("name")).toString();
    return out.join(QStringLiteral(", "));
}

double bodyHeight(AcceptanceRunner& r) { return r.app().bodyCount() > 0 ? geom::boundingBox(r.body(0).shape()).size().z : -1; }

// The Model panel in sight (a phone's slides in behind its button).
void openPanel(Steps& steps, AcceptanceRunner& r, const Config& c)
{
    steps.push_back([&r, c] {
        if (c.phone() && !shown(r, QStringLiteral("historyPanel")))
            r.check(r.tapItem(QStringLiteral("modelPanelButton")), c.name + QStringLiteral(": tap Model"));
    });
    wait(steps, 3);
}

void closePanel(Steps& steps, AcceptanceRunner& r, const Config& c)
{
    steps.push_back([&r, c] {
        if (c.phone())
            r.check(r.tapItem(QStringLiteral("modelPanelButton")), c.name + QStringLiteral(": tap Model to close the panel"));
    });
    wait(steps, 3);
}

// Types `text` into the focused value: the keyboard on the desktop; on touch
// the keypad, its name keys for names (`keys`: k:<key> or n:<name>).
void typeValue(AcceptanceRunner& r, const Config& c, const QStringList& keys, const QString& keyboard, const QString& what)
{
    if (!c.touch) {
        r.type(keyboard);
        return;
    }
    bool all = true;
    for (const QString& k : keys)
        all = r.tapItem((k.startsWith(QStringLiteral("n:")) ? QStringLiteral("keypadName_") : QStringLiteral("keypadKey_")) + k.mid(2)) && all;
    r.check(all, c.name + QStringLiteral(": tap ") + what + QStringLiteral(" on the keypad"));
}

void confirm(AcceptanceRunner& r, const Config& c, const QString& what)
{
    if (c.touch)
        r.check(r.tapItem(QStringLiteral("keypadKey_done")), c.name + QStringLiteral(": tap the check mark (") + what + QStringLiteral(")"));
    else
        r.key(Qt::Key_Return);
}

void addConfig(Steps& steps, AcceptanceRunner& r, const Config& c, const std::shared_ptr<State>& s)
{
    const auto name = [c](const QString& what) { return c.name + QStringLiteral(": ") + what; };
    const auto shot = [c](const char* what) { return QStringLiteral("projectmeasurements_") + c.name + QStringLiteral("_") + QString::fromLatin1(what); };
    steps.push_back([&r, c] {
        r.app().newDocument();
        r.app().setTouchMode(c.touch);
        r.app().setDisplayUnit(QStringLiteral("mm"));
        if (c.width > 0)
            r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
        r.app().createBox(20); // (-10,-10,0) .. (10,10,20)
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        r.app().setTouchMode(c.touch);
        r.app().fitAll();
    });
    wait(steps, 4);
    // ---- None yet: the Model panel's header offers the first; its form opens and cancels.
    openPanel(steps, r, c);
    steps.push_back([&r, c, name] {
        r.check(shown(r, QStringLiteral("measurementAddFirst")) && !shown(r, QStringLiteral("measurementHint")),
                name(QStringLiteral("no measurements: + Measurement in the panel's header, no section")));
        r.check(press(r, c, QStringLiteral("measurementAddFirst")), name(QStringLiteral("+ Measurement")));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name] {
        r.check(shown(r, QStringLiteral("measurementAddForm")) && !shown(r, QStringLiteral("measurementAddFirst")),
                name(QStringLiteral("the form opens below the steps")));
        r.check(press(r, c, QStringLiteral("measurementAddCancel")), name(QStringLiteral("Cancel")));
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(!shown(r, QStringLiteral("measurementAddForm")) && !shown(r, QStringLiteral("measurementHint"))
                    && shown(r, QStringLiteral("measurementAddFirst")),
                name(QStringLiteral("cancelled: the panel as it was")));
    });
    // ---- Through the API: display names become value names; a taken name gets _2.
    steps.push_back([&r, name] {
        ui::AppController& app = r.app();
        r.check(app.identifierFor(QStringLiteral("Length 3")) == QStringLiteral("length_3"), name(QStringLiteral("Length 3 is length_3")),
                app.identifierFor(QStringLiteral("Length 3")));
        const QString depth = app.addProjectMeasurement(QStringLiteral("Shelf depth"), QStringLiteral("length"), 30, QStringLiteral("Ruler"));
        const QString corner = app.addProjectMeasurement(QStringLiteral("Corner angle"), QStringLiteral("angle"), 60, QStringLiteral("Fold"));
        const QString again = app.addProjectMeasurement(QStringLiteral("Shelf depth"), QStringLiteral("length"), 12.5);
        r.check(depth == QStringLiteral("shelf_depth") && corner == QStringLiteral("corner_angle") && again == QStringLiteral("shelf_depth_2"),
                name(QStringLiteral("added through the API as shelf_depth, corner_angle, shelf_depth_2")), depth + QStringLiteral(", ") + corner + QStringLiteral(", ") + again);
        r.check(app.addProjectMeasurement(QStringLiteral("x"), QStringLiteral("volume"), 1).isEmpty(), name(QStringLiteral("an unknown kind is refused")));
        r.check(app.projectMeasurements().size() == 3, name(QStringLiteral("three measurements")), names(r));
    });
    openPanel(steps, r, c);
    // ---- The Model panel: the rows, then "+" by click or tap.
    steps.push_back([&r, c, name, shot] {
        r.check(shown(r, QStringLiteral("measurementRow_shelf_depth")), name(QStringLiteral("the Model panel lists shelf_depth")));
        const QString text = textOf(r, QStringLiteral("measurementText_shelf_depth"));
        r.check(text == QStringLiteral("shelf_depth = 30.0 mm"), name(QStringLiteral("as shelf_depth = 30.0 mm")), text);
        const QString angle = textOf(r, QStringLiteral("measurementText_corner_angle"));
        r.check(angle == QString::fromUtf8("corner_angle = 60.0\xC2\xB0"), name(QStringLiteral("and corner_angle = 60.0 degrees")), angle);
        r.check(textOf(r, QStringLiteral("measurementHint")).startsWith(QStringLiteral("Type a name in any value box")), name(QStringLiteral("the hint")));
        QQuickItem* row = r.findItem(QStringLiteral("measurementRow_shelf_depth"));
        QQuickItem* plus = r.findItem(QStringLiteral("measurementAdd"));
        if (c.touch)
            r.check(row && plus && row->parentItem() && row->parentItem()->parentItem()->height() >= 44 && plus->height() >= 44,
                    name(QStringLiteral("rows and + are 44 pt for a finger")),
                    QStringLiteral("%1 / %2").arg(row && row->parentItem() ? row->parentItem()->parentItem()->height() : 0).arg(plus ? plus->height() : 0));
        r.screenshot(shot("panel"));
        r.check(press(r, c, QStringLiteral("measurementAdd")), name(QStringLiteral("+ in the Model panel")));
    });
    wait(steps, 3);
    steps.push_back([&r, c, name] {
        r.check(shown(r, QStringLiteral("measurementAddForm")), name(QStringLiteral("the form to add one")));
        if (c.touch)
            r.check(r.tapItem(QStringLiteral("measurementAddName")), name(QStringLiteral("tap its name")));
    });
    wait(steps, 1);
    steps.push_back([&r, c, name] {
        r.type(QStringLiteral("Wall")); // a name: the system keyboard on touch
        if (c.touch) {
            r.check(r.tapItem(QStringLiteral("measurementAddValue")), name(QStringLiteral("tap its value")));
        } else {
            r.key(Qt::Key_Return); // to the value
        }
    });
    wait(steps, 2);
    steps.push_back([&r, c, name, shot] {
        if (c.touch)
            r.check(keypadShown(r), name(QStringLiteral("the keypad types the new value")));
        typeValue(r, c, {QStringLiteral("k:2"), QStringLiteral("k:."), QStringLiteral("k:5")}, QStringLiteral("2.5"), QStringLiteral("2.5"));
        r.screenshot(shot("add"));
        if (c.touch)
            confirm(r, c, QStringLiteral("add"));
        else
            r.check(r.clickItem(QStringLiteral("measurementAddConfirm")), name(QStringLiteral("click Add")));
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(std::abs(measured(r, QStringLiteral("Wall")) - 2.5) < 1e-12, name(QStringLiteral("Wall = 2.5 mm added with +")), names(r));
        r.check(!shown(r, QStringLiteral("measurementAddForm")) && !keypadShown(r), name(QStringLiteral("the form and the keypad go")));
        r.check(r.app().undoText().contains(QStringLiteral("measurement")), name(QStringLiteral("one undo step")), r.app().undoText());
    });
    closePanel(steps, r, c);
    // ---- A push/pull: shelf_depth - 2.
    steps.push_back([&r, c, s] {
        s->volume = r.bodyVolume();
        if (c.touch)
            r.touchTap({r.screenPoint(0, 0, 20)});
        else
            r.click(r.screenPoint(0, 0, 20));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), name(QStringLiteral("the top face offers push/pull")), r.app().operationTitle());
        r.check(press(r, c, QStringLiteral("valueChipField")), name(QStringLiteral("the value box")));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name, shot] {
        if (c.touch) {
            r.check(keypadShown(r) && shown(r, QStringLiteral("keypadName_shelf_depth")) && shown(r, QStringLiteral("keypadName_Wall")),
                    name(QStringLiteral("the keypad offers the lengths by name")));
            r.check(!shown(r, QStringLiteral("keypadName_corner_angle")), name(QStringLiteral("but not the angle")));
            QQuickItem* key = r.findItem(QStringLiteral("keypadName_shelf_depth"));
            r.check(key && key->height() >= 44, name(QStringLiteral("a name key is 44 pt")));
        }
        typeValue(r, c, {QStringLiteral("n:shelf_depth"), QStringLiteral("k:-"), QStringLiteral("k:2")}, QStringLiteral("shelf_depth - 2"),
                  QStringLiteral("shelf_depth, minus, 2"));
        const QString typed = textOf(r, QStringLiteral("valueChipField"));
        r.check(typed == (c.touch ? QStringLiteral("shelf_depth-2") : QStringLiteral("shelf_depth - 2")), name(QStringLiteral("the box shows the name")), typed);
        r.screenshot(shot("pushpull"));
        confirm(r, c, QStringLiteral("push/pull"));
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(std::abs(bodyHeight(r) - 28) < 1e-6 && std::abs(r.bodyVolume() - 400 * 28) < 1e-3,
                name(QStringLiteral("shelf_depth - 2 makes the box 28 mm high")), AcceptanceRunner::num(bodyHeight(r)));
        r.check(r.app().typedValueError().isEmpty(), name(QStringLiteral("no error")), r.app().typedValueError());
        r.app().handleKey(Qt::Key_Escape);
        r.app().handleKey(Qt::Key_Escape);
        // ---- A sketch beside the box: a rectangle's live values by name.
        r.app().startSketch();
    });
    wait(steps, 5);
    steps.push_back([&r, c, s, name, shot] {
        r.check(r.app().sketchMode(), name(QStringLiteral("sketching")));
        r.app().setSketchTool(QStringLiteral("rectangle"));
        // Beside the box, where no control covers the view.
        s->sketchStart = r.uncoveredScreenPoint({{14, 14, 0}, {-30, 14, 0}, {14, -30, 0}, {-30, -30, 0}, {0, 16, 0}, {16, 0, 0}});
        r.screenshot(shot("sketch"));
        if (c.touch)
            r.touchTap({s->sketchStart});
        else
            r.click(s->sketchStart);
    });
    wait(steps, 2);
    steps.push_back([&r, c, s, name] {
        if (!c.touch)
            r.mouseMove(s->sketchStart + QPointF(40, -30));
        QQuickItem* under = r.itemAt(s->sketchStart);
        r.check(r.app().sketchDrawing(), name(QStringLiteral("a rectangle started")),
                QStringLiteral("at %1,%2 over %3").arg(s->sketchStart.x()).arg(s->sketchStart.y()).arg(under ? under->objectName() : QStringLiteral("nothing")));
        if (c.touch)
            r.check(r.tapItem(QStringLiteral("sketchInput_width")), name(QStringLiteral("tap its live width")));
    });
    wait(steps, 2);
    steps.push_back([&r, c] {
        if (c.touch) {
            typeValue(r, c, {QStringLiteral("n:Wall"), QStringLiteral("k:*"), QStringLiteral("k:4"), QStringLiteral("k:next"), QStringLiteral("n:shelf_depth")},
                      QString(), QStringLiteral("Wall, times, 4, Next, shelf_depth"));
            confirm(r, c, QStringLiteral("the rectangle"));
        } else {
            r.type(QStringLiteral("wall*4"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("shelf_depth"));
            r.key(Qt::Key_Return);
        }
    });
    wait(steps, 3);
    steps.push_back([&r, s, name] {
        const auto* session = r.app().interaction().sketchSession();
        double left = 1e9, right = -1e9, bottom = 1e9, top = -1e9;
        s->widthDimension = 0;
        if (session) {
            for (const auto& [id, p] : session->sketch().points()) {
                if (p.fixed)
                    continue; // the origin
                left = std::min(left, p.position.x);
                right = std::max(right, p.position.x);
                bottom = std::min(bottom, p.position.y);
                top = std::max(top, p.position.y);
            }
            for (const auto& [id, constraint] : session->sketch().constraints())
                if (constraint.kind == sketch::ConstraintKind::HorizontalDistance)
                    s->widthDimension = int(id);
        }
        r.check(std::abs(right - left - 10) < 1e-9 && std::abs(top - bottom - 30) < 1e-9,
                name(QStringLiteral("wall * 4 by shelf_depth: a rectangle 10 x 30")),
                AcceptanceRunner::num(right - left) + QStringLiteral(" x ") + AcceptanceRunner::num(top - bottom) + QStringLiteral(" ") + r.app().typedValueError());
        r.check(s->widthDimension != 0, name(QStringLiteral("its width is a dimension")));
    });
    wait(steps, 1);
    // ---- The width dimension, edited with a name.
    steps.push_back([&r, c, s, name] {
        r.check(press(r, c, QStringLiteral("dimensionLabel_%1").arg(s->widthDimension)), name(QStringLiteral("the width dimension")));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name] {
        r.check(shown(r, QStringLiteral("dimensionEditor")), name(QStringLiteral("its field")));
        if (c.touch)
            r.check(r.tapItem(QStringLiteral("keypadKey_clear")), name(QStringLiteral("tap C")));
        else
            r.key(Qt::Key_A, Qt::ControlModifier);
        typeValue(r, c, {QStringLiteral("k:("), QStringLiteral("n:shelf_depth"), QStringLiteral("k:+"), QStringLiteral("n:Wall"), QStringLiteral("k:)"),
                         QStringLiteral("k:/"), QStringLiteral("k:2")},
                  QStringLiteral("(shelf_depth + wall) / 2"), QStringLiteral("(shelf_depth + Wall) / 2"));
        confirm(r, c, QStringLiteral("the dimension"));
    });
    wait(steps, 3);
    steps.push_back([&r, c, name] {
        double left = 1e9, right = -1e9;
        if (const auto* session = r.app().interaction().sketchSession())
            for (const auto& [id, p] : session->sketch().points()) {
                if (p.fixed)
                    continue; // the origin
                left = std::min(left, p.position.x);
                right = std::max(right, p.position.x);
            }
        r.check(std::abs(right - left - 16.25) < 1e-9 && !shown(r, QStringLiteral("dimensionEditor")),
                name(QStringLiteral("(shelf_depth + wall) / 2: 16.25 wide")), AcceptanceRunner::num(right - left));
        r.check(press(r, c, QStringLiteral("finishSketchButton")), name(QStringLiteral("Finish sketch")));
    });
    wait(steps, 3);
    // ---- A rotation by an angle's name.
    steps.push_back([&r, c] {
        r.app().setTouchMode(c.touch); // (Finish was clicked)
        r.app().selectBody(QString::fromStdString(r.body(0).id().toString()), false);
    });
    wait(steps, 2);
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("tool_rotate")), name(QStringLiteral("Rotate"))); });
    wait(steps, 2);
    steps.push_back([&r, c, name] {
        r.check(r.app().operationValueLabel() == QStringLiteral("Angle Z"), name(QStringLiteral("turning it on the table")), r.app().operationValueLabel());
        r.check(press(r, c, QStringLiteral("valueChipField")), name(QStringLiteral("the angle")));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name, shot] {
        if (c.touch) {
            r.check(shown(r, QStringLiteral("keypadName_corner_angle")) && !shown(r, QStringLiteral("keypadName_shelf_depth")),
                    name(QStringLiteral("an angle's keypad offers the angle by name, not the lengths")));
            r.screenshot(shot("rotate"));
        }
        typeValue(r, c, {QStringLiteral("n:corner_angle"), QStringLiteral("k:/"), QStringLiteral("k:2")}, QStringLiteral("corner_angle / 2"),
                  QStringLiteral("corner_angle, divide, 2"));
        confirm(r, c, QStringLiteral("rotation"));
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        const auto bb = geom::boundingBox(r.body(0).shape());
        const double across = 20 * (std::cos(kPiScenario / 6) + std::sin(kPiScenario / 6));
        r.check(std::abs(bb.size().x - across) < 1e-6 && std::abs(bb.size().y - across) < 1e-6,
                name(QStringLiteral("corner_angle / 2 turns the box 30 degrees")),
                AcceptanceRunner::num(bb.size().x) + QStringLiteral(" x ") + AcceptanceRunner::num(bb.size().y));
        r.app().handleKey(Qt::Key_Escape);
        r.app().handleKey(Qt::Key_Escape);
    });
    wait(steps, 2);
    // ---- Rename in the panel; the old name is refused.
    openPanel(steps, r, c);
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementRow_shelf_depth")), name(QStringLiteral("open shelf_depth"))); });
    wait(steps, 2);
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementName_shelf_depth")), name(QStringLiteral("its name"))); });
    wait(steps, 1);
    steps.push_back([&r, shot] {
        r.key(Qt::Key_A, Qt::ControlModifier);
        r.type(QStringLiteral("Depth")); // the system keyboard on touch
        r.screenshot(shot("rename"));
        r.key(Qt::Key_Return);
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(std::abs(measured(r, QStringLiteral("Depth")) - 30) < 1e-12 && std::isnan(measured(r, QStringLiteral("shelf_depth"))),
                name(QStringLiteral("renamed Depth")), names(r));
        r.check(r.app().setProjectMeasurementValue(QStringLiteral("Wall"), QStringLiteral("shelf_depth + 1"))
                    == QStringLiteral("No measurement called 'shelf_depth' in this project"),
                name(QStringLiteral("the old name is refused")));
    });
    // ---- A new value in the panel: the old name refused there too, then by name.
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementRow_Wall")), name(QStringLiteral("open Wall"))); });
    wait(steps, 2);
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementValue_Wall")), name(QStringLiteral("its value"))); });
    wait(steps, 2);
    steps.push_back([&r, c, name] {
        if (c.touch) {
            r.check(keypadShown(r), name(QStringLiteral("the keypad for its value")));
            r.check(r.tapItem(QStringLiteral("keypadKey_clear")), name(QStringLiteral("tap C")));
        }
        r.type(QStringLiteral("shelf_depth")); // (a hardware keyboard on touch)
        confirm(r, c, QStringLiteral("the old name"));
    });
    wait(steps, 2);
    steps.push_back([&r, c, name, shot] {
        const QString error = textOf(r, QStringLiteral("measurementError_Wall"));
        r.check(shown(r, QStringLiteral("measurementError_Wall")) && error == QStringLiteral("No measurement called 'shelf_depth' in this project"),
                name(QStringLiteral("the panel refuses the old name")), error);
        r.check(std::abs(measured(r, QStringLiteral("Wall")) - 2.5) < 1e-12, name(QStringLiteral("Wall stays 2.5")));
        r.screenshot(shot("refused"));
        if (c.touch)
            r.check(r.tapItem(QStringLiteral("keypadKey_clear")), name(QStringLiteral("tap C")));
        else
            r.key(Qt::Key_A, Qt::ControlModifier);
        typeValue(r, c, {QStringLiteral("n:Depth"), QStringLiteral("k:/"), QStringLiteral("k:1"), QStringLiteral("k:0")}, QStringLiteral("depth / 10"),
                  QStringLiteral("Depth, divide, 1, 0"));
        confirm(r, c, QStringLiteral("Depth / 10"));
    });
    wait(steps, 3);
    steps.push_back([&r, name] {
        r.check(std::abs(measured(r, QStringLiteral("Wall")) - 3) < 1e-12, name(QStringLiteral("Wall = Depth / 10 = 3 mm")), QString::number(measured(r, QStringLiteral("Wall"))));
        const QString text = textOf(r, QStringLiteral("measurementText_Wall"));
        r.check(text == QStringLiteral("Wall = 3.0 mm"), name(QStringLiteral("the row says so")), text);
        r.check(std::abs(bodyHeight(r) - 28) < 1e-6, name(QStringLiteral("the box keeps its 28 mm (no live link)")), AcceptanceRunner::num(bodyHeight(r)));
    });
    // ---- Delete, undo, redo.
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementRow_shelf_depth_2")), name(QStringLiteral("open shelf_depth_2"))); });
    wait(steps, 2);
    steps.push_back([&r, c, name] { r.check(press(r, c, QStringLiteral("measurementDelete_shelf_depth_2")), name(QStringLiteral("Delete"))); });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(r.app().projectMeasurements().size() == 3 && std::isnan(measured(r, QStringLiteral("shelf_depth_2"))), name(QStringLiteral("deleted")), names(r));
        r.check(!shown(r, QStringLiteral("measurementRow_shelf_depth_2")), name(QStringLiteral("its row goes")));
        r.app().undo();
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(std::abs(measured(r, QStringLiteral("shelf_depth_2")) - 12.5) < 1e-12 && shown(r, QStringLiteral("measurementRow_shelf_depth_2")),
                name(QStringLiteral("undo brings it back")), names(r));
        r.app().undo();
    });
    wait(steps, 2);
    steps.push_back([&r, name] {
        r.check(std::abs(measured(r, QStringLiteral("Wall")) - 2.5) < 1e-12, name(QStringLiteral("undo again: Wall 2.5")), QString::number(measured(r, QStringLiteral("Wall"))));
        r.app().redo();
        r.app().redo();
    });
    wait(steps, 2);
    steps.push_back([&r, s, name, shot] {
        r.check(std::abs(measured(r, QStringLiteral("Wall")) - 3) < 1e-12 && std::isnan(measured(r, QStringLiteral("shelf_depth_2"))),
                name(QStringLiteral("redo: Wall 3, shelf_depth_2 deleted")), names(r));
        r.screenshot(shot("list"));
        // ---- Saved and reopened.
        s->project = QDir(r.outputDir()).filePath(QStringLiteral("projectmeasurements.openshape"));
        QFile::remove(s->project);
        r.check(r.app().saveProjectAs(QUrl::fromLocalFile(s->project)), name(QStringLiteral("save")));
        r.app().newDocument();
        r.check(r.app().projectMeasurements().isEmpty(), name(QStringLiteral("a new project has none")));
        r.check(r.app().openProject(QUrl::fromLocalFile(s->project)) && r.waitForOpen(), name(QStringLiteral("reopen")));
    });
    wait(steps, 3);
    steps.push_back([&r, c, name] {
        r.check(names(r) == QStringLiteral("Depth, corner_angle, Wall") && std::abs(measured(r, QStringLiteral("Depth")) - 30) < 1e-12
                    && std::abs(measured(r, QStringLiteral("corner_angle")) - 60) < 1e-9 && std::abs(measured(r, QStringLiteral("Wall")) - 3) < 1e-12,
                name(QStringLiteral("the list survives save and reopen")), names(r));
        QString note;
        for (const QVariant& m : r.app().projectMeasurements())
            if (m.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("corner_angle"))
                note = m.toMap().value(QStringLiteral("note")).toString();
        r.check(note == QStringLiteral("Fold"), name(QStringLiteral("with its notes")), note);
        r.check(std::abs(bodyHeight(r) - 28) < 1e-6, name(QStringLiteral("and the model")), AcceptanceRunner::num(bodyHeight(r)));
        r.app().setTouchMode(c.touch);
    });
    openPanel(steps, r, c);
    steps.push_back([&r, name, shot] {
        r.check(shown(r, QStringLiteral("measurementRow_Depth")) && shown(r, QStringLiteral("measurementFootnote")),
                name(QStringLiteral("the reopened panel lists them, with the footnote")));
        r.screenshot(shot("reopened"));
    });
    closePanel(steps, r, c);
}

Steps steps(AcceptanceRunner& r)
{
    Steps out;
    auto state = std::make_shared<State>();
    addConfig(out, r, Config{QStringLiteral("desktop"), 0, 0, QVariant(), false}, state);
    addConfig(out, r, Config{QStringLiteral("iphone"), 402, 874, QVariantList{62, 0, 34, 0}, true}, state);
    addConfig(out, r, Config{QStringLiteral("ipad"), 1180, 820, QVariant(), true}, state);
    out.push_back([&r, state] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.resizeWindow(r.initialWindowSize().width(), r.initialWindowSize().height());
        r.app().setTouchMode(false);
        r.app().setDisplayUnit(QStringLiteral("mm"));
        QFile::remove(state->project);
    });
    return out;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("projectmeasurements"), 99, steps});

} // namespace
} // namespace os::app
