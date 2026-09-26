// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Previews off the GUI thread (TD-1) in the real window: on a vented tray
// (119 faces, where a push/pull preview keeps the kernel busy for a while)
// the push/pull arrow is dragged through the window's input path;
// no pointer move waits for the kernel, the preview arrives afterwards and
// Enter applies it. Then Ctrl+Z and Enter while a preview is still being
// computed, and a refused fillet whose message comes back from the worker.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/Operation.h"
#include "sketch/Sketch.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

// The middle of the tray's front rim (the wall is 2 mm: y from -40 to -38).
constexpr double kRimY = -39.0;
constexpr double kRimZ = 40.0;

struct State {
    double dragged = 0; // the push/pull distance the drag left
};

QString itemText(AcceptanceRunner& r, const QString& objectName)
{
    const QQuickItem* item = r.findItem(objectName);
    return item ? item->property("text").toString() : QString();
}

double height(AcceptanceRunner& r)
{
    return geom::boundingBox(r.body(0).shape()).size().z;
}

bool previewShown(AcceptanceRunner& r)
{
    for (const auto& body : r.app().interaction().renderScene().bodies)
        if (body.isPreview)
            return true;
    return false;
}

// A 120 x 80 x 40 mm tray: rounded corners (R8) and bottom (R3), shelled
// open at the top with 2 mm walls, a floor with 84 vent holes (one through
// cut). Built through the same commands the UI pushes.
bool buildTray(AcceptanceRunner& r)
{
    auto& stack = r.app().interaction().undoStack();
    auto& document = r.app().document();
    auto box = std::make_unique<doc::BoxFeature>();
    box->origin = {-60, -40, 0};
    box->size = {120, 80, 40};
    auto create = std::make_unique<cmd::CreateBodyCommand>("Tray", std::move(box));
    const Uuid id = create->bodyId();
    bool ok = stack.push(std::move(create), document).ok();
    auto shape = [&] { return document.body(id)->shape(); };
    auto fillet = [&](double radius, auto&& pick) {
        auto f = std::make_unique<doc::FilletFeature>();
        f->size = radius;
        for (int i = 0; i < shape().edgeCount(); ++i)
            if (const auto e = geom::edgeInfo(shape(), i); e && pick(*e))
                f->edges.push_back({i, *geom::captureEdgeSignature(shape(), i)});
        return stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(f)), document).ok();
    };
    ok = ok && fillet(8.0, [](const geom::EdgeInfo& e) { return e.kind == geom::CurveKind::Line && std::abs(std::abs(e.tangent.z) - 1) < 1e-9; });
    ok = ok && fillet(3.0, [](const geom::EdgeInfo& e) { return std::abs(e.start.z) < 1e-6 && std::abs(e.end.z) < 1e-6; });
    int top = -1;
    for (int i = 0; ok && i < shape().faceCount(); ++i)
        if (const auto f = geom::faceInfo(shape(), i); f && f->isPlanar() && f->normal.z > 0.999)
            top = i;
    auto shell = std::make_unique<doc::ShellFeature>();
    if (top >= 0)
        shell->faces = {{top, *geom::captureFaceSignature(shape(), top)}};
    shell->thickness = 2.0;
    ok = ok && top >= 0 && stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(shell)), document).ok();
    sketch::Sketch vents(Uuid::generate(), sketch::Plane{{0, 0, 0}, {1, 0, 0}, {0, 1, 0}});
    vents.setName("Vents");
    vents.setHostBody(id);
    for (int i = 0; i < 12; ++i)
        for (int j = 0; j < 7; ++j)
            vents.addCircle(vents.addPoint({-41.25 + 7.5 * i, -22.5 + 7.5 * j}), 2.0);
    const Uuid sketchId = vents.id();
    ok = ok && stack.push(std::make_unique<cmd::CreateSketchCommand>(vents), document).ok();
    auto cut = std::make_unique<doc::ExtrudeFeature>();
    cut->sketchId = sketchId;
    if (ok)
        if (auto regions = doc::sketchRegions(*document.sketch(sketchId)))
            for (const auto& region : regions.value())
                cut->profiles.push_back(doc::makeProfileRef(region, *document.sketch(sketchId)));
    cut->distance = 5;
    cut->mode = doc::ExtrudeMode::Cut;
    cut->throughAll = true;
    ok = ok && stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(cut)), document).ok();
    r.app().interaction().documentChanged();
    ok = ok && r.app().interaction().setSketchVisible(sketchId, false).ok();
    return ok && !document.body(id)->hasFailures();
}

