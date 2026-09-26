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

#include <QtQuick/QQuickItem>
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

// ---- Polygon -------------------------------------------------------------------------

std::vector<AcceptanceRunner::Step> polygon(AcceptanceRunner& r)
{
    auto sides = [&r] {
        const auto* session = r.app().interaction().sketchSession();
        return session ? session->polygonSides() : 0;
    };
    std::vector<AcceptanceRunner::Step> steps{
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_polygon")), "Polygon tool button");
            r.check(r.app().sketchTool() == QStringLiteral("polygon"), "the polygon tool is active", r.app().sketchTool());
            const QQuickItem* counter = r.findItem(QStringLiteral("sketchCounter"));
            r.check(counter && counter->isVisible(), "the side count is shown with -/+ buttons");
            r.check(r.app().sketchCounterText() == QStringLiteral("6 sides"), "six sides by default", r.app().sketchCounterText());
        },
        [&r, sides] {
            r.check(r.clickItem(QStringLiteral("sketchCounterPlus")), "+ button");
            r.check(r.clickItem(QStringLiteral("sketchCounterPlus")), "+ button again");
            r.check(sides() == 8, "two more sides", QString::number(sides()));
            r.check(r.clickItem(QStringLiteral("sketchCounterMinus")), "- button");
            r.key(Qt::Key_Minus, Qt::NoModifier, QStringLiteral("-"));
            r.check(sides() == 6, "- button and - key: back to six", QString::number(sides()));
        },
        [&r] {
            r.click(r.screenPoint(0, 0, 0)); // the center, on the origin
            r.mouseMove(r.screenPoint(12, 0.3, 0)); // straight right: the first side is vertical
            r.check(r.app().sketchDrawing(), "the first click places the center");
            r.type(QStringLiteral("10")); // across flats
            r.key(Qt::Key_Return);
            const auto* s = activeSketch(r);
            r.check(s && s->lines().size() == 6 && s->circles().size() == 2, "a hexagon with its two construction circles",
                    s ? QString::number(s->lines().size()) : QString());
            r.check(r.app().sketchStatus() == QStringLiteral("Fully defined"), "centered, sized and upright: fully defined",
                    r.app().sketchStatus());
            double maxX = -1e9, farthest = 0;
            if (s)
                for (const auto& [id, p] : s->points()) {
                    maxX = std::max(maxX, p.position.x);
                    farthest = std::max(farthest, p.position.length());
                }
            r.check(std::abs(maxX - 5) < 1e-6 && std::abs(farthest - 10 / std::sqrt(3.0)) < 1e-6,
                    "10 across flats: a flat at x = 5, corners 5.7735 from the center",
                    AcceptanceRunner::num(maxX) + QStringLiteral(" / ") + AcceptanceRunner::num(farthest));
            r.screenshot(QStringLiteral("sketch3_polygon"));
        },
    };
    const double hexagon = 6 * 25 * std::tan(kPi / 6);
    append(steps, finishAndExtrude(r, 0, 1, QStringLiteral("5"), hexagon * 5, QStringLiteral("hexagon")));
    return steps;
}

// ---- Tangent arc -------------------------------------------------------------------------

// A "D": a line, a tangent half circle back over it, two lines to close it.
std::vector<AcceptanceRunner::Step> tangentArc(AcceptanceRunner& r)
{
    std::vector<AcceptanceRunner::Step> steps{
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_line")), "Line tool button");
            r.click(r.screenPoint(0, 0, 0));
            r.click(r.screenPoint(20, 0, 0));
            r.key(Qt::Key_Escape);
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_tangentArc")), "Tangent arc tool button");
            r.check(r.app().sketchTool() == QStringLiteral("tangentArc"), "the tangent arc tool is active", r.app().sketchTool());
            r.click(r.screenPoint(20, 0, 0)); // the line's end
            r.check(r.app().sketchDrawing(), "the arc starts on the line's end");
            r.mouseMove(r.screenPoint(24, 12, 0));
            r.click(r.screenPoint(20, 20, 0));
            r.key(Qt::Key_Escape); // end the chain
            const auto* s = activeSketch(r);
            bool half = false;
            if (s && s->arcs().size() == 1) {
                const auto& [id, arc] = *s->arcs().begin();
                half = std::abs(s->arcRadius(id) - 10) < 1e-6 && (s->point(arc.center)->position - Vec2{20, 10}).length() < 1e-6;
            }
            r.check(half, "a tangent half circle of radius 10 around (20, 10)");
        },
        [&r] {
            r.key(Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
            r.click(r.screenPoint(20, 20, 0));
            r.click(r.screenPoint(0, 20, 0));
            r.click(r.screenPoint(0, 0, 0));
            const auto* s = activeSketch(r);
            r.check(s && s->lines().size() == 3, "three lines and the arc", s ? QString::number(s->lines().size()) : QString());
            r.screenshot(QStringLiteral("sketch3_tangent_arc"));
        },
    };
    append(steps, finishAndExtrude(r, 10, 10, QStringLiteral("10"), (400 + 50 * kPi) * 10, QStringLiteral("D shape")));
    return steps;
}

