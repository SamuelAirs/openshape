// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Mirror and Pattern copies are independent bodies (the owner's report from
// the iPhone: a mirrored copy kept following the original). A box moved off
// the origin, mirrored across YZ: the image does not touch it, so it becomes a
// body of its own without asking (the hint says so; Separate bodies switches
// it). Pushing the original's top face leaves the image as it was. A pattern
// 5 mm apart: separate bodies; typed to touch: joined; pushing the original
// again leaves the copies as they were.

#include "app/AcceptanceRunner.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "interaction/InteractionController.h"
#include "interaction/Operation.h"
#include "ui/AppController.h"

#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

struct State {
    QStringList messages; // what the app told the user
    Uuid original;
};

QString itemText(AcceptanceRunner& r, const QString& objectName)
{
    const QQuickItem* item = r.findItem(objectName);
    return item ? item->property("text").toString() : QString();
}

// "x0..x1, y0..y1, z0..z1" of a body, for the check's actual value.
QString boxText(const doc::Body& body)
{
    const auto bb = geom::boundingBox(body.shape());
    auto range = [](double a, double b) { return AcceptanceRunner::num(a) + QStringLiteral("..") + AcceptanceRunner::num(b); };
    return range(bb.min.x, bb.max.x) + QStringLiteral(", ") + range(bb.min.y, bb.max.y) + QStringLiteral(", ")
         + range(bb.min.z, bb.max.z);
}

bool hasBox(const doc::Body& body, Vec3 min, Vec3 max)
{
    const auto bb = geom::boundingBox(body.shape());
    return (bb.min - min).length() < 1e-6 && (bb.max - max).length() < 1e-6;
}

const interact::MirrorOperation* mirrorOp(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::MirrorOperation*>(r.app().interaction().operation());
}

