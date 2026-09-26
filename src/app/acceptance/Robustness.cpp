// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Robustness in the real window: hover and click picking (through the
// pick accelerator), a refused fillet whose message names a radius that
// works, a mirror that would change nothing saying why in the hint line,
// a damaged project file refused with a plain message, and a circle
// beside a body pushed in becoming a new body instead of an empty cut.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/Operation.h"
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
        // A circle beside the box, pushed in: the automatic cut would remove
        // nothing, so it becomes a new body (as a join that misses does).
        [&r] {
            r.app().newDocument();
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "a new 20 mm box to sketch beside");
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(0, 0, 20));
            r.key(Qt::Key_K, Qt::NoModifier, QStringLiteral("k"));
            r.check(r.app().sketchMode(), "K on the top face starts a sketch on it");
        },
        [] {}, [] {}, [] {}, [] {}, // camera turns to face the sketch plane
        [&r] {
            r.key(Qt::Key_C, Qt::NoModifier, QStringLiteral("c"));
            const QPointF center = r.screenPoint(17, 0, 20);
            r.check(r.window()->contentItem()->contains(center), "the point beside the box is on screen",
                    QStringLiteral("%1, %2").arg(center.x()).arg(center.y()));
            r.click(center);
            r.mouseMove(r.screenPoint(19, 0, 20));
            r.type(QStringLiteral("6"));
            r.key(Qt::Key_Return);
            const auto* session = r.app().interaction().sketchSession();
            r.check(session && session->sketch().circles().size() == 1, "a 6 mm circle beside the box");
            r.check(r.clickItem(QStringLiteral("finishSketchButton")), "Finish sketch button");
        },
        [] {}, [] {}, [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("viewIso")), "Iso view button"); },
        [] {}, [] {}, [] {}, [] {},
        [&r] {
            r.click(r.screenPoint(17, 0, 20));
            r.check(r.app().operationTitle() == QStringLiteral("Extrude"), "clicking the circle offers extrude",
                    r.app().operationTitle());
        },
        // Drag the arrow down, beside the box.
        [&r, &in] {
            const auto* op = in.operation();
            r.check(op != nullptr, "an extrude to drag");
            if (!op)
                return;
            const Vec3 anchor = op->anchor();
            const double px = in.camera().pixelSize(anchor);
            const interact::ArrowStyle style;
            const Vec3 grab = anchor + op->manipulator().direction() * ((style.gapPx + style.shaftPx * 0.6) * px);
            const QPointF from = r.screenPoint(grab.x, grab.y, grab.z);
            r.drag(from, from + QPointF(0, 80));
            const auto* extrude = dynamic_cast<const interact::ExtrudeOperation*>(in.operation());
            r.check(extrude && extrude->value() < -1.0, "dragging the arrow down pushes the circle in",
                    extrude ? AcceptanceRunner::num(extrude->value()) : QStringLiteral("no extrude"));
            r.check(extrude && extrude->mode() == doc::ExtrudeMode::NewBody,
                    "a cut that would remove nothing becomes a new body");
            r.check(r.app().operationCanCommit(), "it can be applied");
            r.check(r.app().bodyCount() == 1, "the drag only previews");
            r.screenshot(QStringLiteral("robustness_cut_beside_becomes_body"));
        },
        [&r] {
            r.key(Qt::Key_Return);
            r.check(r.app().bodyCount() == 2, "Enter adds a second body");
            if (r.app().bodyCount() != 2)
                return;
            const auto pin = geom::boundingBox(r.body(1).shape());
            r.check(std::abs(pin.max.z - 20) < 1e-6 && pin.min.z < 19, "the new body hangs below the sketch plane",
                    AcceptanceRunner::num(pin.min.z) + QStringLiteral(" .. ") + AcceptanceRunner::num(pin.max.z));
            r.check(std::abs(pin.min.x - 14) < 1e-6 && std::abs(pin.max.x - 20) < 1e-6, "beside the box",
                    AcceptanceRunner::num(pin.min.x) + QStringLiteral(" .. ") + AcceptanceRunner::num(pin.max.x));
            r.check(std::abs(geom::volume(r.body(0).shape()) - 8000.0) < 1e-6, "the box is untouched",
                    AcceptanceRunner::num(geom::volume(r.body(0).shape())));
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("robustness"), 60, steps});

} // namespace
} // namespace os::app
