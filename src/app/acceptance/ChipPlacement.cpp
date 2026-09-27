// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The value chip never covers what was tapped (the owner's iPhone report:
// "the input dialogue often obscures what I've tapped"). On an iPhone in
// portrait (402x874, Dynamic Island) and in landscape, an iPad (1180x820,
// touch) and a desktop window (1400x900, mouse): an edge on the left side of
// a box and then a face are tapped (clicked), and the chip must stay clear of
// the selected edge or face as projected on screen, of the arrow and its tip,
// and of the controls, inside the safe area. On the phone the chip is docked
// and keeps its place while the arrow is dragged; its buttons are tapped
// (Chamfer, Fillet, the last action scrolled into sight, ✕, the field, ✓), and while
// the value is typed it docks below the top bar (the on-screen keyboard
// comes up from below). The window goes back to the run's size at the end
// (the runner's reset restores it too).

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/OverlayPlacement.h"
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

// Open intersection (touching does not count), also for zero-width rectangles
// (a vertical edge seen straight on).
bool overlaps(const QRectF& a, const QRectF& b)
{
    return a.left() < b.right() && b.left() < a.right() && a.top() < b.bottom() && b.top() < a.bottom();
}

// The screen rectangle of model points.
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

struct Config {
    QString name;
    int width = 0;
    int height = 0;
    bool touch = false;
    QVariant safeArea; // [top, right, bottom, left] or none
    double safe[4] = {0, 0, 0, 0};
};

struct State {
    QRectF chipBefore; // where the docked chip was when the drag started
    bool dragged = false;
    double volume = 0; // the box before a typed push/pull
};

// The compact layout: a phone.
bool phone(const Config& c) { return c.width < 600 || c.height < 500; }

// The chip covers none of the controls.
void checkControls(AcceptanceRunner& r, const Config& c, const QString& what, const QRectF& rect)
{
    for (const char* control : {"topBar", "modelButtonPanel", "viewButtonPanel", "statusColumn", "createPanel", "historyPanel", "viewPanel"}) {
        const QRectF other = sceneRect(r, QString::fromLatin1(control));
        r.check(other.isEmpty() || !overlaps(rect, other),
                c.name + QStringLiteral(": the chip does not cover the ") + QString::fromLatin1(control) + QStringLiteral(" (") + what + QStringLiteral(")"),
                rectText(rect) + QStringLiteral(" / ") + rectText(other));
    }
}

void select(AcceptanceRunner& r, const Config& c, QPointF p)
{
    if (c.touch)
        r.touchTap({p});
    else
        r.click(p);
}

// The chip is on screen inside the safe area, clear of the controls, of
// `selection` (the tapped edge or face on screen, with the margin) and of the
// arrow tip.
void checkChip(AcceptanceRunner& r, const Config& c, const QString& what, const QRectF& selection)
{
    QQuickItem* chip = r.findItem(QStringLiteral("valueChip"));
    const QRectF rect = sceneRect(r, QStringLiteral("valueChip"));
    r.check(chip && chip->isVisible() && !rect.isEmpty(), c.name + QStringLiteral(": the value chip shows for ") + what);
    const double w = r.window()->width();
    const double h = r.window()->height();
    const QRectF safe(c.safe[3], c.safe[0], w - c.safe[3] - c.safe[1], h - c.safe[0] - c.safe[2]);
    r.check(safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect), c.name + QStringLiteral(": the chip is inside the safe area (") + what + QStringLiteral(")"),
            rectText(rect));
    const double margin = interact::chipMargin(c.touch);
    r.check(!overlaps(rect, selection.adjusted(-margin + 1, -margin + 1, margin - 1, margin - 1)),
            c.name + QStringLiteral(": the chip leaves the ") + what + QStringLiteral(" clear, with a margin"),
            rectText(rect) + QStringLiteral(" / ") + rectText(selection));
    const QPointF tip = r.app().valueLabelPosition();
    r.check(!rect.adjusted(-margin + 1, -margin + 1, margin - 1, margin - 1).contains(tip),
            c.name + QStringLiteral(": the chip leaves the arrow tip clear (") + what + QStringLiteral(")"),
            QStringLiteral("tip %1,%2 / chip %3").arg(tip.x(), 0, 'f', 0).arg(tip.y(), 0, 'f', 0).arg(rectText(rect)));
    const auto keep = r.app().interaction().keepClearRect();
    r.check(keep.has_value(), c.name + QStringLiteral(": there is a keep-clear rectangle (") + what + QStringLiteral(")"));
    if (keep) {
        const QRectF k(QPointF(keep->left, keep->top), QPointF(keep->right, keep->bottom));
        r.check(!overlaps(rect, k), c.name + QStringLiteral(": the chip is off the selection, the arrow and the tap (") + what + QStringLiteral(")"),
                rectText(rect) + QStringLiteral(" / ") + rectText(k));
        // (By hand: QRectF::contains is false for an edge's zero-width rectangle.)
        const bool holds = selection.left() >= k.left() - 1 && selection.right() <= k.right() + 1 && selection.top() >= k.top() - 1
                        && selection.bottom() <= k.bottom() + 1;
        r.check(holds, c.name + QStringLiteral(": the keep-clear rectangle holds the ") + what,
                rectText(k) + QStringLiteral(" / ") + rectText(selection));
    }
    checkControls(r, c, what, rect);
}