// Zooms in on the front rim (2 mm wide; at height z) so a click lands on it.
void zoomToRim(AcceptanceRunner& r, double z = kRimZ)
{
    auto& in = r.app().interaction();
    in.setStandardView(StandardView::Isometric, false);
    in.fitAll(false);
    in.wheel(in.camera().project({0, kRimY, z}), 8);
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto state = std::make_shared<State>();
    auto num = [](double v) { return AcceptanceRunner::num(v); };
    return {
        [&r] {
            r.check(buildTray(r), "previews: a vented tray (set up through commands)");
            r.check(r.body(0).shape().faceCount() > 100, "the tray has many faces",
                    QString::number(r.body(0).shape().faceCount()));
            zoomToRim(r);
        },
        [&r] {
            r.click(r.screenPoint(0, kRimY, kRimZ));
            r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "clicking the rim arms Push/Pull",
                    r.app().operationTitle());
        },
        [&r, num] {
            // Drag the arrow up, 24 moves, through the window's input path.
            const auto* op = r.app().interaction().operation();
            if (!op) {
                r.check(false, "an operation to drag");
                return;
            }
            // The arrow sits where the rim's face is thickest, maybe out of
            // the zoomed view: show the whole tray.
            r.app().interaction().fitAll(false);
            const auto& camera = r.app().interaction().camera();
            const interact::LinearManipulator handle = op->handle(0);
            const Vec3 anchor = handle.anchor(op->handleOffset(0));
            const double px = camera.pixelSize(anchor);
            // On the shaft (the value chip sits beside the arrow's tip).
            const interact::ArrowStyle style;
            const Vec2 grab = camera.project(anchor + handle.direction() * ((style.gapPx + style.shaftPx * 0.6) * px));
            Vec2 up = camera.project(anchor + handle.direction()) - camera.project(anchor);
            up = up * (1.0 / up.length());
            const Vec2 to = grab + up * 48.0;
            r.drag({grab.x, grab.y}, {to.x, to.y}, 24);
            // The drag is over before the preview of its last value: the
            // window did not wait for the kernel.
            r.check(r.app().interaction().previewBusy(), "the drag returns while its preview still computes");
            r.check(r.app().lastDragMoves() >= 24, "the viewport timed every pointer move",
                    QString::number(r.app().lastDragMoves()));
            r.check(r.app().lastDragLongestMs() < 50.0, "no pointer move of the drag blocked the window for 50 ms",
                    num(r.app().lastDragLongestMs()));
        },
        [&r, state, num] {
            // (The runner waited for the preview.)
            const auto* op = r.app().interaction().operation();
            state->dragged = op ? op->value() : 0.0;
            r.check(state->dragged > 1.0, "the drag moved the arrow", num(state->dragged));
            r.check(previewShown(r), "then the preview is shown");
            r.check(op && op->hasPreview() && op->previewMesh()->triangleCount() > 0 && r.app().operationError().isEmpty()
                        && r.app().operationCanCommit(),
                    "without an error, ready to apply");
            r.screenshot(QStringLiteral("previews_01_dragged"));
            r.key(Qt::Key_Return);
            r.check(std::abs(height(r) - (40.0 + state->dragged)) < 1e-6, "Enter applies it",
                    num(height(r)));
        },
        // Ctrl+Z while a preview is being computed (Esc first leaves the
        // value field, whose own Ctrl+Z would undo typing).
        [&r, state] {
            zoomToRim(r, kRimZ + state->dragged);
            r.click(r.screenPoint(0, kRimY, kRimZ + state->dragged));
            r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "the pushed rim", r.app().operationTitle());
            r.type(QStringLiteral("5"));
            r.check(r.app().interaction().previewBusy(), "a typed value's preview computes off the GUI thread");
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(height(r) - 40.0) < 1e-6, "Ctrl+Z meanwhile undoes the push at once", AcceptanceRunner::num(height(r)));
        },
        [&r] {
            r.check(!previewShown(r), "and the preview of the undone state never shows");
            r.check(std::abs(height(r) - 40.0) < 1e-6, "the tray stays 40 mm high", AcceptanceRunner::num(height(r)));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },
        // Enter while the preview is being computed.
        [&r] {
            zoomToRim(r);
            r.click(r.screenPoint(0, kRimY, kRimZ));
            r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "the rim again", r.app().operationTitle());
            r.type(QStringLiteral("3"));
            r.key(Qt::Key_Return);
            r.check(std::abs(height(r) - 43.0) < 1e-6, "Enter applies a value whose preview still computes",
                    AcceptanceRunner::num(height(r)));
        },
        [&r] {
            r.check(!previewShown(r), "no stale preview after it");
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            // The rim's outer edge: a 5 mm fillet on a 2 mm wall is refused.
            r.click(r.screenPoint(0, -40, 43));
            r.check(r.app().operationTitle() == QStringLiteral("Fillet"), "the rim's outer edge offers a fillet",
                    r.app().operationTitle());
            r.type(QStringLiteral("5"));
        },
        [&r] {
            const QString error = itemText(r, QStringLiteral("valueChipError"));
            r.check(error.contains(QStringLiteral("too large")) || error.startsWith(QStringLiteral("Unable")),
                    "the refusal comes back from the worker to the value chip", error);
            r.check(!r.app().operationCanCommit(), "and there is nothing to apply");
            r.screenshot(QStringLiteral("previews_02_refused"));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },
        // A long Model panel (40 bodies with 2 steps each) while a body's Move
        // arrow is dragged: a drag step changes no row, so the panel is not
        // rebuilt (TD-18).
        [&r] {
            auto& stack = r.app().interaction().undoStack();
            auto& document = r.app().document();
            bool ok = true;
            for (int i = 0; i < 40 && ok; ++i) {
                auto cube = std::make_unique<doc::BoxFeature>();
                cube->origin = {-60.0 + 12.0 * (i % 10), 60.0 + 12.0 * (i / 10), 0};
                cube->size = {8, 8, 8};
                auto create = std::make_unique<cmd::CreateBodyCommand>(document.nextBodyName(), std::move(cube));
                const Uuid id = create->bodyId();
                auto move = std::make_unique<doc::MoveFeature>();
                move->translation = {0, 0, 1};
                ok = stack.push(std::move(create), document).ok()
                  && stack.push(std::make_unique<cmd::AddFeatureCommand>(id, std::move(move)), document).ok();
            }
            r.app().interaction().documentChanged();
            r.app().interaction().fitAll(false);
            r.check(ok && r.app().bodyCount() == 41, "40 more bodies", QString::number(r.app().bodyCount()));
        },
        [] {}, [] {},
        [&r] {
            const QString row = QStringLiteral("historyRow_") + QString::fromStdString(r.body(0).id().toString());
            r.check(r.clickItem(row), "the tray's Model panel row selects it");
            r.check(r.app().operationTitle() == QStringLiteral("Move"), "with the Move arrows", r.app().operationTitle());
        },
        [&r] {
            const auto* op = r.app().interaction().operation();
            if (!op || op->handleCount() < 3) {
                r.check(false, "Move arrows to drag");
                return;
            }
            // The Z arrow, 20 moves up.
            const auto& camera = r.app().interaction().camera();
            const interact::LinearManipulator handle = op->handle(2);
            const Vec3 anchor = handle.anchor(op->handleOffset(2));
            const double px = camera.pixelSize(anchor);
            const interact::ArrowStyle style;
            const Vec2 grab = camera.project(anchor + handle.direction() * ((style.gapPx + style.shaftPx * 0.6) * px));
            r.drag({grab.x, grab.y}, {grab.x, grab.y - 60.0}, 20);
            r.check(r.app().lastDragMoves() >= 20, "the Move arrow was dragged", QString::number(r.app().lastDragMoves()));
            r.check(r.app().lastDragLongestMs() < 50.0,
                    "with 81 Model panel rows, no pointer move blocked the window for 50 ms",
                    QStringLiteral("longest %1 ms, average %2 ms")
                        .arg(r.app().lastDragLongestMs(), 0, 'f', 2)
                        .arg(r.app().lastDragAverageMs(), 0, 'f', 2));
        },
        [&r] {
            const auto* op = r.app().interaction().operation();
            r.check(op && op->hasPreview() && op->value() > 1.0, "the moved tray is previewed",
                    op ? AcceptanceRunner::num(op->value()) : QStringLiteral("no operation"));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("previews"), 60, steps});

} // namespace
} // namespace os::app
