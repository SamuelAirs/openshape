// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Holes for screws: a counterbore and a countersink on a hole's rim, chosen
// in the value chip with screw presets, sized by typing and by the depth
// arrow ("holes"); the Hole tool placing holes on a face by clicks, snapping
// and typed X / Y ("hole_tool"). Checked by exact volumes.

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

double frustum(double r1, double r2, double h)
{
    return kPi * h / 3 * (r1 * r1 + r1 * r2 + r2 * r2);
}

// Where the middle of an operation handle's arrow is on screen.
QPointF handlePoint(AcceptanceRunner& r, int index)
{
    const interact::Operation* op = r.app().interaction().operation();
    if (!op || index >= op->handleCount())
        return {};
    const interact::LinearManipulator h = op->handle(index);
    const Vec3 a = h.anchor(op->handleOffset(index));
    const Vec3 mid = a + h.direction() * (50 * r.app().interaction().camera().pixelSize(a));
    const Vec2 p = r.app().interaction().camera().project(mid);
    return {p.x, p.y};
}

struct State {
    double holed = 0; // the plate with its through hole
};

// A 20 x 20 x 10 plate with an M3 clearance hole (3.4 mm, ISO 273 normal fit)
// through its middle, sketched and cut through the real UI.
std::vector<AcceptanceRunner::Step> plateWithHole(AcceptanceRunner& r, std::shared_ptr<State> s)
{
    return {
        [&r] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "holes: a box");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(0, 0, 20));
            r.check(r.app().operationValueLabel() == QStringLiteral("Height"), "holes: the top face shows the height",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("10"));
            r.key(Qt::Key_Return);
            r.check(std::abs(r.bodyHeight() - 10) < 1e-6, "holes: a 10 mm plate", AcceptanceRunner::num(r.bodyHeight()));
            r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            r.check(r.app().sketchMode(), "holes: K sketches on the top face");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.key(Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
            r.click(r.screenPoint(0, 0, 10));
            r.mouseMove(r.screenPoint(1, 0, 10));
            r.type(QStringLiteral("3.4"));
            r.key(Qt::Key_Return);
            r.check(r.clickItem(QStringLiteral("finishSketchButton")), "holes: Finish sketch button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(0, 0, 10));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "holes: the circle is selected",
                    r.app().operationTitle());
            r.type(QStringLiteral("-10"));
        },
        [&r, s] {
            r.check(r.clickItem(QStringLiteral("action_throughAll")), "holes: Through all");
            r.key(Qt::Key_Return);
            s->holed = r.bodyVolume();
            r.check(std::abs(s->holed - (4000 - kPi * 1.7 * 1.7 * 10)) < 1e-3, "holes: a 3.4 mm hole through the plate",
                    AcceptanceRunner::num(s->holed));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().interaction().fitAll(false);
            const QPointF hole = r.screenPoint(0, 0, 10);
            r.app().interaction().wheel({hole.x(), hole.y()}, 3);
        },
        [] {},
    };
}