void addConfig(Steps& steps, AcceptanceRunner& r, const Config& c, const std::shared_ptr<State>& s, bool first)
{
    const QString shot = QStringLiteral("chip_") + c.name;
    steps.push_back([&r, c] {
        r.app().setTouchMode(c.touch);
        r.resizeWindow(c.width, c.height);
        r.window()->setProperty("simulatedSafeArea", c.safeArea);
    });
    wait(steps, 3);
    if (first) {
        steps.push_back([&r, c] {
            r.check(r.window()->width() == c.width && r.window()->height() == c.height, c.name + QStringLiteral(": the window size"),
                    QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
            r.check(r.clickItem(QStringLiteral("tool_box")), c.name + QStringLiteral(": Box"));
        });
        wait(steps, 4);
    }
    steps.push_back([&r, c] {
        r.check(r.app().bodyCount() == 1, c.name + QStringLiteral(": a box"));
        if (c.touch && !r.app().touchMode())
            r.app().setTouchMode(true); // the Box click was a mouse click
        r.app().fitAll();
    });
    wait(steps, 4); // the fit animation
    // ---- The owner's case: a vertical edge on the left side of the body.
    steps.push_back([&r, c] {
        std::vector<Vec3> along;
        for (double z : {10.0, 7.0, 13.0, 5.0, 15.0})
            along.push_back({-10, -10, z});
        select(r, c, r.uncoveredScreenPoint(along));
    });
    wait(steps, 2);
    steps.push_back([&r, c, shot] {
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Fillet"),
                c.name + QStringLiteral(": the left edge offers a fillet"), r.app().operationTitle());
        const QRectF edge = projected(r, {{-10, -10, 0}, {-10, -10, 20}});
        checkChip(r, c, QStringLiteral("left edge"), edge);
        r.screenshot(shot + QStringLiteral("_edge"));
        if (phone(c))
            r.check(r.clickItem(QStringLiteral("action_chamfer")), c.name + QStringLiteral(": tap Chamfer in the value box"));
        else
            r.key(Qt::Key_Escape);
    });
    if (phone(c)) {
        // ---- A phone: the docked chip's buttons work (its actions scroll sideways).
        steps.push_back([&r, c] {
            r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Chamfer"),
                    c.name + QStringLiteral(": Chamfer from the value box"), r.app().operationTitle());
            const QRectF edge = projected(r, {{-10, -10, 0}, {-10, -10, 20}});
            checkChip(r, c, QStringLiteral("chamfered edge"), edge);
            // Back to Fillet (the next edge offers what was used last).
            r.check(r.clickItem(QStringLiteral("action_fillet")), c.name + QStringLiteral(": tap Fillet in the value box"));
        });
        steps.push_back([&r, c] {
            r.check(r.app().operationTitle() == QStringLiteral("Fillet"), c.name + QStringLiteral(": Fillet again"),
                    r.app().operationTitle());
            // The last action ("Select body"): out of sight in a sideways phone
            // until the row scrolls to it.
            QQuickItem* last = r.findItem(QStringLiteral("action_selectBody"));
            const QRectF row = sceneRect(r, QStringLiteral("valueChipActions"));
            const QRectF button = last ? last->mapRectToScene(QRectF(0, 0, last->width(), last->height())) : QRectF();
            r.check(!row.isEmpty() && last, c.name + QStringLiteral(": the value box's actions are a row"));
            const bool inSight = row.adjusted(-0.5, -0.5, 0.5, 0.5).contains(button);
            r.check(r.clickItem(QStringLiteral("action_selectBody")), c.name + QStringLiteral(": tap Select body, the last action"),
                    inSight ? QStringLiteral("in sight") : QStringLiteral("scrolled into sight"));
        });
        steps.push_back([&r, c] {
            const auto& selection = r.app().interaction().selection();
            r.check(selection.size() == 1 && selection.allOfKind(sel::SelectionKind::Body) && r.app().operationTitle() == QStringLiteral("Move"),
                    c.name + QStringLiteral(": Select body selects the body (Move)"), r.app().operationTitle());
            r.check(r.clickItem(QStringLiteral("valueChipCancel")), c.name + QStringLiteral(": tap ✕ in the value box"));
        });
    }
    steps.push_back([&r, c] {
        r.check(r.app().interaction().selection().empty(),
                c.name + (phone(c) ? QStringLiteral(": ✕ clears the selection") : QStringLiteral(": Esc clears the edge")));
        // ---- A face: the box's left face (y = -10), tapped in its middle.
        select(r, c, r.screenPoint(0, -10, 10));
    });
    wait(steps, 2);
    steps.push_back([&r, c, shot] {
        r.check(r.app().operationActive() && r.app().operationTitle() == QStringLiteral("Push/Pull"),
                c.name + QStringLiteral(": the left face offers push/pull"), r.app().operationTitle());
        const QRectF face = projected(r, {{-10, -10, 0}, {10, -10, 0}, {10, -10, 20}, {-10, -10, 20}});
        checkChip(r, c, QStringLiteral("left face"), face);
        r.screenshot(shot + QStringLiteral("_face"));
    });
    if (phone(c)) {
        // ---- A phone: the docked chip stays put while the arrow is dragged
        // (mouse events, as the runner has no touch drag: the mouse layout
        // first, so the controls keep their size during the drag).
        steps.push_back([&r] {
            r.app().setTouchMode(false);
        });
        wait(steps, 2);
        steps.push_back([&r, s] {
            const auto* op = r.app().interaction().operation();
            s->chipBefore = sceneRect(r, QStringLiteral("valueChip"));
            s->dragged = false;
            if (!op)
                return;
            const Vec3 a = op->anchor();
            const QPointF anchor = r.screenPoint(a.x, a.y, a.z);
            const QPointF tip = r.app().valueLabelPosition();
            const QPointF grab = anchor + (tip - anchor) * 0.6;
            r.mousePress(grab);
            // Drag the arrow toward the chip (or away along it): 60 px along the arrow.
            const QPointF along = (tip - anchor) / std::max(1.0, std::hypot(tip.x() - anchor.x(), tip.y() - anchor.y()));
            for (int i = 1; i <= 6; ++i)
                r.mouseMove(grab + along * (10.0 * i), Qt::LeftButton);
            s->dragged = r.app().manipulatorDragging();
        });
        steps.push_back([&r, c, s] {
            r.check(s->dragged, c.name + QStringLiteral(": the push/pull arrow is being dragged"));
            const QRectF during = sceneRect(r, QStringLiteral("valueChip"));
            r.check(std::abs(during.top() - s->chipBefore.top()) < 0.5 && std::abs(during.left() - s->chipBefore.left()) < 0.5,
                    c.name + QStringLiteral(": the docked chip keeps its place while the arrow is dragged"),
                    rectText(during) + QStringLiteral(" / ") + rectText(s->chipBefore));
            const auto* op = r.app().interaction().operation();
            const Vec3 a = op ? op->anchor() : Vec3{};
            const QPointF anchor = r.screenPoint(a.x, a.y, a.z);
            const QPointF tip = r.app().valueLabelPosition();
            const QPointF along = (tip - anchor) / std::max(1.0, std::hypot(tip.x() - anchor.x(), tip.y() - anchor.y()));
            r.mouseRelease(tip - along * 20.0);
            r.check(r.app().operationHasValue(), c.name + QStringLiteral(": the drag changed the value"), r.app().operationValueText());
        });
        steps.push_back([&r, c] {
            if (c.touch)
                r.app().setTouchMode(true); // the drag was a mouse drag
            const QRectF face = projected(r, {{-10, -10, 0}, {10, -10, 0}, {10, -10, 20}, {-10, -10, 20}});
            checkChip(r, c, QStringLiteral("dragged face"), face);
            r.key(Qt::Key_Escape); // back to the face's own position
        });
        // ---- Typing the value: the on-screen keyboard comes up over the
        // bottom of a phone, so the chip docks below the top bar.
        steps.push_back([&r, c, s] {
            s->volume = r.bodyVolume();
            r.check(!r.app().operationHasValue(), c.name + QStringLiteral(": Esc took the drag back"), r.app().operationValueText());
            r.check(r.clickItem(QStringLiteral("valueChipField")), c.name + QStringLiteral(": tap the value field"));
        });
        steps.push_back([&r, c] {
            QQuickItem* chip = r.findItem(QStringLiteral("valueChip"));
            r.check(chip && chip->property("typing").toBool(), c.name + QStringLiteral(": the value field has the focus"));
            const QString spot = chip ? chip->property("placement").toMap().value(QStringLiteral("spot")).toString() : QString();
            const QRectF rect = sceneRect(r, QStringLiteral("valueChip"));
            r.check(spot == QStringLiteral("dockTop") && rect.bottom() < r.window()->height() / 2.0,
                    c.name + QStringLiteral(": typing, the chip docks below the top bar, above where the keyboard comes"),
                    spot + QStringLiteral(" ") + rectText(rect));
            const double w = r.window()->width();
            const double h = r.window()->height();
            const QRectF safe(c.safe[3], c.safe[0], w - c.safe[3] - c.safe[1], h - c.safe[0] - c.safe[2]);
            r.check(safe.adjusted(-0.5, -0.5, 0.5, 0.5).contains(rect), c.name + QStringLiteral(": typing, the chip is inside the safe area"),
                    rectText(rect));
            checkControls(r, c, QStringLiteral("typing"), rect);
            r.screenshot(QStringLiteral("chip_") + c.name + QStringLiteral("_typing"));
            r.type(QStringLiteral("25"));
        });
        steps.push_back([&r, c] {
            r.check(r.clickItem(QStringLiteral("valueChipApply")), c.name + QStringLiteral(": tap ✓ in the value box"));
        });
        steps.push_back([&r, c, s] {
            const double volume = r.bodyVolume();
            // The typed value is the box's size across the face: 20 -> 25 mm.
            r.check(std::abs(volume - s->volume * 25.0 / 20.0) < 1e-6, c.name + QStringLiteral(": typing 25 and ✓ makes the box 25 mm deep"),
                    AcceptanceRunner::num(volume) + QStringLiteral(" (was ") + AcceptanceRunner::num(s->volume) + QStringLiteral(")"));
            QQuickItem* chip = r.findItem(QStringLiteral("valueChip"));
            r.check(!chip || !chip->property("typing").toBool(), c.name + QStringLiteral(": ✓ ends the typing"));
            r.check(r.clickItem(QStringLiteral("undoButton")), c.name + QStringLiteral(": Undo"));
        });
        steps.push_back([&r, c, s] {
            r.check(std::abs(r.bodyVolume() - s->volume) < 1e-6, c.name + QStringLiteral(": Undo takes the push back"),
                    AcceptanceRunner::num(r.bodyVolume()));
        });
    }
    steps.push_back([&r] {
        r.key(Qt::Key_Escape);
    });
    steps.push_back([&r, c] {
        r.check(r.app().interaction().selection().empty(), c.name + QStringLiteral(": Esc clears the face"));
    });
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    Steps steps;
    const Config portrait{QStringLiteral("iphone_portrait"), 402, 874, true, QVariantList{62, 0, 34, 0}, {62, 0, 34, 0}};
    const Config landscape{QStringLiteral("iphone_landscape"), 874, 402, true, QVariantList{0, 62, 21, 62}, {0, 62, 21, 62}};
    const Config tablet{QStringLiteral("ipad"), 1180, 820, true, QVariant(), {0, 0, 0, 0}};
    const Config desk{QStringLiteral("desktop"), 1400, 900, false, QVariant(), {0, 0, 0, 0}};
    addConfig(steps, r, portrait, s, true);
    addConfig(steps, r, landscape, s, false);
    addConfig(steps, r, tablet, s, false);
    addConfig(steps, r, desk, s, false);
    // ---- Back to the run's window.
    steps.push_back([&r] {
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    wait(steps, 3);
    steps.push_back([&r] {
        r.check(r.window()->size() == r.initialWindowSize(), "chip placement: the window is back to its size",
                QStringLiteral("%1x%2").arg(r.window()->width()).arg(r.window()->height()));
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("chipplacement"), 92, steps});

} // namespace
} // namespace os::app
