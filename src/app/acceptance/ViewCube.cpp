// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The view cube (AxisTriad.qml, interact::ViewCube): a click on its Top face
// looks straight down, a click on a corner down that corner's diagonal, and a
// drag that starts on it orbits the view while the sketch's Line tool is
// armed, without drawing anything. Then the same by touch in an iPhone-sized
// window (portrait and landscape, with the Dynamic Island's margins): taps
// on a corner and a face, a one-finger drag with the Line tool armed.

#include "app/AcceptanceRunner.h"
#include "interaction/InteractionController.h"
#include "interaction/SketchSession.h"
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

struct CubeState {
    double yaw = 0;
    double pitch = 0;
    std::size_t sketchItems = 0;
};

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

QString vec(Vec3 v)
{
    return QStringLiteral("%1 %2 %3").arg(v.x, 0, 'f', 9).arg(v.y, 0, 'f', 9).arg(v.z, 0, 'f', 9);
}

bool near(Vec3 a, Vec3 b, double tolerance)
{
    return std::abs(a.x - b.x) <= tolerance && std::abs(a.y - b.y) <= tolerance && std::abs(a.z - b.z) <= tolerance;
}

Vec3 backward(AcceptanceRunner& r)
{
    return r.app().interaction().camera().backward();
}

// Points, lines, circles and arcs of the sketch being edited.
std::size_t sketchItems(AcceptanceRunner& r)
{
    const auto* session = r.app().interaction().sketchSession();
    if (!session)
        return 0;
    const auto& s = session->sketch();
    return s.points().size() + s.lines().size() + s.circles().size() + s.arcs().size();
}

void remember(AcceptanceRunner& r, CubeState& s)
{
    s.yaw = r.app().interaction().camera().yaw;
    s.pitch = r.app().interaction().camera().pitch;
    s.sketchItems = sketchItems(r);
}

// The window's safe area [top, right, bottom, left] holds the cube, which is
// at least Apple's 44 pt touch target and clear of the View button.
void checkPlacement(AcceptanceRunner& r, const QString& what, double top, double right, double bottom, double left)
{
    const QRectF cube = sceneRect(r, QStringLiteral("axisTriad"));
    const double w = r.window()->width(), h = r.window()->height();
    r.check(cube.width() >= 44 && cube.height() >= 44, what + QStringLiteral(": the cube is at least 44 px"), rectText(cube));
    r.check(!cube.isEmpty() && cube.left() >= left - 0.5 && cube.top() >= top - 0.5 && cube.right() <= w - right + 0.5
                && cube.bottom() <= h - bottom + 0.5,
            what + QStringLiteral(": the cube is inside the safe area"), rectText(cube));
    const QRectF viewButton = sceneRect(r, QStringLiteral("viewButtonPanel"));
    r.check(!cube.intersects(viewButton), what + QStringLiteral(": the cube is clear of the View button"),
            rectText(cube) + QStringLiteral(" / ") + rectText(viewButton));
}

// A yaw change of `expected` radians (and no pitch change) since `s`.
void checkOrbit(AcceptanceRunner& r, const CubeState& s, double expected, const QString& what)
{
    const auto& camera = r.app().interaction().camera();
    const double turned = std::remainder(camera.yaw - s.yaw, 2 * kPi);
    r.check(std::abs(turned - expected) < 1e-3, what + QStringLiteral(": the drag on the cube orbits the view"),
            AcceptanceRunner::num(turned));
    r.check(std::abs(camera.pitch - s.pitch) < 1e-3, what + QStringLiteral(": a sideways drag keeps the elevation"),
            AcceptanceRunner::num(camera.pitch - s.pitch));
    r.check(r.app().sketchMode() && r.app().sketchTool() == QStringLiteral("line"),
            what + QStringLiteral(": still sketching with the Line tool"), r.app().sketchTool());
    r.check(sketchItems(r) == s.sketchItems && !r.app().sketchDrawing(),
            what + QStringLiteral(": the drag drew nothing"), QString::number(sketchItems(r)));
}