// ---- Constraint icons -------------------------------------------------------------------

sketch::EntityId firstConstraint(AcceptanceRunner& r, sketch::ConstraintKind kind)
{
    if (const auto* s = activeSketch(r))
        for (const auto& [id, c] : s->constraints())
            if (c.kind == kind)
                return id;
    return sketch::kNoEntity;
}

std::size_t countConstraints(AcceptanceRunner& r, sketch::ConstraintKind kind)
{
    std::size_t n = 0;
    if (const auto* s = activeSketch(r))
        for (const auto& [id, c] : s->constraints())
            n += c.kind == kind ? 1 : 0;
    return n;
}

std::vector<AcceptanceRunner::Step> constraintIcons(AcceptanceRunner& r)
{
    using K = sketch::ConstraintKind;
    return {
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            // A free rectangle (not on the origin), then the select tool.
            r.click(r.screenPoint(-10, -5, 0));
            r.click(r.screenPoint(20, 10, 0));
            r.check(r.clickItem(QStringLiteral("tool_select")), "Select tool button");
            r.check(countConstraints(r, K::Horizontal) == 2 && countConstraints(r, K::Vertical) == 2,
                    "a rectangle with two H and two V constraints");
        },
        [&r] {
            const auto id = firstConstraint(r, K::Horizontal);
            const QString name = QStringLiteral("constraintIcon_%1").arg(id);
            const QQuickItem* icon = r.findItem(name);
            r.check(icon && icon->isVisible(), "the H glyph is on the canvas");
            r.screenshot(QStringLiteral("sketch3_constraint_icons"));
            r.check(r.clickItem(name), "clicking the H glyph");
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->selection().size() == 1 && session->selection().front() == id,
                    "selects that constraint");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("sketchAction_delete")), "Delete constraint button");
            r.check(countConstraints(r, K::Horizontal) == 1, "the constraint is gone",
                    QString::number(countConstraints(r, K::Horizontal)));
            const auto* s = activeSketch(r);
            r.check(s && s->lines().size() == 4, "the rectangle stays");
            r.check(r.app().sketchStatus() == QStringLiteral("5 degrees of freedom"), "one more degree of freedom",
                    r.app().sketchStatus());
        },
        [&r] {
            // Touch layout: the glyph still selects, and the Delete key removes it.
            r.app().setTouchMode(true);
            const auto id = firstConstraint(r, K::Vertical);
            r.check(r.clickItem(QStringLiteral("constraintIcon_%1").arg(id)), "clicking a V glyph (touch layout)");
            r.screenshot(QStringLiteral("sketch3_constraint_icons_touch"));
            r.key(Qt::Key_Delete);
            r.check(countConstraints(r, K::Vertical) == 1, "Delete removes the selected constraint",
                    QString::number(countConstraints(r, K::Vertical)));
            r.app().setTouchMode(false);
        },
        [&r] {
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(countConstraints(r, K::Horizontal) == 2 && countConstraints(r, K::Vertical) == 2,
                    "undo brings both constraints back");
        },
    };
}

// ---- Mirror and pattern ----------------------------------------------------------------------

// Half a 20 x 20 square against a construction center line, mirrored.
std::vector<AcceptanceRunner::Step> mirror(AcceptanceRunner& r)
{
    std::vector<AcceptanceRunner::Step> steps{
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            r.key(Qt::Key_L, Qt::NoModifier, QStringLiteral("l"));
            r.click(r.screenPoint(0, -5, 0));
            r.click(r.screenPoint(0, 25, 0));
            r.key(Qt::Key_Escape);
            for (const auto& [x, y] : {std::pair{0.0, 0.0}, {10.0, 0.0}, {10.0, 20.0}, {0.0, 20.0}})
                r.click(r.screenPoint(x, y, 0));
            r.key(Qt::Key_Escape);
            r.check(r.clickItem(QStringLiteral("tool_select")), "Select tool button");
            r.click(r.screenPoint(0, 10, 0)); // the center line
            r.check(r.clickItem(QStringLiteral("sketchAction_construction")), "Construction on the center line");
            r.key(Qt::Key_Escape);
        },
        [&r] {
            r.click(r.screenPoint(5, 0, 0));
            r.click(r.screenPoint(10, 10, 0), Qt::ShiftModifier);
            r.click(r.screenPoint(5, 20, 0), Qt::ShiftModifier);
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->selection().size() == 3, "the half profile selected",
                    session ? QString::number(session->selection().size()) : QString());
            r.check(r.clickItem(QStringLiteral("sketchAction_mirror")), "Mirror button");
            r.check(session && session->isMirroring(), "waits for the line to mirror across");
        },
        [&r] {
            r.mouseMove(r.screenPoint(0, 12, 0));
            r.click(r.screenPoint(0, 12, 0));
            const auto* s = activeSketch(r);
            r.check(s && s->lines().size() == 7, "mirrored: three new lines", s ? QString::number(s->lines().size()) : QString());
            r.check(countConstraints(r, sketch::ConstraintKind::Symmetric) == 2, "the copies stay mirrored (two symmetric pairs)");
            r.screenshot(QStringLiteral("sketch3_mirror"));
        },
    };
    append(steps, finishAndExtrude(r, 3, 10, QStringLiteral("5"), 20 * 20 * 5, QStringLiteral("mirrored square")));
    return steps;
}