std::vector<AcceptanceRunner::Step> heads(AcceptanceRunner& r, std::shared_ptr<State> s)
{
    return {
        // The rim offers Counterbore; M5, then a typed diameter.
        [&r] {
            r.click(r.screenPoint(1.7, 0, 10));
            const auto& sel = r.app().interaction().selection();
            r.check(sel.size() == 1 && sel.items()[0].kind == sel::SelectionKind::Edge, "holes: clicking the rim selects it");
            r.check(r.clickItem(QStringLiteral("action_counterbore")), "holes: Counterbore action");
            r.check(r.app().operationTitle() == QStringLiteral("Counterbore M3"), "holes: an M3 counterbore to start with",
                    r.app().operationTitle());
            r.check(r.app().operationValueText() == QStringLiteral("6.50 mm"), "holes: M3 counterbore is 6.5 mm (DIN 974-1)",
                    r.app().operationValueText());
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("action_preset:4")), "holes: M5 preset");
            r.check(r.app().operationTitle() == QStringLiteral("Counterbore M5"), "holes: M5 counterbore", r.app().operationTitle());
            r.type(QStringLiteral("7"));
        },
        [&r] {
            r.check(r.app().operationTitle() == QStringLiteral("Counterbore"), "holes: a typed diameter is no preset",
                    r.app().operationTitle());
            // The arrow into the hole sets the depth.
            r.click(handlePoint(r, 1));
            r.check(r.app().operationValueLabel() == QStringLiteral("Depth"), "holes: the depth arrow", r.app().operationValueLabel());
            r.type(QStringLiteral("4"));
        },
        [&r, s] {
            r.key(Qt::Key_Return);
            const double expected = s->holed - kPi * (3.5 * 3.5 - 1.7 * 1.7) * 4;
            r.check(std::abs(r.bodyVolume() - expected) < 1e-3, "holes: a 7 x 4 mm counterbore removes its ring",
                    AcceptanceRunner::num(r.bodyVolume()));
            r.screenshot(QStringLiteral("holes_counterbore"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(r.bodyVolume() - s->holed) < 1e-3, "holes: undo takes the counterbore away",
                    AcceptanceRunner::num(r.bodyVolume()));
        },
        [] {},
        // Countersink on the same rim: the last screw size (M5) is kept; pick M3.
        [&r] {
            r.key(Qt::Key_Escape);
            r.click(r.screenPoint(1.7, 0, 10));
            r.check(r.clickItem(QStringLiteral("action_countersink")), "holes: Countersink action");
            r.check(r.app().operationTitle() == QStringLiteral("Countersink M5"), "holes: the last screw size is kept",
                    r.app().operationTitle());
            r.check(r.clickItem(QStringLiteral("action_preset:2")), "holes: M3 preset");
            r.check(r.app().operationValueText() == QStringLiteral("6.72 mm"), "holes: M3 countersink is 6.72 mm (ISO 10642)",
                    r.app().operationValueText());
        },
        [&r, s] {
            r.key(Qt::Key_Return);
            const double R = 3.36, rr = 1.7, h = R - rr;
            const double expected = s->holed - (frustum(R, rr, h) - kPi * rr * rr * h);
            r.check(std::abs(r.bodyVolume() - expected) < 1e-3, "holes: a 90-degree countersink removes its frustum",
                    AcceptanceRunner::num(r.bodyVolume()));
            bool listed = false;
            for (const auto& row : r.app().interaction().historyRows())
                listed = listed || row.name == "Countersink";
            r.check(listed, "holes: the Model panel lists the countersink");
            r.screenshot(QStringLiteral("holes_countersink"));
        },
    };
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    std::vector<AcceptanceRunner::Step> all = plateWithHole(r, s);
    for (auto& step : heads(r, s))
        all.push_back(std::move(step));
    return all;
}

const interact::HoleOperation* holeTool(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::HoleOperation*>(r.app().interaction().operation());
}

