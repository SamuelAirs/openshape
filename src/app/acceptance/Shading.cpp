// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// How bodies look from any angle, measured on the rendered window: a cube, a
// cube with a rounded edge and a thin plate are looked at from the
// isometric, front, right and top views and from orbiting, low, high and
// underneath angles, in perspective and orthographic. At each view the
// brightness of every face facing the viewer is sampled at its center and
// checked: faces that meet at an edge have clearly different shades, a top
// is lighter than the sides and an underside darker, nothing (the rounding's
// highlight included) is black or washed out into the background. The
// numbers go to the log ("shading ...").

#include "app/AcceptanceRunner.h"
#include "core/Log.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtGui/QImage>
#include <QtQuick/QQuickWindow>

#include <algorithm>
#include <cmath>
#include <memory>

namespace os::app {
namespace {

using Steps = std::vector<AcceptanceRunner::Step>;

// Brightness steps on a 0-255 scale (luma of the rendered sRGB colour).
constexpr double kMinAdjacentContrast = 16; // faces meeting at a sharp edge
constexpr double kTopMargin = 8;             // a top above every side
constexpr double kDarkest = 70;              // no face this dark
constexpr double kBrightest = 229;           // the background is ~237

struct Probe {
    QString name;
    Vec3 point;  // on the face, away from edges
    Vec3 normal; // outward
};

struct Model {
    QString name;
    std::vector<Probe> faces;
    std::vector<std::pair<int, int>> sharp; // faces meeting at a sharp edge
};

struct View {
    QString name;
    double yawDeg, pitchDeg;
};

const std::vector<View>& views()
{
    static const std::vector<View> v{
        {QStringLiteral("iso"), -45, 35.26},       {QStringLiteral("front"), -90, 0},
        {QStringLiteral("right"), 0, 0},           {QStringLiteral("top"), -90, 90},
        {QStringLiteral("orbit-left"), -135, 35},  {QStringLiteral("orbit-back"), 60, 30},
        {QStringLiteral("orbit-back-left"), 150, 40}, {QStringLiteral("low"), -20, 8},
        {QStringLiteral("high"), 25, 65},          {QStringLiteral("below"), -60, -30},
    };
    return v;
}

Model cube()
{
    Model m{QStringLiteral("cube"),
            {{QStringLiteral("top"), {0, 0, 20}, {0, 0, 1}},
             {QStringLiteral("bottom"), {0, 0, 0}, {0, 0, -1}},
             {QStringLiteral("front"), {0, -10, 10}, {0, -1, 0}},
             {QStringLiteral("back"), {0, 10, 10}, {0, 1, 0}},
             {QStringLiteral("right"), {10, 0, 10}, {1, 0, 0}},
             {QStringLiteral("left"), {-10, 0, 10}, {-1, 0, 0}}},
            {}};
    for (int a = 0; a < 6; ++a)
        for (int b = a + 1; b < 6; ++b)
            if (b != a + 1 || a % 2 == 1) // not opposite (top/bottom, front/back, right/left)
                m.sharp.push_back({a, b});
    return m;
}

// The cube with its top front edge rounded (R 5).
Model rounded()
{
    const double s = std::sqrt(0.5);
    Model m{QStringLiteral("rounded"),
            {{QStringLiteral("top"), {0, 2.5, 20}, {0, 0, 1}},
             {QStringLiteral("front"), {0, -10, 7.5}, {0, -1, 0}},
             {QStringLiteral("right"), {10, 0, 10}, {1, 0, 0}},
             {QStringLiteral("left"), {-10, 0, 10}, {-1, 0, 0}},
             {QStringLiteral("rounding"), {0, -5 - 5 * s, 15 + 5 * s}, {0, -s, s}},
             {QStringLiteral("back"), {0, 10, 10}, {0, 1, 0}}},
            {{0, 2}, {0, 3}, {1, 2}, {1, 3}, {0, 5}, {2, 5}, {3, 5}}};
    return m;
}

// The cube pushed down to a 5 mm plate.
Model plate()
{
    return {QStringLiteral("plate"),
            {{QStringLiteral("top"), {0, 0, 5}, {0, 0, 1}},
             {QStringLiteral("front"), {0, -10, 2.5}, {0, -1, 0}},
             {QStringLiteral("right"), {10, 0, 2.5}, {1, 0, 0}},
             {QStringLiteral("left"), {-10, 0, 2.5}, {-1, 0, 0}},
             {QStringLiteral("back"), {0, 10, 2.5}, {0, 1, 0}},
             {QStringLiteral("bottom"), {0, 0, 0}, {0, 0, -1}}},
            {{0, 1}, {0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {2, 4}, {3, 4}, {5, 1}, {5, 2}, {5, 3}, {5, 4}}};
}

// Mean luma of the device pixels around a window point, or -1 off screen.
double luma(const QImage& image, QPointF logical)
{
    const double dpr = image.devicePixelRatio();
    const int cx = int(std::lround(logical.x() * dpr)), cy = int(std::lround(logical.y() * dpr));
    constexpr int r = 3;
    if (cx - r < 0 || cy - r < 0 || cx + r >= image.width() || cy + r >= image.height())
        return -1;
    double sum = 0;
    for (int y = cy - r; y <= cy + r; ++y)
        for (int x = cx - r; x <= cx + r; ++x) {
            const QColor c = image.pixelColor(x, y);
            sum += 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
        }
    return sum / ((2 * r + 1) * (2 * r + 1));
}

struct Totals {
    double minContrast = 1e9; // over all views and sharp pairs
    int measured = 0;
};

// Samples every face that faces the viewer at the current camera and checks
// the rules above.
void measure(AcceptanceRunner& r, const Model& model, const QString& viewName, const QString& projection, Totals& totals)
{
    const Camera& camera = r.app().interaction().camera();
    const QImage image = r.window()->grabWindow();
    std::vector<double> shade(model.faces.size(), -1);
    QStringList parts;
    for (std::size_t i = 0; i < model.faces.size(); ++i) {
        const Probe& f = model.faces[i];
        const Vec3 toViewer = camera.projection == Camera::Projection::Perspective ? (camera.eye() - f.point).normalized()
                                                                                    : camera.backward();
        if (f.normal.dot(toViewer) < 0.3)
            continue; // edge-on or facing away: too small to judge
        const Vec2 p = camera.project(f.point);
        shade[i] = luma(image, {p.x, p.y});
        if (shade[i] >= 0)
            parts << QStringLiteral("%1 %2").arg(f.name).arg(shade[i], 0, 'f', 0);
    }
    const QString where = QStringLiteral("%1 %2 %3").arg(model.name, viewName, projection);
    double minContrast = 1e9;
    for (const auto& [a, b] : model.sharp) {
        if (shade[a] < 0 || shade[b] < 0)
            continue;
        const double d = std::abs(shade[a] - shade[b]);
        minContrast = std::min(minContrast, d);
        r.check(d >= kMinAdjacentContrast,
                QStringLiteral("shading %1: %2 and %3 differ").arg(where, model.faces[a].name, model.faces[b].name),
                AcceptanceRunner::num(d));
    }
    for (std::size_t i = 0; i < model.faces.size(); ++i) {
        if (shade[i] < 0)
            continue;
        const Probe& f = model.faces[i];
        r.check(shade[i] >= kDarkest && shade[i] <= kBrightest,
                QStringLiteral("shading %1: %2 is neither black nor washed out").arg(where, f.name), AcceptanceRunner::num(shade[i]));
        for (std::size_t j = 0; j < model.faces.size(); ++j) {
            if (shade[j] < 0 || std::abs(model.faces[j].normal.z) > 0.01)
                continue; // j is a side
            if (f.normal.z > 0.999)
                r.check(shade[i] >= shade[j] + kTopMargin,
                        QStringLiteral("shading %1: %2 is lighter than %3").arg(where, f.name, model.faces[j].name),
                        AcceptanceRunner::num(shade[i] - shade[j]));
            if (f.normal.z < -0.999)
                r.check(shade[i] < shade[j],
                        QStringLiteral("shading %1: %2 is darker than %3").arg(where, f.name, model.faces[j].name),
                        AcceptanceRunner::num(shade[j] - shade[i]));
        }
    }
    if (minContrast < 1e9) {
        totals.minContrast = std::min(totals.minContrast, minContrast);
        parts << QStringLiteral("(min adjacent contrast %1)").arg(minContrast, 0, 'f', 0);
    }
    ++totals.measured;
    OS_LOG(Info, App) << "shading " << where.toStdString() << ": " << parts.join(QStringLiteral(", ")).toStdString();
}

// Looks at the model from every view in both projections.
void measureAllViews(Steps& steps, AcceptanceRunner& r, const Model& model, const std::shared_ptr<Totals>& totals)
{
    for (const auto projection : {Camera::Projection::Perspective, Camera::Projection::Orthographic}) {
        const QString projectionName =
            projection == Camera::Projection::Perspective ? QStringLiteral("perspective") : QStringLiteral("orthographic");
        for (const View& view : views()) {
            steps.push_back([&r, projection, view] {
                auto& in = r.app().interaction();
                in.setProjection(projection);
                in.setViewAngles(view.yawDeg * kPi / 180, view.pitchDeg * kPi / 180, false);
                in.fitAll(false);
            });
            steps.push_back([&r, model, view, projectionName, totals] { measure(r, model, view.name, projectionName, *totals); });
        }
    }
    steps.push_back([&r] {
        auto& in = r.app().interaction();
        in.setViewAngles(-45 * kPi / 180, 35.26 * kPi / 180, false);
        in.fitAll(false);
    });
    steps.push_back([&r, model] { r.screenshot(QStringLiteral("shading_") + model.name); });
}

Steps steps(AcceptanceRunner& r)
{
    auto totals = std::make_shared<Totals>();
    // The pointer rests on the title bar, so no face is hovered (tinted).
    auto park = [&r] { r.mouseMove(QPointF(60, 40)); };
    Steps s;
    s.push_back([&r, park] {
        r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
        r.check(r.app().bodyCount() == 1, "shading: a cube to look at");
        park();
    });
    for (int i = 0; i < 4; ++i)
        s.push_back([] {});
    measureAllViews(s, r, cube(), totals);

    // Round the top front edge (click it, type 5, Enter).
    s.push_back([&r] {
        r.app().setView(QStringLiteral("iso"));
        r.app().interaction().skipAnimation();
        r.app().interaction().fitAll(false);
    });
    s.push_back([&r] { r.click(r.screenPoint(0, -10, 20)); });
    s.push_back([&r] {
        r.check(r.app().operationTitle() == QStringLiteral("Fillet"), "shading: the edge offers a fillet", r.app().operationTitle());
        r.type(QStringLiteral("5"));
    });
    s.push_back([&r] { r.key(Qt::Key_Return); });
    s.push_back([&r, park] {
        r.check(r.body(0).features().size() == 2, "shading: the top front edge is rounded");
        park();
    });
    measureAllViews(s, r, rounded(), totals);

    // Undo the rounding; push the top down to a 5 mm plate.
    s.push_back([&r] {
        r.key(Qt::Key_Z, Qt::ControlModifier);
        r.app().setView(QStringLiteral("iso"));
        r.app().interaction().skipAnimation();
        r.app().interaction().fitAll(false);
    });
    s.push_back([&r] { r.click(r.screenPoint(0, 0, 20)); });
    s.push_back([&r] { r.type(QStringLiteral("5")); });
    s.push_back([&r] { r.key(Qt::Key_Return); });
    s.push_back([&r, park] {
        r.key(Qt::Key_Escape);
        r.key(Qt::Key_Escape);
        r.check(std::abs(r.bodyHeight() - 5) < 1e-6, "shading: the cube is now a 5 mm plate", AcceptanceRunner::num(r.bodyHeight()));
        park();
    });
    measureAllViews(s, r, plate(), totals);
    s.push_back([&r, totals] {
        OS_LOG(Info, App) << "shading: " << totals->measured << " views measured, smallest contrast between faces meeting at an edge "
                          << totals->minContrast;
        r.check(totals->measured == 3 * 2 * int(views().size()), "shading: every view measured");
    });
    return s;
}

const bool registered = registerAcceptanceScenario({QStringLiteral("shading"), 12, steps});

} // namespace
} // namespace os::app
