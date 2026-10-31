// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The iPhone Duo, folded and unfolded mid-task (docs/IPAD.md, "iPhone Duo").
// The same window is resized live between the Duo's screens, with their
// (estimated) safe areas: the outer display (466x678 points; 678x466 held
// sideways), the inner display in the book orientation (951x669) with its
// side bar of status icons on the right or on the left, the inner display
// turned (669x951, the bar along the top) and a Split View half (440x669,
// the bar at its side). Five tasks are carried through every change: (a) a
// sketch with the Rectangle tool drawing and its width typed (not finished),
// (b) a push/pull with a typed value not yet applied, (c) the keypad open
// for the value box, (d) the Model panel open with a step unfolded, (e)
// Home with projects. After each change nothing is lost (the tool, the
// values, the selection, the open panel), every control shown lies inside
// the safe area, the value box keeps off the selection, the layout is the
// compact one exactly where the window is phone-sized, and the log's window
// line (ui/WindowMetricsLog) tells the new size and insets. Each task ends
// by applying what was typed, measured on the model.

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/SketchSession.h"
#include "ui/AppController.h"
#include "ui/WindowMetricsLog.h"

#include <QtCore/QDir>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QVariantList>
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

struct Screen {
    QString name;
    int width = 0;
    int height = 0;
    double safe[4] = {0, 0, 0, 0}; // top, right, bottom, left
    bool compact() const { return width < 600 || height < 500; }
};

// The Duo's screens (docs/IPAD.md; the insets are estimates until the
// owner's log tells the real ones).
const Screen kOuter{QStringLiteral("outer"), 466, 678, {59, 0, 34, 0}};
const Screen kOuterSideways{QStringLiteral("outer_sideways"), 678, 466, {0, 59, 21, 59}};
const Screen kInnerBarRight{QStringLiteral("inner_bar_right"), 951, 669, {0, 84, 20, 0}};
const Screen kInnerBarLeft{QStringLiteral("inner_bar_left"), 951, 669, {0, 0, 20, 84}};
const Screen kInnerTurned{QStringLiteral("inner_turned"), 669, 951, {84, 0, 20, 0}};
const Screen kSplitHalf{QStringLiteral("split_half"), 440, 669, {0, 84, 20, 0}};

// Unfold, the bar to the other side, Split View, turned, folded sideways, folded.
const std::vector<Screen> kFolds{kInnerBarRight, kInnerBarLeft, kSplitHalf, kInnerTurned, kOuterSideways, kOuter};

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

QRectF safeRect(AcceptanceRunner& r, const Screen& s)
{
    return QRectF(s.safe[3], s.safe[0], r.window()->width() - s.safe[3] - s.safe[1], r.window()->height() - s.safe[0] - s.safe[2]);
}

bool tapItem(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    if (!item || !item->isVisible())
        return false;
    r.touchTap({item->mapToScene(QPointF(item->width() / 2, item->height() / 2))});
    return true;
}

bool tapKeys(AcceptanceRunner& r, const QString& keys)
{
    bool all = true;
    for (const QChar c : keys)
        all = tapItem(r, QStringLiteral("keypadKey_") + c) && all;
    return all;
}

bool shown(AcceptanceRunner& r, const QString& name)
{
    QQuickItem* item = r.findItem(name);
    return item && item->isVisible();
}

double bodyHeight(AcceptanceRunner& r)
{
    return r.app().bodyCount() == 0 ? -1.0 : geom::boundingBox(r.body(0).shape()).size().z;
}

void applyScreen(AcceptanceRunner& r, const Screen& s)
{
    r.resizeWindow(s.width, s.height);
    r.window()->setProperty("simulatedSafeArea", QVariantList{s.safe[0], s.safe[1], s.safe[2], s.safe[3]});
}

