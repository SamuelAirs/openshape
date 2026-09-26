// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The compact layout of a phone-sized window (iPhone 16 Pro: 402x874 points
// in portrait, 874x402 in landscape, with the Dynamic Island's safe-area
// margins), switched live by resizing the window: the tool strip along the
// bottom, the Model button's sliding panel, the View menu, the sketch's
// strip; a box made and pushed there by touch; Undo / Redo saying what they
// did (touch has no tooltip); a body's row tapped twice (selects it, then
// folds the row and keeps it selected). The window goes back to the
// run's size at the end (and the runner's reset restores it too).

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QVariantList>
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

// An item's rectangle in window coordinates (empty if missing).
QRectF sceneRect(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item)
        return {};
    return item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
}

QString rectText(const QRectF& rect)
{
    return QStringLiteral("%1,%2 %3x%4").arg(rect.x(), 0, 'f', 0).arg(rect.y(), 0, 'f', 0).arg(rect.width(), 0, 'f', 0).arg(rect.height(), 0, 'f', 0);
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

// Inside the window minus the (simulated) safe-area insets.
bool insideSafeArea(AcceptanceRunner& r, const QRectF& rect, double top, double right, double bottom, double left)
{
    const double w = r.window()->width();
    const double h = r.window()->height();
    return !rect.isEmpty() && rect.left() >= left - 0.5 && rect.top() >= top - 0.5 && rect.right() <= w - right + 0.5
        && rect.bottom() <= h - bottom + 0.5;
}

void setSafeArea(AcceptanceRunner& r, double top, double right, double bottom, double left)
{
    r.window()->setProperty("simulatedSafeArea", QVariantList{top, right, bottom, left});
}

double bodyHeight(AcceptanceRunner& r)
{
    return r.app().bodyCount() == 0 ? -1.0 : geom::boundingBox(r.body(0).shape()).size().z;
}

// A tap with one finger (the touch layout stays on; a mouse click in the
// view would switch back to the mouse layout).
void tap(AcceptanceRunner& r, QPointF p)
{
    r.touchTap({p});
}

// The panels of the compact layout: the tool strip at the bottom, the hint
// and the value chip above it, the top bar and the Model / View buttons at
// the top, all inside the safe area; the regular view buttons hidden.
void checkCompactLayout(AcceptanceRunner& r, const QString& where, double top, double right, double bottom, double left)
{
    const QRectF strip = sceneRect(r, QStringLiteral("createPanel"));
    const QRectF bar = sceneRect(r, QStringLiteral("topBar"));
    const QRectF view = sceneRect(r, QStringLiteral("viewButtonPanel"));
    const QRectF status = sceneRect(r, QStringLiteral("statusColumn"));
    const double h = r.window()->height();
    r.check(shown(r, QStringLiteral("createPanel")) && strip.top() > h / 2 && strip.height() < 70,
            where + QStringLiteral(": the tools are a strip along the bottom"), rectText(strip));
    r.check(insideSafeArea(r, strip, top, right, bottom, left), where + QStringLiteral(": the strip stays above the home indicator"),
            rectText(strip));
    r.check(insideSafeArea(r, bar, top, right, bottom, left) && bar.top() < h / 3,
            where + QStringLiteral(": the top bar stays clear of the Dynamic Island"), rectText(bar));
    r.check(shown(r, QStringLiteral("viewButtonPanel")) && insideSafeArea(r, view, top, right, bottom, left),
            where + QStringLiteral(": a View button instead of the view buttons"), rectText(view));
    r.check(!shown(r, QStringLiteral("viewPanel")), where + QStringLiteral(": the view buttons are folded into the View menu"));
    r.check(!view.intersects(bar), where + QStringLiteral(": the View button does not cover the top bar"));
    r.check(status.bottom() <= strip.top() + 0.5 && status.left() >= left, where + QStringLiteral(": the hint sits above the strip"),
            rectText(status));
}

// The message toast shows `text` and nothing covers it: where the value chip
// sits at the toast's resting place, the toast moves above the chip (or,
// with no room there, is drawn over it).
void checkToast(AcceptanceRunner& r, const QString& text, const QString& what)
{
    QQuickItem* toast = r.findItem(QStringLiteral("toast"));
    QQuickItem* chip = r.findItem(QStringLiteral("valueChip"));
    const QString shownText = toast ? toast->property("text").toString() : QString();
    r.check(toast && toast->isVisible() && shownText == text, what + QStringLiteral(" says what it did"), shownText);
    const QRectF toastRect = sceneRect(r, QStringLiteral("toast"));
    const QRectF chipRect = sceneRect(r, QStringLiteral("valueChip"));
    const bool room = chipRect.top() - 8 - toastRect.height() >= sceneRect(r, QStringLiteral("topBar")).bottom() + 8;
    r.check(toast && chip && chip->isVisible() && (!toastRect.intersects(chipRect) || (!room && toast->z() > chip->z())),
            what + QStringLiteral(": the message is clear of the value chip"),
            rectText(toastRect) + QStringLiteral(" / ") + rectText(chipRect));
}

struct CompactState {
    double height = 0;
    QString label; // the step Undo / Redo names
};

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<CompactState>();
    Steps steps;
    // ---- iPhone 16 Pro in portrait: 402x874 with the Dynamic Island's margins.
    steps.push_back([&r] {
        r.app().setTouchMode(true);
        r.resizeWindow(402, 874);
        setSafeArea(r, 62, 0, 34, 0);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        const QSize size = r.window()->size();
        r.check(size.width() == 402, "compact: the window is phone-sized",
                QStringLiteral("%1x%2").arg(size.width()).arg(size.height()));
        checkCompactLayout(r, QStringLiteral("portrait"), 62, 0, 34, 0);
        r.screenshot(QStringLiteral("compact_01_portrait"));
        // Box from the strip.
        r.check(r.clickItem(QStringLiteral("tool_box")), "compact: Box in the tool strip");
    });
    wait(steps, 4); // the fit animation
    steps.push_back([&r, s] {
        r.check(r.app().bodyCount() == 1, "compact: the strip's Box adds a body");
        s->height = bodyHeight(r);
        r.check(std::abs(s->height - 20.0) < 1e-9, "compact: a 20 mm cube", AcceptanceRunner::num(s->height));
        // Tap its top face: push/pull with the value chip.
        tap(r, r.screenPoint(0, 0, 20));
    });
    steps.push_back([&r] {
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Push/Pull"),
                "compact: tapping the top face offers push/pull", r.app().operationTitle());
        r.check(r.app().touchMode(), "compact: the tap keeps the touch layout");
        const QRectF chip = sceneRect(r, QStringLiteral("valueChip"));
        const QRectF status = sceneRect(r, QStringLiteral("statusColumn"));
        r.check(shown(r, QStringLiteral("valueChip")) && insideSafeArea(r, chip, 62, 0, 34, 0),
                "compact: the value chip is on screen", rectText(chip));
        r.check(chip.bottom() <= status.top() + 0.5, "compact: nothing below covers the value chip",
                rectText(chip) + QStringLiteral(" / ") + rectText(status));
        r.screenshot(QStringLiteral("compact_02_pushpull"));
        // The hint is one short line; a tap on it shows all of it.
        QQuickItem* hint = r.findItem(QStringLiteral("hintText"));
        r.check(hint && hint->height() < 20, "compact: the hint is one line", hint ? AcceptanceRunner::num(hint->height()) : QString());
        r.check(r.clickItem(QStringLiteral("hintMore")), "compact: tap the hint");
    });
    steps.push_back([&r] {
        QQuickItem* hint = r.findItem(QStringLiteral("hintText"));
        r.check(hint && hint->property("expanded").toBool() && hint->height() > 20, "compact: the whole hint shows",
                hint ? AcceptanceRunner::num(hint->height()) : QString());
        const QString text = hint ? hint->property("text").toString() : QString();
        r.check(text.contains(QStringLiteral("✓")) && !text.contains(QStringLiteral("Enter")),
                "compact: the touch hint speaks of the check mark, not Enter", text);
        r.check(r.clickItem(QStringLiteral("hintMore")), "compact: tap the hint again");
        r.check(hint && !hint->property("expanded").toBool(), "compact: back to one line");
        r.type(QStringLiteral("30"));
    });
    steps.push_back([&r] {
        r.key(Qt::Key_Return);
        const double height = bodyHeight(r);
        r.check(std::abs(height - 30.0) < 1e-9, "compact: typing 30 and Enter pushes the top to 30 mm", AcceptanceRunner::num(height));
        // A tool at the far end of the strip: it scrolls to it.
        r.check(r.clickItem(QStringLiteral("tool_intersect")), "compact: Intersect at the end of the strip");
        QQuickItem* scroll = r.findItem(QStringLiteral("createScroll"));
        r.check(scroll && scroll->property("contentX").toDouble() > 10, "compact: the strip scrolls sideways",
                scroll ? AcceptanceRunner::num(scroll->property("contentX").toDouble()) : QString());
    });
    // ---- The Model panel slides in behind the Model button.
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("modelPanelButton")), "compact: a Model button once there are steps");
        r.check(!shown(r, QStringLiteral("historyPanel")), "compact: the Model panel starts closed");
        r.check(r.clickItem(QStringLiteral("modelPanelButton")), "compact: Model button");
    });
    wait(steps, 3); // the slide
    steps.push_back([&r] {
        const QRectF panel = sceneRect(r, QStringLiteral("historyPanel"));
        const QRectF strip = sceneRect(r, QStringLiteral("createPanel"));
        r.check(shown(r, QStringLiteral("historyPanel")) && insideSafeArea(r, panel, 62, 0, 34, 0),
                "compact: the Model panel slides in, on screen", rectText(panel));
        r.check(panel.bottom() <= strip.top(), "compact: the Model panel ends above the strip", rectText(panel));
        r.check(!shown(r, QStringLiteral("valueChip")), "compact: the value chip waits while the Model panel is open");
        r.screenshot(QStringLiteral("compact_03_model_panel"));
        r.check(r.clickItem(QStringLiteral("historyPanelHide")), "compact: the Model panel's Close");
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("historyPanel")), "compact: Close slides it away");
        r.check(shown(r, QStringLiteral("valueChip")), "compact: the value chip is back");
        // ---- The View menu.
        r.check(r.clickItem(QStringLiteral("viewMenuButton")), "compact: View button");
    });
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("viewPanel")), "compact: the View menu opens");
        const QRectF menu = sceneRect(r, QStringLiteral("viewPanel"));
        r.check(insideSafeArea(r, menu, 62, 0, 34, 0), "compact: the View menu fits on screen", rectText(menu));
        r.screenshot(QStringLiteral("compact_04_view_menu"));
        r.check(r.clickItem(QStringLiteral("viewTop")), "compact: Top in the View menu");
        r.check(!shown(r, QStringLiteral("viewPanel")), "compact: choosing a view closes the menu");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        const Vec3 b = r.app().interaction().camera().backward();
        r.check(b.z > 0.999, "compact: the view looks straight down", AcceptanceRunner::num(b.z));
        r.check(r.clickItem(QStringLiteral("viewMenuButton")), "compact: View button again");
    });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("viewIso")), "compact: Iso in the View menu");
    });
    wait(steps, 4);
    // ---- Undo / Redo: touch has no tooltip, so a message says what they did,
    // readable even with the value chip where the message rests: the model
    // panned down by two fingers puts the arrow tip, and so the chip, there.
    steps.push_back([&r] {
        r.app().interaction().twoFingerPan({200, 300}, {200, 750});
    });
    steps.push_back([&r, s] {
        const QRectF chip = sceneRect(r, QStringLiteral("valueChip"));
        const QRectF status = sceneRect(r, QStringLiteral("statusColumn"));
        r.check(shown(r, QStringLiteral("valueChip")) && chip.bottom() > status.top() - 20,
                "compact: panned down, the value chip sits just above the hint", rectText(chip));
        s->label = r.app().undoText();
        r.check(r.app().canUndo() && !s->label.isEmpty(), "compact: something to undo", s->label);
        r.check(r.clickItem(QStringLiteral("undoButton")), "compact: Undo button");
    });
    steps.push_back([&r, s] {
        const double height = bodyHeight(r);
        r.check(std::abs(height - 20.0) < 1e-9, "compact: Undo takes the top back to 20 mm", AcceptanceRunner::num(height));
        checkToast(r, QStringLiteral("Undo ") + s->label, QStringLiteral("compact: Undo"));
        s->label = r.app().redoText();
        r.check(r.clickItem(QStringLiteral("redoButton")), "compact: Redo button");
    });
    steps.push_back([&r, s] {
        const double height = bodyHeight(r);
        r.check(std::abs(height - 30.0) < 1e-9, "compact: Redo pushes the top to 30 mm again", AcceptanceRunner::num(height));
        checkToast(r, QStringLiteral("Redo ") + s->label, QStringLiteral("compact: Redo"));
        r.app().interaction().twoFingerPan({200, 750}, {200, 300}); // back
        // ---- A tap on the body's row selects it; a second tap folds the row
        // and keeps the body selected.
        r.check(r.clickItem(QStringLiteral("modelPanelButton")), "compact: Model button again");
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("historyRow_") + QString::fromStdString(r.body(0).id().toString())),
                "compact: tap the body's row");
    });
    steps.push_back([&r] {
        const auto& selection = r.app().interaction().selection();
        const QString id = QString::fromStdString(r.body(0).id().toString());
        QQuickItem* panel = r.findItem(QStringLiteral("historyPanel"));
        r.check(selection.size() == 1 && selection.allOfKind(sel::SelectionKind::Body) && selection.items()[0].bodyId == r.body(0).id(),
                "compact: the tap selects the body", QString::number(selection.size()));
        r.check(panel && panel->property("expandedId").toString() == id, "compact: the row unfolds");
    });
    wait(steps, 4); // a second tap this soon would be a double-tap
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("historyRow_") + QString::fromStdString(r.body(0).id().toString())),
                "compact: tap the row again");
    });
    steps.push_back([&r] {
        const auto& selection = r.app().interaction().selection();
        QQuickItem* panel = r.findItem(QStringLiteral("historyPanel"));
        r.check(selection.size() == 1 && selection.allOfKind(sel::SelectionKind::Body) && selection.items()[0].bodyId == r.body(0).id(),
                "compact: tapping the row again keeps the body selected", QString::number(selection.size()));
        r.check(panel && panel->property("expandedId").toString().isEmpty(), "compact: the row folds");
        r.check(r.clickItem(QStringLiteral("historyPanelHide")), "compact: the Model panel's Close again");
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.key(Qt::Key_Escape);
    });
    steps.push_back([&r] {
        r.check(r.app().interaction().selection().empty(), "compact: Esc clears the selection");
    });
    // ---- Turn the phone: 874x402, the island on the side. The layout follows live.
    steps.push_back([&r] {
        r.resizeWindow(874, 402);
        setSafeArea(r, 0, 62, 21, 62);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.window()->width() == 874 && r.window()->height() <= 402, "compact: the window turned to landscape",
                QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
        checkCompactLayout(r, QStringLiteral("landscape"), 0, 62, 21, 62);
        r.screenshot(QStringLiteral("compact_05_landscape"));
        r.check(r.clickItem(QStringLiteral("viewMenuButton")), "landscape: View button");
    });
    steps.push_back([&r] {
        const QRectF menu = sceneRect(r, QStringLiteral("viewPanel"));
        r.check(insideSafeArea(r, menu, 0, 62, 21, 62), "landscape: the View menu fits (in columns)", rectText(menu));
        r.check(r.clickItem(QStringLiteral("viewFit")), "landscape: Fit in the View menu");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.key(Qt::Key_Escape); // the pushed face is still selected; a tap would add to it
    });
    steps.push_back([&r, s] {
        s->height = bodyHeight(r);
        r.check(r.app().interaction().selection().empty(), "landscape: nothing selected");
        tap(r, r.screenPoint(0, 0, s->height));
    });
    steps.push_back([&r] {
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Push/Pull"),
                "landscape: tapping the top face offers push/pull", r.app().operationTitle());
        const QRectF chip = sceneRect(r, QStringLiteral("valueChip"));
        const QRectF status = sceneRect(r, QStringLiteral("statusColumn"));
        r.check(insideSafeArea(r, chip, 0, 62, 21, 62) && chip.bottom() <= status.top() + 0.5,
                "landscape: the value chip is on screen, above the hint", rectText(chip) + QStringLiteral(" / ") + rectText(status));
        r.type(QStringLiteral("40"));
    });
    steps.push_back([&r] {
        r.key(Qt::Key_Return);
        const double height = bodyHeight(r);
        r.check(std::abs(height - 40.0) < 1e-9, "landscape: the top pushed to 40 mm", AcceptanceRunner::num(height));
        r.check(std::abs(geom::volume(r.body(0).shape()) - 16000.0) < 1e-6, "landscape: volume 20 x 20 x 40 = 16000 mm^3",
                AcceptanceRunner::num(geom::volume(r.body(0).shape())));
        r.key(Qt::Key_Escape);
        // ---- A sketch: its tools are a strip too.
        r.check(r.clickItem(QStringLiteral("sketchButton")), "landscape: Sketch in the strip");
    });
    steps.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("planeFront")), "landscape: the plane menu opens above the strip");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(r.app().sketchMode(), "landscape: sketching");
        const QRectF strip = sceneRect(r, QStringLiteral("sketchToolPanel"));
        const QRectF toolbar = sceneRect(r, QStringLiteral("sketchToolbar"));
        r.check(strip.top() > r.window()->height() / 2.0 && insideSafeArea(r, strip, 0, 62, 21, 62),
                "landscape: the sketch tools are a strip along the bottom", rectText(strip));
        r.check(insideSafeArea(r, toolbar, 0, 62, 21, 62) && !toolbar.intersects(sceneRect(r, QStringLiteral("topBar"))),
                "landscape: Finish sketch clear of the top bar", rectText(toolbar));
        r.check(r.clickItem(QStringLiteral("tool_trim")), "landscape: Trim at the end of the sketch strip");
        r.check(r.app().sketchTool() == QStringLiteral("trim"), "landscape: the Trim tool", r.app().sketchTool());
        r.screenshot(QStringLiteral("compact_06_sketch"));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), "landscape: Finish sketch");
    });
    wait(steps, 4);
    // ---- Back to the run's window: the regular layout returns.
    steps.push_back([&r] {
        r.check(!r.app().sketchMode(), "compact: the sketch is finished");
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.window()->size() == r.initialWindowSize(), "compact: the window is back to its size",
                QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
        r.check(shown(r, QStringLiteral("viewPanel")) && !shown(r, QStringLiteral("viewButtonPanel")),
                "regular again: the view buttons are back");
        const QRectF palette = sceneRect(r, QStringLiteral("createPanel"));
        r.check(palette.height() > 200 && palette.left() < r.window()->width() / 4.0, "regular again: the tool column on the left",
                rectText(palette));
        r.check(shown(r, QStringLiteral("historyPanel")), "regular again: the Model panel is always shown");
        r.screenshot(QStringLiteral("compact_07_regular_again"));
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("compact"), 90, steps});

} // namespace
} // namespace os::app
