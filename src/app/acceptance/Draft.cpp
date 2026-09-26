// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Extrude with a draft: a sketched square extruded 10 mm with the Draft
// action and a typed angle becomes an exact pyramid frustum.

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <cmath>

namespace os::app {
namespace {

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    return {
        [&r] {
            r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            r.check(r.app().sketchMode(), "draft: K starts a sketch");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(0, 0, 0));
            r.mouseMove(r.screenPoint(12, 12, 0));
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Return);
            r.check(r.clickItem(QStringLiteral("finishSketchButton")), "draft: Finish sketch button");
        },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(10, 10, 0));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "draft: the square is selected", r.app().operationTitle());
            r.type(QStringLiteral("10"));
        },
        [&r] { r.check(r.clickItem(QStringLiteral("action_draft")), "draft: Draft action in the value chip"); },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("Draft"), "draft: the chip edits the draft",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("5"));
        },
        [&r] {
            r.check(r.app().operationValueText() == QString::fromUtf8("5.0\xC2\xB0"), "draft: 5 degrees", r.app().operationValueText());
            r.screenshot(QStringLiteral("draft_preview"));
            r.key(Qt::Key_Return);
        },
        [&r] {
            r.check(r.app().bodyCount() == 1, "draft: extruded into a body");
            const double t = 10 * std::tan(5 * kPi / 180), top = (20 - 2 * t) * (20 - 2 * t);
            const double expected = 10.0 / 3 * (400 + top + std::sqrt(400 * top));
            r.check(std::abs(r.bodyVolume() - expected) < 1e-3, "draft: an exact pyramid frustum",
                    AcceptanceRunner::num(r.bodyVolume()) + QStringLiteral(" vs ") + AcceptanceRunner::num(expected));
            r.check(std::abs(r.bodyHeight() - 10) < 1e-6, "draft: 10 mm tall", AcceptanceRunner::num(r.bodyHeight()));
            r.screenshot(QStringLiteral("draft"));
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("extrude_draft"), 72, steps});

} // namespace
} // namespace os::app