// The Hole tool from the palette: a click snapped to the face's center, a
// second hole placed by typed X / Y, M3 close fit with a countersink; one
// step; saved and reopened.
std::vector<AcceptanceRunner::Step> toolSteps(AcceptanceRunner& r)
{
    return {
        [&r] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "hole tool: a box");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(3, -3, 20));
            r.type(QStringLiteral("5"));
            r.key(Qt::Key_Return);
            r.check(std::abs(r.bodyHeight() - 5) < 1e-6, "hole tool: a 20 x 20 x 5 plate", AcceptanceRunner::num(r.bodyHeight()));
            r.app().interaction().fitAll(false);
        },
        [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_hole")), "hole tool: Hole in the Modify palette");
            r.check(holeTool(r) != nullptr, "hole tool: the selected top face takes holes", r.app().operationTitle());
            r.check(!r.app().operationPrompt().isEmpty(), "hole tool: it asks where the first hole goes");
        },
        [&r] {
            // Near the center: it snaps there.
            const double px = r.app().interaction().camera().pixelSize({0, 0, 5});
            r.click(r.screenPoint(4 * px, 3 * px, 5));
            const auto* tool = holeTool(r);
            r.check(tool && tool->positions().size() == 1 && tool->positions()[0].length() < 1e-9,
                    "hole tool: a click near the center snaps to it",
                    tool && !tool->positions().empty()
                        ? AcceptanceRunner::num(tool->positions()[0].x) + QStringLiteral(", ") + AcceptanceRunner::num(tool->positions()[0].y)
                        : QString());
            // A second hole, then its exact place typed: 4 from the left, 15 from the front edge.
            r.click(r.screenPoint(-5.3, 4.7, 5));
            r.check(tool && tool->positions().size() == 2, "hole tool: a second click adds a hole");
            r.check(r.clickItem(QStringLiteral("action_field:x")), "hole tool: X field");
        },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("X from corner"), "hole tool: X is measured from the corner",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("4"));
        },
        [&r] {
            // Tab in the chip goes on to Y, its current value selected.
            r.key(Qt::Key_Tab);
            r.check(r.app().operationValueLabel() == QStringLiteral("Y from corner"), "hole tool: Tab goes to Y",
                    r.app().operationValueLabel());
        },
        [&r] {
            r.type(QStringLiteral("15"));
        },
        [&r] {
            const auto* tool = holeTool(r);
            r.check(tool && tool->livePositions(tool->value()).size() == 2
                        && (tool->livePositions(tool->value())[1] - Vec2{-6, 5}).length() < 1e-9,
                    "hole tool: the typed position (-6, 5)");
            r.check(r.clickItem(QStringLiteral("action_field:diameter")), "hole tool: back to the diameter");
        },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("Diameter"), "hole tool: the chip shows the diameter",
                    r.app().operationValueLabel());
            r.check(r.clickItem(QStringLiteral("action_field:y")), "hole tool: Y field");
        },
        [&r] {
            r.check(r.app().operationValueText() == QStringLiteral("15.00 mm"), "hole tool: Y kept its typed value",
                    r.app().operationValueText());
            r.check(r.clickItem(QStringLiteral("action_fit:close")), "hole tool: Close fit");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("action_head:countersink")), "hole tool: Countersink");
        },
        [&r] {
            const auto* tool = holeTool(r);
            r.check(tool && std::abs(tool->diameter() - 3.2) < 1e-9, "hole tool: M3 close fit is 3.2 mm (ISO 273)");
            r.screenshot(QStringLiteral("hole_tool_preview"));
            r.key(Qt::Key_Return);
            const double h = 3.36 - 1.6;
            const double oneHole = kPi * 1.6 * 1.6 * 5 + frustum(3.36, 1.6, h) - kPi * 1.6 * 1.6 * h;
            r.check(std::abs(r.bodyVolume() - (2000 - 2 * oneHole)) < 1e-3, "hole tool: two countersunk M3 holes through the plate",
                    AcceptanceRunner::num(r.bodyVolume()));
            r.check(r.body(0).features().size() == 3, "hole tool: one step for both holes");
            bool listed = false;
            for (const auto& row : r.app().interaction().historyRows())
                listed = listed || (row.name == "Holes" && row.detail.find("M3 close fit") != std::string::npos);
            r.check(listed, "hole tool: the Model panel lists the holes");
        },
        [] {},
        [&r] {
            r.screenshot(QStringLiteral("hole_tool"));
            const double volume = r.bodyVolume();
            const QString path = QDir::temp().filePath(QStringLiteral("openshape_acceptance_holes.openshape"));
            QFile::remove(path);
            r.check(r.app().saveProjectAs(QUrl::fromLocalFile(path)), "hole tool: the project saves");
            r.app().newDocument();
            r.check(r.app().openProject(QUrl::fromLocalFile(path)), "hole tool: and reopens");
            r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - volume) < 1e-6, "hole tool: the same holes after reopening",
                    AcceptanceRunner::num(r.bodyVolume()));
            QFile::remove(path);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("holes"), 70, steps});
const bool registeredTool = registerAcceptanceScenario({QStringLiteral("hole_tool"), 71, toolSteps});

} // namespace
} // namespace os::app
