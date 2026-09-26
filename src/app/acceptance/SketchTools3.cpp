// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Sketch toolkit, part 3: center rectangle, polygon, tangent arc, constraint
// icons, sketch mirror and pattern. Every tool is reached through its tool bar
// button, drawn with the real mouse, sized by typing, and the result extruded
// and measured.

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickWindow>

#include <cmath>

namespace os::app {
namespace {

const sketch::Sketch* activeSketch(AcceptanceRunner& r)
{
    const auto* session = r.app().interaction().sketchSession();
    return session ? &session->sketch() : nullptr;
}

// K on an empty selection: a sketch on the ground plane (then the view turns).
AcceptanceRunner::Step startSketch(AcceptanceRunner& r)
{
    return [&r] {
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
        r.check(r.app().sketchMode(), "K starts a sketch");
    };
}

// Finish the sketch, click inside the profile at (x, y), type a height, Enter.
std::vector<AcceptanceRunner::Step> finishAndExtrude(AcceptanceRunner& r, double x, double y, const QString& height,
                                                     double expectedVolume, const QString& what)
{
    return {
        [&r] { r.check(r.clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button"); },
        [] {}, [] {}, [] {}, [] {},
        [&r, x, y, height] {
            r.click(r.screenPoint(x, y, 0));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "the profile is selected for extrusion",
                    r.app().operationTitle());
            r.type(height);
            r.key(Qt::Key_Return);
        },
        [&r, expectedVolume, what] {
            r.check(r.app().bodyCount() == 1, what + QStringLiteral(": extruded into a body"));
            const double volume = r.bodyVolume();
            r.check(std::abs(volume - expectedVolume) < 1e-3 * std::max(1.0, expectedVolume),
                    what + QStringLiteral(": volume ") + AcceptanceRunner::num(expectedVolume), AcceptanceRunner::num(volume));
        },
    };
}

void append(std::vector<AcceptanceRunner::Step>& to, std::vector<AcceptanceRunner::Step> more)
{
    for (auto& step : more)
        to.push_back(std::move(step));
}

// ---- Center rectangle ---------------------------------------------------------------

std::vector<AcceptanceRunner::Step> centerRectangle(AcceptanceRunner& r)
{
    std::vector<AcceptanceRunner::Step> steps{
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_centerRectangle")), "Center rectangle tool button");
            r.check(r.app().sketchTool() == QStringLiteral("centerRectangle"), "the center rectangle tool is active",
                    r.app().sketchTool());
            r.click(r.screenPoint(0, 0, 0)); // the center, on the origin
            r.mouseMove(r.screenPoint(14, 7, 0));
            r.check(r.app().sketchDrawing(), "the first click places the center");
            r.type(QStringLiteral("40"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Return);
            const auto* s = activeSketch(r);
            r.check(s && s->lines().size() == 5, "four sides and a construction diagonal",
                    s ? QString::number(s->lines().size()) : QString());
            r.check(r.app().sketchStatus() == QStringLiteral("Fully defined"), "centered on the origin and sized: fully defined",
                    r.app().sketchStatus());
            double minX = 1e9, maxX = -1e9;
            if (s)
                for (const auto& [id, p] : s->points()) {
                    minX = std::min(minX, p.position.x);
                    maxX = std::max(maxX, p.position.x);
                }
            r.check(std::abs(minX + 20) < 1e-6 && std::abs(maxX - 20) < 1e-6, "the rectangle spans -20..20",
                    AcceptanceRunner::num(minX) + QStringLiteral("..") + AcceptanceRunner::num(maxX));
            r.screenshot(QStringLiteral("sketch3_center_rectangle"));
        },
    };
    append(steps, finishAndExtrude(r, 5, 5, QStringLiteral("10"), 40 * 20 * 10, QStringLiteral("center rectangle")));
    steps.push_back([&r] {
        const auto bb = geom::boundingBox(r.body(0).shape());
        r.check(std::abs(bb.min.x + 20) < 1e-6 && std::abs(bb.max.y - 10) < 1e-6, "the block is centered on the origin",
                AcceptanceRunner::num(bb.min.x) + QStringLiteral(" / ") + AcceptanceRunner::num(bb.max.y));
    });
    return steps;
}

const bool registeredCenterRectangle = registerAcceptanceScenario({QStringLiteral("sketch3_centerrect"), 60, centerRectangle});

} // namespace
} // namespace os::app