// After a change of screen: the window has its size, the layout is compact
// exactly on a phone-sized window, every control shown is inside the safe
// area, the value box keeps off the selection, and the log says so.
void checkScreen(AcceptanceRunner& r, const Screen& s, const QString& task)
{
    const QString where = QStringLiteral("duo ") + task + QStringLiteral(", ") + s.name;
    r.check(r.window()->width() == s.width && r.window()->height() == s.height, where + QStringLiteral(": the window's size"),
            QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
    r.check(r.window()->property("compactLayout").toBool() == s.compact(),
            where + (s.compact() ? QStringLiteral(": the compact layout") : QStringLiteral(": the regular layout")));
    // Every control over the view (and the ones a task opens).
    const QRectF safe = safeRect(r, s).adjusted(-0.5, -0.5, 0.5, 0.5);
    QStringList outside;
    int seen = 0;
    for (const char* name : {"topBar", "createPanel", "modelButtonPanel", "viewButtonPanel", "axisTriad", "viewPanel", "statusColumn",
                             "historyPanel", "valueChip", "numericKeypad", "sketchToolbar", "sketchToolPanel", "sketchStatus",
                             "sketchActions", "sketchCounter", "finishSketchButton"}) {
        QQuickItem* item = r.findItem(QString::fromLatin1(name));
        if (!item || !item->isVisible() || item->opacity() <= 0)
            continue;
        const QRectF rect = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        if (rect.isEmpty() || rect.left() >= r.window()->width()) // the compact Model panel, slid away
            continue;
        ++seen;
        if (!safe.contains(rect))
            outside << QString::fromLatin1(name) + QLatin1Char(' ') + rectText(rect);
    }
    r.check(seen > 0 && outside.isEmpty(), where + QStringLiteral(": every control is inside the safe area"),
            outside.isEmpty() ? QStringLiteral("%1 controls").arg(seen) : outside.join(QStringLiteral("; ")));
    // The value box never covers the selection, its arrow or the tap.
    if (shown(r, QStringLiteral("valueChip"))) {
        const QRectF chip = sceneRect(r, QStringLiteral("valueChip"));
        const auto keep = r.app().interaction().keepClearRect();
        const QRectF k = keep ? QRectF(QPointF(keep->left, keep->top), QPointF(keep->right, keep->bottom)) : QRectF();
        r.check(!keep || !overlaps(chip, k), where + QStringLiteral(": the value box keeps off the selection"),
                rectText(chip) + QStringLiteral(" / ") + rectText(k));
    }
    // The device test's log line names the new size and insets.
    auto* log = r.window()->findChild<ui::WindowMetricsLog*>();
    if (log)
        log->flush();
    const QString line = log ? log->lastLine() : QString();
    const QString expected = QStringLiteral("window %1x%2 pt").arg(s.width).arg(s.height);
    const QString insets = QStringLiteral("simulated safe area top %1 right %2 bottom %3 left %4")
                               .arg(s.safe[0])
                               .arg(s.safe[1])
                               .arg(s.safe[2])
                               .arg(s.safe[3]);
    r.check(line.startsWith(expected) && line.contains(insets) && line.contains(s.compact() ? QStringLiteral("compact layout")
                                                                                               : QStringLiteral("regular layout")),
            where + QStringLiteral(": the log tells the window's size and safe area"), line);
}

// Resizes to each of kFolds in turn; after each, the common checks, the
// task's own (`same`) and a picture.
void foldThrough(Steps& steps, AcceptanceRunner& r, const QString& task, const std::function<void(const Screen&, const QString&)>& same)
{
    for (const Screen& s : kFolds) {
        steps.push_back([&r, s] { applyScreen(r, s); });
        wait(steps, 3); // the layout settles; the compact Model panel slides
        steps.push_back([&r, s, task, same] {
            checkScreen(r, s, task);
            same(s, QStringLiteral("duo ") + task + QStringLiteral(", ") + s.name);
            r.screenshot(QStringLiteral("duo_") + task + QStringLiteral("_") + s.name);
        });
    }
}

struct DuoState {
    std::unique_ptr<QTemporaryDir> folder;
    QString boxRow;
};

Steps steps(AcceptanceRunner& r)
{
    auto st = std::make_shared<DuoState>();
    Steps steps;
    // ---- (a) A sketch: the Rectangle tool drawing, its width typed.
    steps.push_back([&r] {
        r.app().setTouchMode(true);
        applyScreen(r, kOuter);
    });
    wait(steps, 3);
    steps.push_back([&r] {
        checkScreen(r, kOuter, QStringLiteral("start"));
        r.screenshot(QStringLiteral("duo_start_outer"));
        r.check(tapItem(r, QStringLiteral("emptyStartSketch")), "duo sketch: tap Start a sketch");
    });
    wait(steps, 5);
    steps.push_back([&r] {
        r.check(r.app().sketchMode(), "duo sketch: sketching");
        r.check(tapItem(r, QStringLiteral("tool_rectangle")), "duo sketch: the Rectangle tool");
    });
    steps.push_back([&r] { r.touchTap({r.screenPoint(-10, -5, 0)}); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().sketchDrawing(), "duo sketch: a tap starts the rectangle");
        r.type(QStringLiteral("40")); // its width, on a keyboard case
    });
    wait(steps, 6); // the typing pause: the width is taken, the rectangle not finished
    foldThrough(steps, r, QStringLiteral("sketch"), [&r](const Screen&, const QString& where) {
        r.check(r.app().sketchMode() && r.app().sketchTool() == QStringLiteral("rectangle") && r.app().sketchDrawing(),
                where + QStringLiteral(": still drawing the rectangle"), r.app().sketchTool());
        r.check(shown(r, QStringLiteral("sketchToolPanel")) && shown(r, QStringLiteral("finishSketchButton")),
                where + QStringLiteral(": the sketch's tools and Finish sketch are there"));
    });
    steps.push_back([&r] { r.key(Qt::Key_Tab); }); // on to the height
    wait(steps, 1);
    steps.push_back([&r] { r.type(QStringLiteral("25")); });
    wait(steps, 6); // the typing pause
    steps.push_back([&r] { r.key(Qt::Key_Return); });
    wait(steps, 2);
    steps.push_back([&r] {
        // The rectangle's corners: not the sketch's origin point, which
        // widens the extent when the rectangle lies away from it (drawn
        // down from the corner tapped when the window, resizing under a
        // resting mouse, hovers it there).
        double left = 1e9, right = -1e9, bottom = 1e9, top = -1e9;
        if (const auto* session = r.app().interaction().sketchSession())
            for (const auto& [id, p] : session->sketch().points()) {
                if (id == sketch::kOriginId)
                    continue;
                left = std::min(left, p.position.x);
                right = std::max(right, p.position.x);
                bottom = std::min(bottom, p.position.y);
                top = std::max(top, p.position.y);
            }
        r.check(std::abs(right - left - 40) < 1e-6 && std::abs(top - bottom - 25) < 1e-6,
                "duo sketch: a 40 x 25 rectangle after folding and unfolding six times",
                AcceptanceRunner::num(right - left) + QStringLiteral(" x ") + AcceptanceRunner::num(top - bottom));
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), "duo sketch: Finish sketch");
    });
    wait(steps, 3);
    // ---- (b) A push/pull with a typed value not applied yet.
    steps.push_back([&r] {
        r.check(!r.app().sketchMode(), "duo push/pull: the sketch is finished");
        r.app().newDocument();
        r.app().setTouchMode(true);
    });
    wait(steps, 2);
    steps.push_back([&r] { r.check(tapItem(r, QStringLiteral("emptyAddBox")), "duo push/pull: tap Add a box"); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().bodyCount() == 1, "duo push/pull: a box");
        r.app().setTouchMode(true);
        r.app().fitAll();
    });
    wait(steps, 4);
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 20)}); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "duo push/pull: the top face offers push/pull", r.app().operationTitle());
        r.type(QStringLiteral("35")); // a keyboard case's keys
    });
    wait(steps, 2);
    foldThrough(steps, r, QStringLiteral("pushpull"), [&r](const Screen&, const QString& where) {
        const auto& selection = r.app().interaction().selection();
        QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Push/Pull") && selection.size() == 1
                    && selection.items()[0].kind == sel::SelectionKind::Face,
                where + QStringLiteral(": the top face is still being pushed"), r.app().operationTitle());
        r.check(field && field->isVisible() && field->property("text").toString() == QStringLiteral("35"),
                where + QStringLiteral(": the value typed is still there, 35"), field ? field->property("text").toString() : QString());
        r.check(std::abs(bodyHeight(r) - 20) < 1e-9, where + QStringLiteral(": not applied yet"), AcceptanceRunner::num(bodyHeight(r)));
    });
    steps.push_back([&r] { r.key(Qt::Key_Return); });
    steps.push_back([&r] {
        r.check(std::abs(bodyHeight(r) - 35) < 1e-9, "duo push/pull: Enter makes it 35 mm high", AcceptanceRunner::num(bodyHeight(r)));
        r.app().fitAll();
    });
    wait(steps, 4);
    // ---- (c) The keypad open for the value box.
    steps.push_back([&r] { r.touchTap({r.screenPoint(0, 0, 35)}); });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "duo keypad: the top face again", r.app().operationTitle());
        r.check(tapItem(r, QStringLiteral("valueChipField")), "duo keypad: tap the value");
    });
    wait(steps, 2);
    steps.push_back([&r] {
        r.check(shown(r, QStringLiteral("numericKeypad")), "duo keypad: the keypad comes up");
        r.check(tapKeys(r, QStringLiteral("42")), "duo keypad: tap 4, 2");
    });
    wait(steps, 2);
    foldThrough(steps, r, QStringLiteral("keypad"), [&r](const Screen& s, const QString& where) {
        QQuickItem* field = r.findItem(QStringLiteral("valueChipField"));
        const QRectF pad = sceneRect(r, QStringLiteral("numericKeypad"));
        const QRectF chip = sceneRect(r, QStringLiteral("valueChip"));
        r.check(!pad.isEmpty() && field && field->property("text").toString() == QStringLiteral("42"),
                where + QStringLiteral(": the keypad is still up, 42 typed"), field ? field->property("text").toString() : QString());
        r.check(!chip.isEmpty() && !overlaps(pad, chip), where + QStringLiteral(": the keypad leaves the value box clear"),
                rectText(pad) + QStringLiteral(" / ") + rectText(chip));
        // The face being pushed stays in sight, off the keypad.
        const auto keep = r.app().interaction().keepClearRect();
        const QRectF k = keep ? QRectF(QPointF(keep->left, keep->top), QPointF(keep->right, keep->bottom)) : QRectF();
        r.check(keep && k.width() * k.height() > 400 && !overlaps(pad, k), where + QStringLiteral(": the face and its arrow are in sight, off the keypad"),
                rectText(k) + QStringLiteral(" / ") + rectText(pad));
        double smallest = 1e9;
        for (const char* id : {"0", "1", "5", "9", "back", "done"}) {
            const QRectF key = sceneRect(r, QStringLiteral("keypadKey_") + QString::fromLatin1(id));
            if (!key.isEmpty())
                smallest = std::min({smallest, key.width(), key.height()});
        }
        r.check(smallest >= 44 && smallest < 1e8, where + QStringLiteral(": keys of 44 pt or more"), AcceptanceRunner::num(smallest));
        // A phone held upright docks it along the bottom; otherwise it is
        // no wider than half the window (the model keeps the rest).
        const double usable = s.width - s.safe[1] - s.safe[3];
        if (s.compact() && s.height >= s.width)
            r.check(pad.width() >= usable - 10, where + QStringLiteral(": docked along the bottom"), rectText(pad));
        else
            r.check(pad.width() <= usable - 120, where + QStringLiteral(": at its own size, the model keeps the rest"), rectText(pad));
    });
    steps.push_back([&r] { r.check(tapItem(r, QStringLiteral("keypadKey_done")), "duo keypad: tap the check mark"); });
    wait(steps, 1);
    steps.push_back([&r] {
        r.check(std::abs(bodyHeight(r) - 42) < 1e-9, "duo keypad: the check mark makes it 42 mm high", AcceptanceRunner::num(bodyHeight(r)));
        r.check(!shown(r, QStringLiteral("numericKeypad")), "duo keypad: the keypad goes");
        r.key(Qt::Key_Escape);
    });
    wait(steps, 1);
    // ---- (d) The Model panel open, the box's step unfolded.
    steps.push_back([&r, st] {
        r.check(tapItem(r, QStringLiteral("modelPanelButton")), "duo Model panel: tap Model");
        st->boxRow = QString::fromStdString(r.body(0).id().toString());
    });
    wait(steps, 3);
    steps.push_back([&r, st] { r.check(tapItem(r, QStringLiteral("historyRow_") + st->boxRow), "duo Model panel: tap the body's row"); });
    wait(steps, 2);
    foldThrough(steps, r, QStringLiteral("modelpanel"), [&r, st](const Screen&, const QString& where) {
        QQuickItem* panel = r.findItem(QStringLiteral("historyPanel"));
        const QRectF rect = sceneRect(r, QStringLiteral("historyPanel"));
        r.check(r.window()->property("historyOpen").toBool() && panel && panel->isVisible() && rect.right() <= r.window()->width(),
                where + QStringLiteral(": the Model panel is open"), rectText(rect));
        r.check(panel && panel->property("expandedId").toString() == st->boxRow, where + QStringLiteral(": the body's row stays unfolded"));
        const auto& selection = r.app().interaction().selection();
        r.check(selection.size() == 1 && selection.allOfKind(sel::SelectionKind::Body), where + QStringLiteral(": the body stays selected"),
                QString::number(selection.size()));
    });
    steps.push_back([&r] { r.check(tapItem(r, QStringLiteral("historyPanelHide")), "duo Model panel: Close"); });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(!shown(r, QStringLiteral("historyPanel")), "duo Model panel: Close slides it away");
        r.key(Qt::Key_Escape);
    });
    // ---- (e) Home, with projects.
    steps.push_back([&r, st] {
        st->folder = std::make_unique<QTemporaryDir>(QDir::tempPath() + QStringLiteral("/openshape-duo-XXXXXX"));
        r.check(st->folder->isValid(), "duo Home: a folder for projects");
        for (const char* name : {"Bracket", "Lid", "Hinge", "Knob", "Spacer"}) {
            r.app().newDocument();
            (void)r.app().createBox(20);
            r.check(r.app().saveProjectAs(QUrl::fromLocalFile(st->folder->filePath(QString::fromLatin1(name) + QStringLiteral(".openshape")))),
                    QStringLiteral("duo Home: save ") + QString::fromLatin1(name));
        }
        r.app().setHomeVisible(true);
    });
    wait(steps, 4);
    foldThrough(steps, r, QStringLiteral("home"), [&r](const Screen& s, const QString& where) {
        r.check(r.app().homeVisible() && shown(r, QStringLiteral("homeScreen")), where + QStringLiteral(": Home is shown"));
        const QRectF safe = safeRect(r, s).adjusted(-0.5, -0.5, 0.5, 0.5);
        QStringList outside;
        for (const char* name : {"homeNew", "homeOpen", "homeImport", "homeBack", "homeCard_0", "homeCard_1", "homeCardMenu_0"}) {
            const QRectF rect = sceneRect(r, QString::fromLatin1(name));
            if (!rect.isEmpty() && (rect.left() < safe.left() || rect.right() > safe.right() || rect.top() < safe.top()))
                outside << QString::fromLatin1(name) + QLatin1Char(' ') + rectText(rect);
        }
        r.check(outside.isEmpty(), where + QStringLiteral(": Home's buttons and cards are inside the safe area"), outside.join(QStringLiteral("; ")));
        const QRectF first = sceneRect(r, QStringLiteral("homeCard_0"));
        const QRectF second = sceneRect(r, QStringLiteral("homeCard_1"));
        r.check(!first.isEmpty() && !second.isEmpty() && std::abs(first.top() - second.top()) < 1,
                where + QStringLiteral(": the cards are side by side"), rectText(first) + QStringLiteral(" / ") + rectText(second));
    });
    steps.push_back([&r, st] {
        r.app().setHomeVisible(false);
        r.app().setAppFolder({});
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.window()->size() == r.initialWindowSize(), "duo: the window is back to its size");
        r.check(!r.window()->property("compactLayout").toBool(), "duo: the regular layout again");
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("duo"), 91, steps});

} // namespace
} // namespace os::app
