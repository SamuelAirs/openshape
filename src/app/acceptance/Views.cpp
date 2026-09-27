// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The view buttons (bottom right): standard views, projection, display unit, fit.

#include "app/AcceptanceRunner.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QSettings>
#include <QtQuick/QQuickWindow>

#include <cmath>

namespace os::app {
namespace {

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto backward = [&r] { return r.app().interaction().camera().backward(); };
    auto vec = [](Vec3 v) { return QStringLiteral("%1 %2 %3").arg(v.x, 0, 'f', 3).arg(v.y, 0, 'f', 3).arg(v.z, 0, 'f', 3); };
    return {
        [&r] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "views: a box to look at");
            r.check(r.clickItem(QStringLiteral("viewTop")), "Top view button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r, backward, vec] {
            r.check(backward().z > 0.999, "Top looks straight down", vec(backward()));
            r.check(r.clickItem(QStringLiteral("viewFront")), "Front view button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r, backward, vec] {
            r.check(backward().y < -0.999, "Front looks along +Y", vec(backward()));
            r.check(r.clickItem(QStringLiteral("viewRight")), "Right view button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r, backward, vec] {
            r.check(backward().x > 0.999, "Right looks along -X", vec(backward()));
            r.check(r.clickItem(QStringLiteral("viewIso")), "Iso view button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r, backward, vec] {
            const Vec3 b = backward();
            r.check(b.x > 0.5 && b.y < -0.5 && b.z > 0.5, "Iso looks from front-right, above", vec(b));
            r.check(r.app().perspective(), "the view is in perspective");
            r.check(r.clickItem(QStringLiteral("viewProjection")), "projection button");
            r.check(!r.app().perspective(), "it switches to orthographic");
            r.check(QSettings().value(QStringLiteral("view/perspective")).toString() == QStringLiteral("false"),
                    "the choice is remembered for the next start");
            r.check(r.clickItem(QStringLiteral("viewProjection")), "projection button again");
            r.check(r.app().perspective(), "and back to perspective");
            r.check(QSettings().value(QStringLiteral("view/perspective")).toString() == QStringLiteral("true"),
                    "that is remembered too");
            r.check(r.clickItem(QStringLiteral("viewUnit")), "unit button");
            r.check(r.app().displayUnit() == QStringLiteral("in"), "it switches to inches", r.app().displayUnit());
            r.check(r.clickItem(QStringLiteral("viewUnit")), "unit button again");
            r.check(r.app().displayUnit() == QStringLiteral("mm"), "and back to millimeters", r.app().displayUnit());
        },
        [&r] {
            // Zoom far out, then Fit brings the box back to fill the view.
            for (int i = 0; i < 12; ++i)
                r.app().interaction().wheel({r.window()->width() / 2.0, r.window()->height() / 2.0}, -1.0);
            r.check(r.clickItem(QStringLiteral("viewFit")), "Fit button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            const QPointF a = r.screenPoint(-10, -10, 0);
            const QPointF b = r.screenPoint(10, 10, 20);
            const double extent = std::hypot(b.x() - a.x(), b.y() - a.y());
            r.check(extent > 0.25 * r.window()->height(), "Fit shows the box large", AcceptanceRunner::num(extent));
            r.screenshot(QStringLiteral("views_fit"));
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("views"), 10, steps});

} // namespace
} // namespace os::app