Steps steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<CubeState>();
    const Vec3 corner = Vec3{1, 1, 1} / std::sqrt(3.0);
    Steps steps;
    // ---- Desktop, with the mouse.
    steps.push_back([&r] {
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(r.app().bodyCount() == 1, "view cube: a box to look at");
        QQuickItem* cube = r.findItem(QStringLiteral("axisTriad"));
        r.check(cube && cube->isVisible() && cube->width() >= 44, "view cube: shown",
                cube ? AcceptanceRunner::num(cube->width()) : QString());
        QQuickItem* front = r.findItem(QStringLiteral("viewCubeFace_Front"));
        QQuickItem* left = r.findItem(QStringLiteral("viewCubeFace_Left"));
        r.check(front && front->isVisible() && left && !left->isVisible(),
                "view cube: the faces turned toward the viewer are drawn, the others not");
        r.screenshot(QStringLiteral("viewcube_01_iso"));
        r.check(r.clickItem(QStringLiteral("viewCubeFace_Top")), "view cube: click its Top face");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(near(backward(r), {0, 0, 1}, 1e-6), "view cube: Top looks straight down (+Z)", vec(backward(r)));
        // From Top the four upper corners show; (+X, +Y, +Z) is id 13.
        r.check(r.clickItem(QStringLiteral("viewCubeCorner_13")), "view cube: click its back right upper corner");
    });
    wait(steps, 4);
    steps.push_back([&r, corner] {
        r.check(near(backward(r), corner, 1e-6), "view cube: the corner looks down the diagonal (1, 1, 1)", vec(backward(r)));
        r.screenshot(QStringLiteral("viewcube_02_corner"));
        // A sketch on the ground with the Line tool armed.
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
        r.check(r.app().sketchMode(), "view cube: K starts a sketch");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(r.clickItem(QStringLiteral("tool_line")), "view cube: the Line tool");
        r.check(r.app().sketchTool() == QStringLiteral("line"), "view cube: the Line tool is armed", r.app().sketchTool());
        remember(r, *s);
        // 80 px to the left (the cube is at the right edge): 0.64 rad (Camera::orbit).
        const QRectF cube = sceneRect(r, QStringLiteral("axisTriad"));
        r.drag(cube.center(), cube.center() - QPointF(80, 0), 8);
    });
    steps.push_back([&r, s] {
        checkOrbit(r, *s, 0.64, QStringLiteral("view cube (mouse)"));
        r.screenshot(QStringLiteral("viewcube_03_orbited_in_sketch"));
    });
    // ---- iPhone 16 Pro in portrait (402x874), by touch, still sketching.
    steps.push_back([&r] {
        r.app().setTouchMode(true);
        r.resizeWindow(402, 874);
        r.window()->setProperty("simulatedSafeArea", QVariantList{62, 0, 34, 0});
    });
    wait(steps, 3);
    steps.push_back([&r] {
        checkPlacement(r, QStringLiteral("view cube (portrait)"), 62, 0, 34, 0);
        r.check(r.app().sketchTool() == QStringLiteral("line"), "view cube (portrait): the Line tool is still armed",
                r.app().sketchTool());
        r.screenshot(QStringLiteral("viewcube_04_portrait"));
        // Looking straight down again: the upper corners show; (-X, -Y, +Z) is id 10.
        r.check(r.tapItem(QStringLiteral("viewCubeCorner_10")), "view cube (portrait): tap its front left upper corner");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(near(backward(r), Vec3{-1, -1, 1} / std::sqrt(3.0), 1e-6),
                "view cube (portrait): the corner looks down its diagonal", vec(backward(r)));
        r.check(r.tapItem(QStringLiteral("viewCubeFace_Front")), "view cube (portrait): tap its Front face");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(near(backward(r), {0, -1, 0}, 1e-6), "view cube (portrait): Front looks along +Y", vec(backward(r)));
        r.check(r.app().touchMode(), "view cube (portrait): the taps keep the touch layout");
        remember(r, *s);
        const QRectF cube = sceneRect(r, QStringLiteral("axisTriad"));
        r.touchPress(cube.center());
    });
    // One finger 60 px to the left, a move per frame (Qt Quick holds a
    // finger's moves until its next frame).
    for (int i = 1; i <= 6; ++i) {
        steps.push_back([&r, i] {
            const QRectF cube = sceneRect(r, QStringLiteral("axisTriad"));
            r.touchMove(cube.center() - QPointF(10.0 * i, 0));
        });
    }
    steps.push_back([&r] {
        const QRectF cube = sceneRect(r, QStringLiteral("axisTriad"));
        r.touchRelease(cube.center() - QPointF(60, 0));
    });
    steps.push_back([&r, s] {
        checkOrbit(r, *s, 0.48, QStringLiteral("view cube (portrait)"));
        r.screenshot(QStringLiteral("viewcube_05_portrait_orbited"));
    });
    // ---- Turned to landscape (874x402), the island on the side.
    steps.push_back([&r] {
        r.resizeWindow(874, 402);
        r.window()->setProperty("simulatedSafeArea", QVariantList{0, 62, 21, 62});
    });
    wait(steps, 3);
    steps.push_back([&r] {
        checkPlacement(r, QStringLiteral("view cube (landscape)"), 0, 62, 21, 62);
        // Level after the sideways drag: Top is edge-on and not drawn, but the
        // Front face's upper corners show; (+X, -Y, +Z) is the Iso view.
        QQuickItem* top = r.findItem(QStringLiteral("viewCubeFace_Top"));
        r.check(top && !top->isVisible(), "view cube (landscape): an edge-on face is not drawn");
        r.check(r.tapItem(QStringLiteral("viewCubeCorner_11")), "view cube (landscape): tap its front right upper corner");
    });
    wait(steps, 4);
    steps.push_back([&r] {
        r.check(near(backward(r), Vec3{1, -1, 1} / std::sqrt(3.0), 1e-6),
                "view cube (landscape): the corner is the Iso view", vec(backward(r)));
        r.check(r.tapItem(QStringLiteral("viewCubeFace_Top")), "view cube (landscape): tap its Top face");
    });
    wait(steps, 4);
    steps.push_back([&r, s] {
        r.check(near(backward(r), {0, 0, 1}, 1e-6), "view cube (landscape): Top looks straight down", vec(backward(r)));
        r.check(r.app().sketchTool() == QStringLiteral("line") && sketchItems(r) == s->sketchItems,
                "view cube (landscape): still the Line tool, nothing drawn", QString::number(sketchItems(r)));
        r.screenshot(QStringLiteral("viewcube_06_landscape"));
        r.window()->setProperty("simulatedSafeArea", QVariant());
        r.app().setTouchMode(false);
        const QSize size = r.initialWindowSize();
        r.resizeWindow(size.width(), size.height());
    });
    return steps;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("viewcube"), 11, steps});

} // namespace
} // namespace os::app
