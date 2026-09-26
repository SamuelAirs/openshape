// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bodies and copies: Duplicate (value chip, Ctrl+D, Model panel row), Split
// into bodies (value chip, Model panel), Mirror / Pattern as separate bodies,
// Delete on a body others are built from (hides it), Rotate about a picked
// edge, corner, hole or circle.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
#include "document/SketchProfiles.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "sketch/Sketch.h"
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

        // ---- Split into bodies ------------------------------------------------------
        // Set up (not what is tested): a 60 x 20 x 5 plate cut in two by a
        // 4 mm slot (a hidden tool body).
        [&r] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().newDocument();
            auto& stack = r.app().interaction().undoStack();
            auto& document = r.app().document();
            auto plate = std::make_unique<doc::BoxFeature>();
            plate->origin = {-30, -10, 0};
            plate->size = {60, 20, 5};
            auto slot = std::make_unique<doc::BoxFeature>();
            slot->origin = {-2, -15, -5};
            slot->size = {4, 30, 30};
            auto plateCommand = std::make_unique<cmd::CreateBodyCommand>("Body 1", std::move(plate));
            auto slotCommand = std::make_unique<cmd::CreateBodyCommand>("Slot", std::move(slot));
            const Uuid plateId = plateCommand->bodyId();
            const Uuid slotId = slotCommand->bodyId();
            auto combine = std::make_unique<doc::CombineFeature>();
            combine->toolBody = slotId;
            combine->mode = doc::CombineMode::Subtract;
            const bool built = stack.push(std::move(plateCommand), document).ok() && stack.push(std::move(slotCommand), document).ok()
                            && stack.push(std::make_unique<cmd::AddFeatureCommand>(plateId, std::move(combine)), document).ok()
                            && stack.push(std::make_unique<cmd::SetBodyVisibilityCommand>(slotId, false), document).ok();
            r.app().interaction().documentChanged();
            r.app().interaction().fitAll(false);
            r.check(built && r.body(0).shape().solidCount() == 2, "split: a plate cut in two by a slot");
        },
        [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "Model panel row selects the plate");
            r.check(r.findItem(QStringLiteral("action_split")) != nullptr, "a body in pieces offers Split into bodies");
        },
        [&r, num] {
            r.check(r.clickItem(QStringLiteral("action_split")), "Split into bodies button");
            r.check(r.app().bodyCount() == 3, "one more body (plate, hidden slot, piece)", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 3)
                return;
            r.check(std::abs(geom::volume(r.body(0).shape()) - 2800.0) < 1e-6 && r.body(0).shape().solidCount() == 1,
                    "the plate keeps one 28 mm piece", num(geom::volume(r.body(0).shape())));
            r.check(std::abs(geom::volume(r.body(2).shape()) - 2800.0) < 1e-6
                        && std::abs(geom::boundingBox(r.body(2).shape()).min.x - 2.0) < 1e-6,
                    "the other piece is a body of its own", num(geom::volume(r.body(2).shape())));
            r.screenshot(QStringLiteral("bodies_02_split"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2 && r.body(0).shape().solidCount() == 2, "undo: one body in two pieces again");
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("historySplit_") + idOf(r.body(0))),
                    "Split into bodies right under the body's warning in the Model panel");
            r.check(r.app().bodyCount() == 3, "it splits too", QString::number(r.app().bodyCount()));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2, "undo: in two pieces again");
        },
        [&r] {
            // The step that left the pieces (the Combine) offers it while expanded.
            const QString step = QString::fromStdString(r.body(0).features().back()->id().toString());
            r.check(r.clickItem(QStringLiteral("historyRow_") + step), "the Combine step's row");
        },
        [&r] {
            const QString step = QString::fromStdString(r.body(0).features().back()->id().toString());
            r.check(r.clickItem(QStringLiteral("historySplit_") + step), "Split into bodies on the step that left the pieces");
            r.check(r.app().bodyCount() == 3, "that splits it as well", QString::number(r.app().bodyCount()));
        },
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "the split plate selected in the Model panel"); },
        [&r, num] {
            // The piece is built from the plate: Delete hides the plate instead.
            r.key(Qt::Key_Delete);
            r.check(r.app().bodyCount() == 3, "Delete keeps the plate the piece is built from", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 3)
                return;
            r.check(!r.body(0).isVisible(), "it is hidden instead");
            const double piece = geom::volume(r.body(2).shape());
            r.check(std::abs(piece - 2800.0) < 1e-6 && !r.body(2).hasFailures(), "the piece stays as it was", num(piece));
            r.screenshot(QStringLiteral("bodies_03a_parent_hidden"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.body(0).isVisible(), "undo shows the plate again");
        },
        [&r, num] {
            // The same from the Model panel (the plate's row is still expanded).
            r.check(r.clickItem(QStringLiteral("historyDelete_") + idOf(r.body(0))), "Delete in the plate's Model panel row");
            r.check(r.app().bodyCount() == 3 && !r.body(0).isVisible(), "the row's Delete hides it too");
            if (r.app().bodyCount() == 3)
                r.check(std::abs(geom::volume(r.body(2).shape()) - 2800.0) < 1e-6 && !r.body(2).hasFailures(),
                        "and the piece stays", num(geom::volume(r.body(2).shape())));
        },
        [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "the hidden plate's row"); },
        [&r] {
            const QQuickItem* button = r.findItem(QStringLiteral("historyDelete_") + idOf(r.body(0)));
            r.check(!button || !button->isVisible(), "a hidden plate the piece is built from offers no Delete");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.body(0).isVisible(), "undo shows the plate again");
        },
        [&r] {
            // An upstream edit through the Model panel: both pieces follow.
            const QString box = QString::fromStdString(r.body(0).features().front()->id().toString());
            r.check(r.clickItem(QStringLiteral("historyRow_") + box), "the plate's Box step in the Model panel");
        },
        [&r] {
            const QString box = QString::fromStdString(r.body(0).features().front()->id().toString());
            r.check(r.clickItem(QStringLiteral("historyParam_") + box + QStringLiteral("_height")), "its height field");
            r.type(QStringLiteral("8"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, num] {
            if (r.app().bodyCount() != 3) {
                r.check(false, "three bodies after the split", QString::number(r.app().bodyCount()));
                return;
            }
            const double plate = geom::volume(r.body(0).shape());
            const double piece = geom::volume(r.body(2).shape());
            r.check(std::abs(plate - 4480.0) < 1e-6 && std::abs(piece - 4480.0) < 1e-6,
                    "an 8 mm plate: both pieces follow", num(plate) + QStringLiteral(" / ") + num(piece));
            r.check(!r.body(2).hasFailures(), "the piece has no failed step");
            r.screenshot(QStringLiteral("bodies_03_split_follows"));
        },

        // ---- Mirror / Pattern as separate bodies -----------------------------------
        [&r] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().newDocument();
            r.app().setView(QStringLiteral("iso"));
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b")); // (-10,-10,0)..(10,10,20)
        },
        [] {}, [] {}, [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "copies: the box selected in the Model panel"); },
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_mirror")), "Mirror tool button");
            r.check(r.clickItem(QStringLiteral("barAction_separate")), "Separate bodies option of Mirror");
            const auto* mirror = dynamic_cast<const interact::MirrorOperation*>(r.app().interaction().operation());
            r.check(mirror && mirror->separate(), "Mirror makes a separate body");
        },
        [&r] {
            r.click(r.screenPoint(10, 0, 10)); // the box's +X face
            r.check(r.app().operationCanCommit(), "clicking the +X face sets the mirror plane");
        },
        [&r, num] {
            r.check(r.clickItem(QStringLiteral("barAction_apply")), "Apply the mirror");
            r.check(r.app().bodyCount() == 2, "the mirror image is a second body", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 2)
                return;
            const auto bb = geom::boundingBox(r.body(1).shape());
            r.check(std::abs(bb.min.x - 10.0) < 1e-6 && std::abs(bb.max.x - 30.0) < 1e-6
                        && std::abs(geom::volume(r.body(1).shape()) - 8000.0) < 1e-6,
                    "beside the box, across its face", num(bb.min.x) + QStringLiteral("..") + num(bb.max.x));
            r.check(r.body(0).features().size() == 1 && std::abs(geom::volume(r.body(0).shape()) - 8000.0) < 1e-6,
                    "the original keeps its history and volume");
            r.screenshot(QStringLiteral("bodies_04_mirror_separate"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 1, "undo removes the image");
        },
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_pattern")), "Pattern tool button");
            r.check(r.clickItem(QStringLiteral("action_separate")), "Separate bodies option of Pattern");
            const auto* pattern = dynamic_cast<const interact::PatternOperation*>(r.app().interaction().operation());
            r.check(pattern && pattern->separate() && pattern->canCommit(), "the pattern previews separate copies");
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            r.check(r.app().bodyCount() == 3, "three bodies: the box and two copies", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 3)
                return;
            const double x1 = geom::boundingBox(r.body(1).shape()).min.x;
            const double x2 = geom::boundingBox(r.body(2).shape()).min.x;
            r.check(std::abs(x1 - 15.0) < 1e-6 && std::abs(x2 - 40.0) < 1e-6, "25 mm apart along X", num(x1) + QStringLiteral(", ") + num(x2));
            r.screenshot(QStringLiteral("bodies_05_pattern_separate"));
        },
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "the patterned box selected in the Model panel"); },
        [&r, num] {
            // The copies are built from the box: Delete hides it instead.
            r.key(Qt::Key_Delete);
            r.check(r.app().bodyCount() == 3, "Delete keeps the box the copies are built from", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 3)
                return;
            r.check(!r.body(0).isVisible(), "it is hidden instead");
            const double v1 = geom::volume(r.body(1).shape());
            const double v2 = geom::volume(r.body(2).shape());
            r.check(std::abs(v1 - 8000.0) < 1e-6 && std::abs(v2 - 8000.0) < 1e-6 && !r.body(1).hasFailures() && !r.body(2).hasFailures(),
                    "both copies stay", num(v1) + QStringLiteral(", ") + num(v2));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.body(0).isVisible(), "undo shows the box again");
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 1, "undo removes the copies");
        },

        // ---- Rotate about a picked edge ---------------------------------------------
        // Set up: a 20 x 10 x 5 box at the origin.
        [&r] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().newDocument();
            r.app().interaction().setStandardView(StandardView::Isometric, false);
            auto box = std::make_unique<doc::BoxFeature>();
            box->size = {20, 10, 5};
            const bool built = r.app().interaction().undoStack().push(std::make_unique<cmd::CreateBodyCommand>("Body 1", std::move(box)),
                                                                    r.app().document()).ok();
            r.app().interaction().documentChanged();
            r.app().interaction().fitAll(false);
            r.check(built, "rotate: a 20 x 10 x 5 box");
        },
        [] {}, [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "the box selected in the Model panel"); },
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_rotate")), "Rotate tool button");
            const auto* op = r.app().interaction().operation();
            r.check(op && op->ringCount() == 3, "three rings through the center");
        },
        [&r] {
            r.click(r.screenPoint(4, 0, 5)); // the top front edge, along X
            const auto* rotate = dynamic_cast<const interact::RotateOperation*>(r.app().interaction().operation());
            r.check(rotate && rotate->ringCount() == 1 && rotate->axis() && std::abs(rotate->axis()->x - 1.0) < 1e-9,
                    "clicking an edge makes it the axis: one ring around it");
            r.check(r.app().operationValueLabel() == QStringLiteral("Angle"), "the value is the angle about the edge",
                    r.app().operationValueLabel());
            r.screenshot(QStringLiteral("bodies_06_rotate_about_edge"));
            r.type(QStringLiteral("90"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            const auto bb = geom::boundingBox(r.body(0).shape());
            const bool exact = std::abs(bb.min.x) < 1e-6 && std::abs(bb.max.x - 20) < 1e-6 && std::abs(bb.min.y) < 1e-6
                            && std::abs(bb.max.y - 5) < 1e-6 && std::abs(bb.min.z - 5) < 1e-6 && std::abs(bb.max.z - 15) < 1e-6;
            r.check(exact, "90 degrees about the edge: it stands on that edge (0..20, 0..5, 5..15)",
                    num(bb.min.y) + QStringLiteral("..") + num(bb.max.y) + QStringLiteral(", ") + num(bb.min.z)
                        + QStringLiteral("..") + num(bb.max.z));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(geom::boundingBox(r.body(0).shape()).max.z - 5) < 1e-6, "undo lays it down again");
        },
        [] {}, [] {}, [] {}, // the same spot clicked again now would be a double-click
        [&r] {
            r.click(r.screenPoint(4, 0, 5));
            r.check(r.app().interaction().operation() && r.app().interaction().operation()->ringCount() == 1,
                    "the edge again: one ring");
            r.check(r.clickItem(QStringLiteral("action_pivotCenter")), "Center pivot button");
            const auto* op = r.app().interaction().operation();
            r.check(op && op->ringCount() == 3, "back to three rings through the center");
        },
        [&r, num] {
            r.click(r.screenPoint(20, 0, 5)); // the front right top corner
            const auto* rotate = dynamic_cast<const interact::RotateOperation*>(r.app().interaction().operation());
            const Vec3 pivot = rotate ? rotate->center() : Vec3{};
            r.check(rotate && !rotate->axis() && rotate->ringCount() == 3 && (pivot - Vec3{20, 0, 5}).length() < 1e-6,
                    "clicking a corner moves the three rings there",
                    num(pivot.x) + QStringLiteral(", ") + num(pivot.y) + QStringLiteral(", ") + num(pivot.z));
            r.type(QStringLiteral("90"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            // 90 degrees about Z through (20, 0): x' = 20 - y, y' = x - 20.
            const auto bb = geom::boundingBox(r.body(0).shape());
            const bool exact = std::abs(bb.min.x - 10) < 1e-6 && std::abs(bb.max.x - 20) < 1e-6 && std::abs(bb.min.y + 20) < 1e-6
                            && std::abs(bb.max.y) < 1e-6 && std::abs(bb.min.z) < 1e-6 && std::abs(bb.max.z - 5) < 1e-6;
            r.check(exact, "90 degrees about the corner (10..20, -20..0, 0..5)",
                    num(bb.min.x) + QStringLiteral("..") + num(bb.max.x) + QStringLiteral(", ") + num(bb.min.y)
                        + QStringLiteral("..") + num(bb.max.y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },

        // ---- Rotate about a hole; a circle's center as the pivot --------------------
        // Set up: a 40 x 30 x 6 plate with an 8 mm hole through it at (-10, 0):
        // off the plate's center (where the rings are), on the side away from
        // the value chip.
        [&r, num] {
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().newDocument();
            r.app().interaction().setStandardView(StandardView::Isometric, false);
            auto& stack = r.app().interaction().undoStack();
            auto& document = r.app().document();
            auto plate = std::make_unique<doc::BoxFeature>();
            plate->origin = {-20, -15, 0};
            plate->size = {40, 30, 6};
            auto plateCommand = std::make_unique<cmd::CreateBodyCommand>("Body 1", std::move(plate));
            const Uuid plateId = plateCommand->bodyId();
            bool built = stack.push(std::move(plateCommand), document).ok();
            sketch::Sketch circle(Uuid::generate(), sketch::Plane::xy());
            circle.setName("Sketch 1");
            circle.addCircle(circle.addPoint({-10, 0}), 4);
            circle.setVisible(false); // not picked through the hole
            const Uuid sketchId = circle.id();
            built = built && stack.push(std::make_unique<cmd::CreateSketchCommand>(std::move(circle)), document).ok();
            const sketch::Sketch* sk = document.sketch(sketchId);
            const auto regions = sk ? std::optional(doc::sketchRegions(*sk)) : std::nullopt;
            built = built && regions && regions->ok() && regions->value().size() == 1;
            if (built) {
                auto cut = std::make_unique<doc::ExtrudeFeature>();
                cut->sketchId = sketchId;
                cut->profiles = {doc::makeProfileRef(regions->value().front(), *sk)};
                cut->distance = 1;
                cut->mode = doc::ExtrudeMode::Cut;
                cut->throughAll = true;
                cut->symmetric = true;
                built = stack.push(std::make_unique<cmd::AddFeatureCommand>(plateId, std::move(cut)), document).ok();
            }
            r.app().interaction().documentChanged();
            r.app().interaction().fitAll(false);
            const double v = r.app().bodyCount() == 1 ? geom::volume(r.body(0).shape()) : 0.0;
            r.check(built && std::abs(v - (7200.0 - kPi * 16.0 * 6.0)) < 1e-6, "rotate: a plate with a hole", num(v));
        },
        [] {}, [] {},
        [&r] { r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(r.body(0))), "the plate selected in the Model panel"); },
        [&r] {
            r.check(r.clickItem(QStringLiteral("tool_rotate")), "Rotate tool button for the plate");
            // Zoomed in on the hole, so its rim and wall are easy to hit in
            // small windows too (TD-35).
            const QPointF hole = r.screenPoint(-10, 0, 3);
            r.app().interaction().wheel({hole.x(), hole.y()}, 3);
        },
        [] {},
        [&r, num] {
            const QPointF wall = r.screenPoint(-10 - 4 * 0.7071, 4 * 0.7071, 3); // the hole's wall, far side
            const auto picked = r.app().interaction().pickAt({wall.x(), wall.y()}, interact::InputProfile{});
            r.click(wall);
            const auto* rotate = dynamic_cast<const interact::RotateOperation*>(r.app().interaction().operation());
            const bool axis = rotate && rotate->axis() && std::abs(rotate->axis()->z - 1.0) < 1e-9;
            const Vec3 center = rotate ? rotate->center() : Vec3{};
            r.check(axis && rotate->ringCount() == 1 && std::abs(center.x + 10) < 1e-6 && std::abs(center.y) < 1e-6,
                    "clicking the hole's wall turns about its axis: one ring",
                    num(center.x) + QStringLiteral(", ") + num(center.y) + QStringLiteral(" (the view picks kind %1 there)").arg(int(picked.kind)));
            r.screenshot(QStringLiteral("bodies_07_rotate_about_hole"));
            r.type(QStringLiteral("90"));
            r.key(Qt::Key_Return);
        },
        [&r, num] {
            // 90 degrees about the hole's axis (-10, 0): x' = -10 - y, y' = x + 10.
            const auto bb = geom::boundingBox(r.body(0).shape());
            const bool exact = std::abs(bb.min.x + 25) < 1e-6 && std::abs(bb.max.x - 5) < 1e-6 && std::abs(bb.min.y + 10) < 1e-6
                            && std::abs(bb.max.y - 30) < 1e-6 && std::abs(bb.min.z) < 1e-6 && std::abs(bb.max.z - 6) < 1e-6;
            r.check(exact, "90 degrees about the hole (-25..5, -10..30, 0..6)",
                    num(bb.min.x) + QStringLiteral("..") + num(bb.max.x) + QStringLiteral(", ") + num(bb.min.y)
                        + QStringLiteral("..") + num(bb.max.y));
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(std::abs(geom::boundingBox(r.body(0).shape()).max.x - 20) < 1e-6, "undo turns it back");
        },
        [&r, num] {
            const QPointF rim = r.screenPoint(-10 + 4 * 0.7071, -4 * 0.7071, 6); // the hole's top rim, near side
            const auto picked = r.app().interaction().pickAt({rim.x(), rim.y()}, interact::InputProfile{});
            r.click(rim);
            const auto* rotate = dynamic_cast<const interact::RotateOperation*>(r.app().interaction().operation());
            const Vec3 pivot = rotate ? rotate->center() : Vec3{};
            r.check(rotate && !rotate->axis() && rotate->ringCount() == 3 && (pivot - Vec3{-10, 0, 6}).length() < 1e-6,
                    "clicking the hole's rim moves the rings to its center",
                    num(pivot.x) + QStringLiteral(", ") + num(pivot.y) + QStringLiteral(", ") + num(pivot.z)
                        + QStringLiteral(" (the view picks kind %1 there)").arg(int(picked.kind)));
            r.screenshot(QStringLiteral("bodies_08_pivot_at_hole_rim"));
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("bodies"), 40, steps});

} // namespace
} // namespace os::app
