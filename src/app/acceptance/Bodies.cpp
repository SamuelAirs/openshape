// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Bodies and copies: Duplicate (value chip, Ctrl+D, Model panel row), Split
// into bodies (value chip, Model panel), Mirror / Pattern as separate bodies,
// Rotate about a picked edge.

#include "app/AcceptanceRunner.h"
#include "commands/DocumentCommands.h"
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
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("bodies"), 40, steps});

} // namespace
} // namespace os::app
