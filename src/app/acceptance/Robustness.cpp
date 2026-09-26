// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Robustness in the real window: hover and click picking (through the
// pick accelerator), a refused fillet whose message names a radius that
// works, a mirror that would change nothing saying why in the hint line,
// and a damaged project file refused with a plain message.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "io/ProjectFile.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <cstdio>
#include <memory>

namespace os::app {
namespace {

struct State {
    double radius = 0;       // the radius the refusal suggested
    double filleted = 0;     // volume after the fillet
    QStringList messages;    // what the app told the user
};

QString itemText(AcceptanceRunner& r, const QString& objectName)
{
    const QQuickItem* item = r.findItem(objectName);
    return item ? item->property("text").toString() : QString();
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto state = std::make_shared<State>();
    QObject::connect(&r.app(), &ui::AppController::message, &r,
                     [state](const QString& text) { state->messages.push_back(text); });
    auto& in = r.app().interaction();
    return {
        [&r] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "robustness: a 20 mm box");
        },
        [] {}, [] {}, [] {}, [] {},
        // Hover: the top face, then just off the top front edge (edges win).
        [&r, &in] {
            r.mouseMove(r.screenPoint(0, 0, 20));
            const auto& hover = in.hover();
            const auto info = hover.kind == sel::PickKind::Face ? geom::faceInfo(r.body(0).shape(), hover.index) : std::nullopt;
            r.check(info && info->normal.z > 0.999, "hovering the top face picks it");
        },
        [&r, &in] {
            r.mouseMove(r.screenPoint(0, -10, 20) + QPointF(0, 3));
            const auto& hover = in.hover();
            const auto info = hover.kind == sel::PickKind::Edge ? geom::edgeInfo(r.body(0).shape(), hover.index) : std::nullopt;
            r.check(info && std::abs(info->midpoint.y + 10) < 1e-6 && std::abs(info->midpoint.z - 20) < 1e-6,
                    "3 px off the top front edge, the edge is picked");
            r.click(r.screenPoint(0, -10, 20) + QPointF(0, 3));
            r.check(r.app().operationTitle() == QStringLiteral("Fillet"), "clicking it offers a fillet", r.app().operationTitle());
        },
        // Too large: the chip says so and names a radius that works.
        [&r] { r.type(QStringLiteral("25")); },
        [&r, state] {
            r.key(Qt::Key_Return);
            const QString error = itemText(r, QStringLiteral("valueChipError"));
            r.check(error.startsWith(QStringLiteral("The radius is too large for this edge. Try ")), "the chip explains the refusal",
                    error);
            std::sscanf(error.toUtf8().constData(), "The radius is too large for this edge. Try %lf mm or less.", &state->radius);
            r.check(state->radius > 15 && state->radius <= 20, "it suggests a radius below the 20 mm side",
                    AcceptanceRunner::num(state->radius));
            r.check(std::abs(r.bodyVolume() - 8000.0) < 1e-6, "nothing was applied", AcceptanceRunner::num(r.bodyVolume()));
        },
        [&r, state] {
            r.key(Qt::Key_Escape); // leave the field; typing starts a new value
            r.type(QString::number(state->radius, 'f', 1));
        },
        [&r, state] {
            r.key(Qt::Key_Return);
            const double rr = state->radius;
            state->filleted = 8000.0 - (rr * rr - kPi * rr * rr / 4) * 20.0;
            r.check(std::abs(r.bodyVolume() - state->filleted) < 1e-3, "the suggested radius works",
                    AcceptanceRunner::num(r.bodyVolume()));
        },
        // Mirror across YZ: the part is symmetric about it, so nothing would change.
        [&r] {
            r.key(Qt::Key_Escape);
            const QString id = QString::fromStdString(r.body(0).id().toString());
            r.check(r.clickItem(QStringLiteral("historyRow_") + id), "Model panel row selects the body");
        },
        [&r] { r.check(r.clickItem(QStringLiteral("tool_mirror")), "Mirror tool button"); },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_plane:0")), "Across YZ"); },
        [] {},
        [&r] {
            r.check(!r.app().operationCanCommit(), "a mirror that changes nothing cannot be applied");
            const QString hint = itemText(r, QStringLiteral("hintText"));
            r.check(hint.startsWith(QStringLiteral("The body is already symmetric about this plane")), "the hint line says why",
                    hint);
            r.check(r.findItem(QStringLiteral("barAction_apply")) == nullptr
                        || !r.findItem(QStringLiteral("barAction_apply"))->isVisible(),
                    "no Apply button");
            r.screenshot(QStringLiteral("robustness_mirror_refused"));
            r.check(r.clickItem(QStringLiteral("barAction_plane:2")), "Across XY instead");
        },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_apply")), "Apply button"); },
        [] {},
        [&r, state] {
            const auto bb = geom::boundingBox(r.body(0).shape());
            r.check(std::abs(r.bodyVolume() - 2 * state->filleted) < 1e-3 && std::abs(bb.size().z - 40) < 1e-6,
                    "mirrored below the ground: twice the volume, 40 mm tall", AcceptanceRunner::num(r.bodyVolume()));
        },
        // A damaged project is refused with a plain message; the model stays.
        [&r, state] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            const QString path = QDir::tempPath() + QStringLiteral("/openshape_acceptance_damaged.openshape");
            QFile::remove(path);
            r.check(r.app().saveProjectAs(QUrl::fromLocalFile(path)), "project saves");
            QFile file(path);
            if (file.open(QIODevice::ReadWrite)) {
                file.resize(file.size() / 2);
                file.close();
            }
            state->messages.clear();
            const double before = r.bodyVolume();
            r.check(!r.app().openProject(QUrl::fromLocalFile(path)), "a half-written project does not open");
            r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - before) < 1e-9, "the open model is untouched");
            r.check(!state->messages.isEmpty() && state->messages.back().contains(QStringLiteral("damaged")),
                    "the user is told the file is damaged", state->messages.isEmpty() ? QString() : state->messages.back());
            QFile::remove(path);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("robustness"), 60, steps});

} // namespace
} // namespace os::app
