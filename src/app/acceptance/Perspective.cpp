// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The perspective view (the default) and the ground, with real input:
// a new window starts in perspective; Fit frames the box, which casts a soft
// contact shadow on the ground (darker right at the box); the wheel zooms
// towards the face under the pointer and keeps that point in place; an orbit
// turns about the point pressed on; a face picked in perspective pushes to
// a typed height; the Top view looks straight down; a sketch faces its plane
// head-on (undistorted) and stays in perspective. The grid fades out: along
// one of its lines, seen low over the ground, the line's darkness falls
// smoothly to nothing before the grid's edge, in perspective and
// orthographic (it used to stop at a hard border).

#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtGui/QImage>
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

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

// Mean luma of the device pixels in a (2r+1)-wide horizontal run at a window
// point, or the darkest single pixel of it; -1 off the image.
double luma(const QImage& image, QPointF logical, int r, bool darkest)
{
    const double dpr = image.devicePixelRatio();
    const int cx = int(std::lround(logical.x() * dpr)), cy = int(std::lround(logical.y() * dpr));
    if (cx - r < 0 || cy < 0 || cx + r >= image.width() || cy >= image.height())
        return -1;
    double sum = 0, least = 1e9;
    for (int x = cx - r; x <= cx + r; ++x) {
        const QColor c = image.pixelColor(x, cy);
        const double l = 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
        sum += l;
        least = std::min(least, l);
    }
    return darkest ? least : sum / (2 * r + 1);
}

// The ground in front of the box's front face (y = -10, the box resting on
// z = 0): darker right at the face (the contact shadow), plain a few
// millimetres out. Samples sit in the middle of grid cells, off the lines.
void checkContactShadow(AcceptanceRunner& r)
{
    const interact::RenderGrid grid = r.app().interaction().renderScene().grid;
    const QImage image = r.window()->grabWindow();
    const double minor = grid.minorStep;
    const double x = (std::floor(4.0 / minor) + 0.5) * minor; // a cell center, off the Y axis
    auto groundAt = [&](double gap) { return luma(image, r.screenPoint(x, -10 - gap, 0), 1, false); };
    // At the face the grid line y = -10 coincides with the box's edge: start
    // a few pixels out.
    const double px = r.app().interaction().camera().pixelSize({x, -10, 0});
    const double nearGap = 5 * px;
    const double farGap = (std::floor((10 + 6.0) / minor) + 0.5) * minor - 10; // a cell center ~6 mm out
    const double nearLevel = groundAt(nearGap), farLevel = groundAt(farGap);
    OS_LOG(Info, App) << "contact shadow: ground " << nearGap << " mm from the box " << nearLevel << ", " << farGap << " mm out "
                      << farLevel;
    r.check(nearLevel >= 0 && farLevel >= 0 && farLevel - nearLevel >= 3, "perspective: a soft shadow where the box meets the ground",
            AcceptanceRunner::num(farLevel - nearLevel));
    r.check(farLevel - nearLevel <= 40, "perspective: a subtle one", AcceptanceRunner::num(farLevel - nearLevel));
}

struct State {
    QPointF zoomCursor;
    double zoomDistance = 0;
    QPointF orbitFrom;
    Vec3 orbitPivot;
    double orbitYaw = 0;
};

