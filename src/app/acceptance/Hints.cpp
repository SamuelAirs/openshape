// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The hint line says what to do next: here while the very first body is
// extruded (the empty model's hints once hid the extrude hints).

#include "app/AcceptanceRunner.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickItem>

#include <cmath>

namespace os::app {
namespace {

QString hint(AcceptanceRunner& r)
{
    QQuickItem* line = r.findItem(QStringLiteral("hintText"));
    return line ? line->property("text").toString() : QString();
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    return {
        [&r] {
            r.check(hint(r).contains(QStringLiteral("Add a box")), "an empty model suggests a box or a sketch", hint(r));
            r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(0, 0, 0));
            r.mouseMove(r.screenPoint(30, 15, 0));
            r.type(QStringLiteral("40"));
            r.key(Qt::Key_Tab);
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Return);
            r.check(r.clickItem(QStringLiteral("finishSketchButton")), "hints: Finish sketch");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.check(hint(r).contains(QStringLiteral("Click inside a closed sketch shape")),
                    "a sketch without bodies: click inside a shape to extrude it", hint(r));
            r.click(r.screenPoint(20, 10, 0));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "hints: the profile arms Extrude", r.app().operationTitle());
            r.check(hint(r).contains(QStringLiteral("Up to face")), "the first extrusion shows the extrude hint", hint(r));
            r.type(QStringLiteral("10"));
        },
        [&r] {
            r.check(hint(r).contains(QStringLiteral("Enter to apply")) && !hint(r).contains(QStringLiteral("Click inside")),
                    "with a typed distance it says how to apply, not how to start", hint(r));
            r.key(Qt::Key_Return);
            r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - 8000.0) < 1e-6, "hints: the extrusion is 40 x 20 x 10",
                    AcceptanceRunner::num(r.bodyVolume()));
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("hints"), 15, steps});

} // namespace
} // namespace os::app
