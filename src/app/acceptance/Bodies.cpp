// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bodies and copies: Duplicate (value chip, Ctrl+D, Model panel row).

#include "app/AcceptanceRunner.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <optional>

namespace os::app {
namespace {

QString idOf(const doc::Body& body)
{
    return QString::fromStdString(body.id().toString());
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    auto selectedBody = [&r]() -> std::optional<Uuid> {
        const auto& sel = r.app().interaction().selection();
        if (sel.size() != 1 || sel.items()[0].kind != sel::SelectionKind::Body)
            return std::nullopt;
        return sel.items()[0].bodyId;
    };
    return {
        // ---- Duplicate --------------------------------------------------------------
        [&r] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "bodies: B adds a box");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "Model panel row selects the box");
            r.check(r.app().operationTitle() == QStringLiteral("Move"), "the box shows the Move arrows", r.app().operationTitle());
        },
        [&r, num, selectedBody] {
            r.check(r.clickItem(QStringLiteral("action_duplicate")), "Duplicate button beside the arrows");
            r.check(r.app().bodyCount() == 2, "Duplicate adds a body", QString::number(r.app().bodyCount()));
            const doc::Body& copy = r.body(1);
            r.check(copy.name() == "Body 1 copy", "named 'Body 1 copy'", QString::fromStdString(copy.name()));
            r.check(std::abs(geom::volume(copy.shape()) - geom::volume(r.body(0).shape())) < 1e-6,
                    "the copy has the source's volume", num(geom::volume(copy.shape())));
            r.check(selectedBody() == copy.id() && r.app().operationTitle() == QStringLiteral("Move"),
                    "the copy is selected with the Move arrows");
            r.screenshot(QStringLiteral("bodies_01_duplicated"));
            r.type(QStringLiteral("30"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            const double copyMin = geom::boundingBox(r.body(1).shape()).min.x;
            r.check(std::abs(copyMin - 20.0) < 1e-6, "typing 30 moves the copy away", num(copyMin));
            r.check(std::abs(geom::boundingBox(r.body(0).shape()).min.x + 10.0) < 1e-6, "the source stays put");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 1, "two undos: the move, then the duplicate", QString::number(r.app().bodyCount()));
            r.key(Qt::Key_Y, Qt::ControlModifier);
            r.key(Qt::Key_Y, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2 && std::abs(geom::boundingBox(r.body(1).shape()).min.x - 20.0) < 1e-6,
                    "redo brings the moved copy back");
        },
        [&r] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "Model panel row selects the box again");
        },
        [&r, selectedBody] {
            r.key(Qt::Key_D, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 3, "Ctrl+D duplicates the selected body", QString::number(r.app().bodyCount()));
            r.check(r.app().bodyCount() == 3 && r.body(2).name() == "Body 1 copy 2", "the next copy is 'Body 1 copy 2'");
            r.check(r.app().bodyCount() == 3 && selectedBody() == r.body(2).id(), "and it is selected");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2, "undo removes it");
        },
        [] {}, [] {}, [] {}, // another click on the same row now would be a double-click
        [&r] {
            // The row's actions show while it is expanded.
            QQuickItem* button = r.findItem(QStringLiteral("historyDuplicate_") + idOf(r.body(0)));
            if (!button || !button->isVisible())
                r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0)));
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("historyDuplicate_") + idOf(r.body(0))), "Duplicate in the body's Model panel row");
            r.check(r.app().bodyCount() == 3, "the row's Duplicate adds a body", QString::number(r.app().bodyCount()));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2, "undo removes it");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("bodies"), 40, steps});

} // namespace
} // namespace os::app