// Looks low over the empty ground and follows one major grid line away from
// the viewer: its darkness must fade out smoothly before the grid's radius.
void checkGridFades(AcceptanceRunner& r, const QString& projection)
{
    const auto& in = r.app().interaction();
    const interact::RenderGrid grid = in.renderScene().grid;
    const QImage image = r.window()->grabWindow();
    const double major = grid.majorStep, minor = grid.minorStep, radius = grid.radius;
    QQuickItem* viewport = r.findItem(QStringLiteral("viewport"));
    QRectF card; // the empty view's "Start with a shape" card
    if (QQuickItem* item = r.findItem(QStringLiteral("emptyState")); item && item->isVisible())
        card = item->mapRectToScene(QRectF(0, 0, item->width(), item->height())).adjusted(-12, -12, 12, 12);
    auto clear = [&](QPointF p) { return r.itemAt(p) == viewport && !card.contains(p); };
    // Darkness of the major line x = const at distances along it (in grid
    // radii from the center): its darkest pixel against the ground halfway to
    // the next minor line, in the same row. Samples lie halfway between the
    // lines across it (y = k minor), which would darken both; points under
    // panels are skipped.
    auto follow = [&](double x) {
        std::vector<std::pair<double, double>> out;
        const int first = int(std::floor(-0.3 * radius / minor)), last = int(std::ceil(1.25 * radius / minor));
        for (int k = first; k <= last; ++k) {
            const double y = grid.center.y + (k + 0.5) * minor;
            const double s = (y - grid.center.y) / radius;
            const QPointF onLine = r.screenPoint(x, y, 0);
            const QPointF beside = r.screenPoint(x + minor * 0.5, y, 0);
            if (!clear(onLine) || !clear(beside))
                continue;
            const double line = luma(image, onLine, 3, true), background = luma(image, beside, 1, false);
            if (line >= 0 && background >= 0)
                out.push_back({s, std::max(0.0, background - line)});
        }
        return out;
    };
    // The first major line (not the Y axis) with enough of it on screen.
    std::vector<std::pair<double, double>> darkness;
    for (const int k : {1, -1, 2, -2, 3, -3}) {
        const double x = grid.center.x + k * major;
        if (std::abs(x) < minor * 0.5)
            continue;
        darkness = follow(x);
        if (darkness.size() > 60)
            break;
    }
    // A median of five neighbours evens out single pixels of anti-aliasing.
    std::vector<std::pair<double, double>> smooth = darkness;
    for (std::size_t i = 2; i + 2 < darkness.size(); ++i) {
        double window[5];
        for (int k = 0; k < 5; ++k)
            window[k] = darkness[i + k - 2].second;
        std::nth_element(window, window + 2, window + 5);
        smooth[i].second = window[2];
    }
    double nearMax = 0, farMax = 0, biggestStep = 0, farthest = -1;
    for (std::size_t i = 0; i < smooth.size(); ++i) {
        const auto& [s, d] = smooth[i];
        if (std::abs(s) < 0.35)
            nearMax = std::max(nearMax, d);
        if (s > 1.02)
            farMax = std::max(farMax, d);
        farthest = std::max(farthest, s);
        if (i > 0 && s - smooth[i - 1].first < 0.035)
            biggestStep = std::max(biggestStep, std::abs(d - smooth[i - 1].second));
    }
    darkness = smooth;
    QStringList profile;
    for (std::size_t i = 0; i < darkness.size(); i += 5)
        profile << QStringLiteral("%1:%2").arg(darkness[i].first, 0, 'f', 2).arg(darkness[i].second, 0, 'f', 0);
    OS_LOG(Info, App) << "grid fade " << projection.toStdString() << " (distance/radius: darkness) " << profile.join(QLatin1Char(' ')).toStdString();
    r.check(darkness.size() > 60, QStringLiteral("grid %1: the line is on screen").arg(projection), QString::number(darkness.size()));
    r.check(farthest > 1.02, QStringLiteral("grid %1: the view reaches past the grid's edge").arg(projection), AcceptanceRunner::num(farthest));
    r.check(nearMax >= 6, QStringLiteral("grid %1: the line shows near the middle").arg(projection), AcceptanceRunner::num(nearMax));
    r.check(farMax <= 2, QStringLiteral("grid %1: nothing is left past the edge").arg(projection), AcceptanceRunner::num(farMax));
    r.check(biggestStep <= 8, QStringLiteral("grid %1: it fades smoothly (no hard border)").arg(projection), AcceptanceRunner::num(biggestStep));
}