// A plate with a row of holes (linear pattern) and holes turned about the origin (circular).
std::vector<AcceptanceRunner::Step> pattern(AcceptanceRunner& r)
{
    auto circles = [&r] {
        const auto* s = activeSketch(r);
        return s ? s->circles().size() : std::size_t(0);
    };
    std::vector<AcceptanceRunner::Step> steps{
        startSketch(r),
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(-20, -20, 0)); // the plate (the rectangle tool is active)
            r.click(r.screenPoint(55, 20, 0));
            r.check(r.clickItem(QStringLiteral("tool_circle")), "Circle tool button");
            r.click(r.screenPoint(10, 10, 0));
            r.mouseMove(r.screenPoint(13, 10, 0));
            r.type(QStringLiteral("6"));
            r.key(Qt::Key_Return);
            r.check(r.clickItem(QStringLiteral("tool_select")), "Select tool button");
            r.click(r.screenPoint(13, 10, 0)); // the hole
            r.check(r.clickItem(QStringLiteral("sketchAction_pattern")), "Pattern button");
            r.check(r.app().sketchCounterText() == QStringLiteral("3 in total"), "three in a row by default",
                    r.app().sketchCounterText());
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("sketchCounterPlus")), "+ button");
            r.click(r.screenPoint(25, 10, 0)); // where the next hole goes
            r.type(QStringLiteral("12"));      // the spacing
            r.screenshot(QStringLiteral("sketch3_pattern_linear"));
        },
        [&r, circles] {
            r.check(r.clickItem(QStringLiteral("sketchAction_apply")), "Apply button");
            r.check(circles() == 4, "four holes in a row", QString::number(circles()));
            double farthest = 0;
            if (const auto* s = activeSketch(r))
                for (const auto& [id, c] : s->circles())
                    farthest = std::max(farthest, s->point(c.center)->position.x);
            r.check(std::abs(farthest - 46) < 1e-6, "12 mm apart: the last at x = 46", AcceptanceRunner::num(farthest));
        },
        [&r] {
            r.click(r.screenPoint(13, 10, 0)); // the first hole again
            r.check(r.clickItem(QStringLiteral("sketchAction_pattern")), "Pattern button (circular)");
            r.check(r.clickItem(QStringLiteral("sketchAction_pattern:circular")), "Circular button");
            r.click(r.screenPoint(0, 0, 0)); // the center: the origin
            r.type(QStringLiteral("180"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("3"));
            r.screenshot(QStringLiteral("sketch3_pattern_circular"));
            r.key(Qt::Key_Return);
        },
        [&r, circles] {
            r.check(circles() == 6, "two more holes turned about the origin", QString::number(circles()));
            bool opposite = false;
            if (const auto* s = activeSketch(r))
                for (const auto& [id, c] : s->circles())
                    opposite = opposite || (s->point(c.center)->position - Vec2{-10, -10}).length() < 1e-6;
            r.check(opposite, "the last one half a turn round, at (-10, -10)");
        },
    };
    append(steps, finishAndExtrude(r, 0, -15, QStringLiteral("4"), (75 * 40 - 6 * kPi * 9) * 4,
                                   QStringLiteral("plate with six holes")));
    return steps;
}

const bool registeredMirror = registerAcceptanceScenario({QStringLiteral("sketch3_mirror"), 64, mirror});
const bool registeredPattern = registerAcceptanceScenario({QStringLiteral("sketch3_pattern"), 65, pattern});
const bool registeredConstraintIcons =registerAcceptanceScenario({QStringLiteral("sketch3_constrainticons"), 63, constraintIcons});
const bool registeredTangentArc =registerAcceptanceScenario({QStringLiteral("sketch3_tangentarc"), 62, tangentArc});
const bool registeredCenterRectangle =registerAcceptanceScenario({QStringLiteral("sketch3_centerrect"), 60, centerRectangle});
const bool registeredPolygon = registerAcceptanceScenario({QStringLiteral("sketch3_polygon"), 61, polygon});

} // namespace
} // namespace os::app