const interact::PatternOperation* patternOp(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::PatternOperation*>(r.app().interaction().operation());
}

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto state = std::make_shared<State>();
    QObject::connect(&r.app(), &ui::AppController::message, &r,
                     [state](const QString& text) { state->messages.push_back(text); });
    auto original = [&r, state]() -> const doc::Body* { return r.app().document().body(state->original); };
    return {
        // A box, moved 30 mm along X (off the origin planes).
        [&r, state] {
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b")); // (-10,-10,0)..(10,10,20)
            r.check(r.app().bodyCount() == 1, "copies: B adds a box");
            if (r.app().bodyCount() == 1)
                state->original = r.body(0).id();
        },
        [] {}, [] {}, [] {},
        [&r] {
            r.check(r.clickItem(QStringLiteral("historyRow_") + QString::fromStdString(r.body(0).id().toString())),
                    "the box selected in the Model panel");
            r.type(QStringLiteral("30"));
            r.key(Qt::Key_Return);
        },
        [&r] {
            r.check(hasBox(r.body(0), {20, -10, 0}, {40, 10, 20}), "typed 30: the box moved along X", boxText(r.body(0)));
            r.app().interaction().fitAll(false);
        },

        // ---- Mirror: automatically a separate body -------------------------------------
        [&r] { r.check(r.clickItem(QStringLiteral("tool_mirror")), "Mirror tool button"); },
        [&r] { r.check(r.clickItem(QStringLiteral("barAction_plane:0")), "Across YZ"); },
        [] {},
        [&r] {
            const auto* mirror = mirrorOp(r);
            r.check(mirror && mirror->separate() && mirror->separateIsAutomatic() && r.app().operationCanCommit(),
                    "the image would not touch the box: a separate body, without asking");
            const QString hint = itemText(r, QStringLiteral("hintText"));
            r.check(hint.startsWith(QStringLiteral("Enter or Apply mirrors it as a separate body")), "the hint line says so", hint);
            r.screenshot(QStringLiteral("copies_01_mirror_preview"));
            // The option shows the choice and switches it.
            r.check(r.clickItem(QStringLiteral("barAction_separate")), "Separate bodies option (turned off)");
        },
        [] {},
        [&r] {
            const auto* mirror = mirrorOp(r);
            r.check(mirror && !mirror->separate(), "joined when turned off");
            const QString hint = itemText(r, QStringLiteral("hintText"));
            r.check(hint.startsWith(QStringLiteral("Enter or Apply mirrors it into the body")), "the hint line follows", hint);
            r.check(r.clickItem(QStringLiteral("barAction_separate")), "Separate bodies option (on again)");
        },
        [] {},
        [&r, state] {
            const auto* mirror = mirrorOp(r);
            r.check(mirror && mirror->separate(), "separate again");
            state->messages.clear();
            r.check(r.clickItem(QStringLiteral("barAction_apply")), "Apply the mirror");
        },
        [] {},
        [&r, state] {
            r.check(r.app().bodyCount() == 2, "the image is a second body", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 2)
                return;
            r.check(hasBox(r.body(1), {-40, -10, 0}, {-20, 10, 20}) && std::abs(geom::volume(r.body(1).shape()) - 8000.0) < 1e-6,
                    "across YZ: -40..-20", boxText(r.body(1)));
            r.check(state->messages.join(QStringLiteral(" | ")).contains(QStringLiteral("Mirrored as a separate body")),
                    "the message says what happened", state->messages.join(QStringLiteral(" | ")));
            r.check(r.app().document().bodiesUsing(state->original).empty(), "the image is not built from the box");
            r.screenshot(QStringLiteral("copies_02_mirrored"));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },
        [] {},
        // Push the original's top face to 35 mm: the image stays 20 mm tall.
        [&r] {
            r.click(r.uncoveredScreenPoint({{34, 4, 20}, {26, 4, 20}, {34, -4, 20}, {26, -4, 20}, {30, 0, 20}}));
            r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "clicking the box's top face arms Push/Pull",
                    r.app().operationTitle());
            r.type(QStringLiteral("35"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, original] {
            const doc::Body* box = original();
            r.check(box && hasBox(*box, {20, -10, 0}, {40, 10, 35}), "the box is 35 mm tall",
                    box ? boxText(*box) : QStringLiteral("gone"));
            r.check(r.app().bodyCount() == 2 && hasBox(r.body(1), {-40, -10, 0}, {-20, 10, 20})
                        && std::abs(geom::volume(r.body(1).shape()) - 8000.0) < 1e-6,
                    "the mirror image did not change", r.app().bodyCount() == 2 ? boxText(r.body(1)) : QString());
            r.screenshot(QStringLiteral("copies_03_original_pushed"));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
        },

        // ---- Pattern: separate bodies with gaps, joined when touching ------------------
        [&r, state] {
            r.check(r.clickItem(QStringLiteral("historyRow_") + QString::fromStdString(state->original.toString())),
                    "the box selected in the Model panel again");
            r.check(r.clickItem(QStringLiteral("tool_pattern")), "Pattern tool button");
        },
        [] {},
        [&r] {
            const auto* pattern = patternOp(r);
            r.check(pattern && pattern->separate() && pattern->separateIsAutomatic() && pattern->canCommit(),
                    "5 mm apart: the copies are separate bodies, without asking");
            const QString hint = itemText(r, QStringLiteral("hintText"));
            r.check(hint.contains(QStringLiteral("each copy becomes a separate body")), "the hint line says so", hint);
            r.type(QStringLiteral("20"));
            r.key(Qt::Key_Escape); // leaves the field (Enter would apply); typing starts a new value
        },
        [] {},
        [&r] {
            const auto* pattern = patternOp(r);
            r.check(pattern && !pattern->separate() && std::abs(pattern->value() - 20.0) < 1e-9,
                    "typed to touch (20 mm): joined", pattern ? AcceptanceRunner::num(pattern->value()) : QString());
            r.screenshot(QStringLiteral("copies_04_pattern_touching"));
            r.type(QStringLiteral("25"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, state] {
            r.check(r.app().bodyCount() == 4, "25 mm apart: two more bodies", QString::number(r.app().bodyCount()));
            if (r.app().bodyCount() != 4)
                return;
            r.check(hasBox(r.body(2), {45, -10, 0}, {65, 10, 35}) && hasBox(r.body(3), {70, -10, 0}, {90, 10, 35}),
                    "copies of the 35 mm box, 25 mm apart", boxText(r.body(2)) + QStringLiteral(" / ") + boxText(r.body(3)));
            r.check(state->messages.join(QStringLiteral(" | ")).contains(QStringLiteral("Patterned as 2 separate bodies")),
                    "the message says what happened", state->messages.join(QStringLiteral(" | ")));
            r.screenshot(QStringLiteral("copies_05_patterned"));
            r.key(Qt::Key_Escape);
            r.key(Qt::Key_Escape);
            r.app().interaction().fitAll(false);
        },
        [] {}, [] {},
        // Push the original's top face again, to 50 mm: the copies stay 35 mm.
        [&r] {
            r.click(r.uncoveredScreenPoint({{34, 4, 35}, {26, 4, 35}, {34, -4, 35}, {26, -4, 35}, {30, 0, 35}}));
            r.check(r.app().operationTitle() == QStringLiteral("Push/Pull"), "the box's top face again", r.app().operationTitle());
            r.type(QStringLiteral("50"));
            r.key(Qt::Key_Return);
        },
        [] {},
        [&r, original] {
            const doc::Body* box = original();
            r.check(box && hasBox(*box, {20, -10, 0}, {40, 10, 50}), "the box is 50 mm tall",
                    box ? boxText(*box) : QStringLiteral("gone"));
            if (r.app().bodyCount() != 4) {
                r.check(false, "four bodies", QString::number(r.app().bodyCount()));
                return;
            }
            r.check(hasBox(r.body(2), {45, -10, 0}, {65, 10, 35}) && hasBox(r.body(3), {70, -10, 0}, {90, 10, 35}),
                    "the pattern copies did not change", boxText(r.body(2)) + QStringLiteral(" / ") + boxText(r.body(3)));
            r.check(hasBox(r.body(1), {-40, -10, 0}, {-20, 10, 20}), "nor did the mirror image", boxText(r.body(1)));
            r.screenshot(QStringLiteral("copies_06_original_pushed_again"));
            // One undo each: the push, then the whole pattern.
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.key(Qt::Key_Z, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 2, "undo: the push, then the pattern", QString::number(r.app().bodyCount()));
            r.key(Qt::Key_Y, Qt::ControlModifier);
            r.check(r.app().bodyCount() == 4 && hasBox(r.body(3), {70, -10, 0}, {90, 10, 35}), "redo brings the copies back");
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("copies"), 41, steps});

} // namespace
} // namespace os::app