Steps steps(AcceptanceRunner& r)
{
    auto state = std::make_shared<State>();
    auto camera = [&r]() -> const Camera& { return r.app().interaction().camera(); };
    Steps s;
    s.push_back([&r] {
        r.check(r.startedInPerspective(), "perspective: a new window starts in perspective");
        r.check(r.app().perspective(), "perspective: the view is in perspective");
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
    });
    wait(s, 4);
    // Fit: the whole box on screen, large.
    s.push_back([&r] {
        r.check(r.clickItem(QStringLiteral("viewFit")), "perspective: Fit button");
    });
    wait(s, 4);
    s.push_back([&r, state, camera] {
        bool inside = true;
        double left = 1e9, right = -1e9, top = 1e9, bottom = -1e9;
        for (int c = 0; c < 8; ++c) {
            const QPointF p = r.screenPoint(c & 1 ? 10 : -10, c & 2 ? 10 : -10, c & 4 ? 20 : 0);
            inside = inside && p.x() > 0 && p.y() > 0 && p.x() < r.window()->width() && p.y() < r.window()->height();
            left = std::min(left, p.x());
            right = std::max(right, p.x());
            top = std::min(top, p.y());
            bottom = std::max(bottom, p.y());
        }
        r.check(inside, "perspective: Fit shows the whole box");
        r.check(bottom - top > 0.3 * r.window()->height(), "perspective: and large", AcceptanceRunner::num(bottom - top));
        r.mouseMove({60, 40}); // off the view: nothing hovered
        checkContactShadow(r);
        // Zoom with the wheel over the front face, away from the middle.
        const Vec3 onFace{6, -10, 14};
        state->zoomCursor = r.screenPoint(onFace.x, onFace.y, onFace.z);
        state->zoomDistance = (camera().eye() - onFace).length();
        r.wheel(state->zoomCursor, 3);
    });
    s.push_back([&r, state, camera] {
        const Vec3 onFace{6, -10, 14};
        const QPointF now = r.screenPoint(onFace.x, onFace.y, onFace.z);
        r.check(distance(now, state->zoomCursor) < 1.0, "perspective: the wheel keeps the face point under the pointer",
                AcceptanceRunner::num(distance(now, state->zoomCursor)));
        const double expected = state->zoomDistance * std::pow(0.85, 3);
        const double actual = (camera().eye() - onFace).length();
        r.check(std::abs(actual - expected) < 0.01 * expected, "perspective: and closes in on it by the zoom factor",
                AcceptanceRunner::num(actual) + QStringLiteral(" vs ") + AcceptanceRunner::num(expected));
        r.check(r.clickItem(QStringLiteral("viewFit")), "perspective: Fit again");
    });
    wait(s, 4);
    // Orbit by dragging from a point on the right face: that point stays put.
    s.push_back([&r, state, camera] {
        state->orbitPivot = {10, 3, 8};
        state->orbitFrom = r.screenPoint(state->orbitPivot.x, state->orbitPivot.y, state->orbitPivot.z);
        state->orbitYaw = camera().yaw;
        r.drag(state->orbitFrom, state->orbitFrom + QPointF(70, 30));
        const QPointF now = r.screenPoint(state->orbitPivot.x, state->orbitPivot.y, state->orbitPivot.z);
        r.check(std::abs(camera().yaw - state->orbitYaw) > 0.1, "perspective: dragging orbits");
        r.check(distance(now, state->orbitFrom) < 1.5, "perspective: about the point pressed on",
                AcceptanceRunner::num(distance(now, state->orbitFrom)));
        r.check(r.clickItem(QStringLiteral("viewIso")), "perspective: Iso button");
    });
    wait(s, 4);
    // Pick the top face and push it to a typed height.
    s.push_back([&r] { r.click(r.screenPoint(0, 0, 20)); });
    s.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "perspective: clicking the top face offers push/pull",
                r.app().operationTitle());
        const auto scene = r.app().interaction().renderScene();
        r.check(scene.arrows.size() == 1, "perspective: one arrow");
        if (scene.arrows.size() == 1) {
            // Sized in screen pixels at its anchor.
            const auto& arrow = scene.arrows.front();
            const interact::ArrowStyle style;
            const double px = r.app().interaction().camera().pixelSize(arrow.anchor);
            const Vec3 tip = arrow.anchor + arrow.direction * (style.totalPx() * px);
            const double onScreen = distance(r.screenPoint(arrow.anchor.x, arrow.anchor.y, arrow.anchor.z), r.screenPoint(tip.x, tip.y, tip.z));
            r.check(onScreen > 0.5 * style.totalPx() && onScreen < 1.2 * style.totalPx(), "perspective: the arrow keeps its size on screen",
                    AcceptanceRunner::num(onScreen));
        }
        r.type(QStringLiteral("35"));
    });
    s.push_back([&r] {
        r.key(Qt::Key_Return);
        r.check(std::abs(r.bodyHeight() - 35) < 1e-9, "perspective: typed 35 mm, the box is 35 mm tall", AcceptanceRunner::num(r.bodyHeight()));
        r.check(r.clickItem(QStringLiteral("viewTop")), "perspective: Top button");
    });
    wait(s, 4);
    s.push_back([&r, camera] {
        r.check(camera().backward().z > 0.999, "perspective: Top looks straight down");
        r.check(r.app().perspective(), "perspective: still in perspective");
        // Sketch on the (selected) top face.
        r.check(r.clickItem(QStringLiteral("sketchButton")), "perspective: Sketch on the top face");
    });
    wait(s, 4);
    s.push_back([&r, camera] {
        r.check(r.app().sketchMode(), "perspective: sketching");
        r.check(r.app().perspective(), "perspective: the sketch stays in perspective");
        r.check(camera().backward().z > 0.999, "perspective: facing the sketch plane head-on");
        // Head-on, the plane is not distorted: 10 mm along X and along Y
        // are equally long on screen, here and at a corner of the face.
        for (const Vec3 at : {Vec3{0, 0, 35}, Vec3{-8, 8, 35}}) {
            const double alongX = distance(r.screenPoint(at.x, at.y, at.z), r.screenPoint(at.x + 10, at.y, at.z));
            const double alongY = distance(r.screenPoint(at.x, at.y, at.z), r.screenPoint(at.x, at.y + 10, at.z));
            r.check(std::abs(alongX - alongY) < 0.005 * alongX, "perspective: the sketch plane is undistorted",
                    AcceptanceRunner::num(alongX) + QStringLiteral(" / ") + AcceptanceRunner::num(alongY));
        }
        r.check(r.clickItem(QStringLiteral("finishSketchButton")), "perspective: Finish sketch");
    });
    wait(s, 4);
    // The grid, over the empty ground: undo the push and the box.
    s.push_back([&r] {
        for (int i = 0; i < 4 && r.app().bodyCount() > 0; ++i)
            r.key(Qt::Key_Z, Qt::ControlModifier);
        r.check(r.app().bodyCount() == 0, "grid: the ground is empty");
        r.mouseMove({60, 40}); // off the view
    });
    for (const bool perspective : {true, false}) {
        const QString name = perspective ? QStringLiteral("perspective") : QStringLiteral("orthographic");
        s.push_back([&r, perspective] {
            r.app().setPerspective(perspective);
            r.app().interaction().setViewAngles(-kPi / 2, 15 * kPi / 180, false);
            r.app().interaction().fitAll(false);
        });
        s.push_back([&r, name] {
            checkGridFades(r, name);
            r.screenshot(QStringLiteral("grid_") + name);
        });
    }
    return s;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("perspective"), 11, steps});

} // namespace
} // namespace os::app
