// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Text on a face ("text"): the top face's Text action, the words typed into
// the chip's text field, the size and a 1 mm depth typed, applied (the
// volume grows by the letters' area x 1 mm), Deboss from the palette on the
// same face, the words changed in the Model panel, undo, and a project saved
// and reopened with the text. Needs the built-in font (resources/fonts/).

#include "app/AcceptanceRunner.h"
#include "document/Body.h"
#include "document/Document.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"
#include "interaction/InteractionController.h"
#include "ui/AppController.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QUrl>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <cmath>
#include <memory>

namespace os::app {
namespace {

const interact::TextOperation* textTool(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::TextOperation*>(r.app().interaction().operation());
}

// The letters' area for `words` at `size` (capital height) in the built-in font.
double letterArea(const std::string& words, double size)
{
    auto faces = geom::textFaces({words, doc::kTextFontRegular, size});
    return faces ? geom::surfaceArea(faces.value()) : -1.0;
}

QString idOf(const doc::Feature& f)
{
    return QString::fromStdString(f.id().toString());
}

struct State {
    double plate = 0; // the plate's volume before the text
    QStringList messages;
    QMetaObject::Connection listening;
};

std::vector<AcceptanceRunner::Step> steps(AcceptanceRunner& r)
{
    auto s = std::make_shared<State>();
    return {
        [&r, s] {
            s->listening = QObject::connect(&r.app(), &ui::AppController::message, &r.app(),
                                            [s](const QString& t) { s->messages << t; });
            r.check(geom::hasFont(doc::kTextFontRegular), "text: the built-in font (Noto Sans) is there");
            r.key(Qt::Key_B, Qt::NoModifier, QStringLiteral("b"));
            r.check(r.app().bodyCount() == 1, "text: a box");
        },
        [] {}, [] {}, [] {},
        [&r, s] {
            r.click(r.screenPoint(3, -3, 20));
            r.type(QStringLiteral("5"));
            r.key(Qt::Key_Return);
            r.check(std::abs(r.bodyHeight() - 5) < 1e-6, "text: a 20 x 20 x 5 plate", AcceptanceRunner::num(r.bodyHeight()));
            s->plate = r.bodyVolume();
            r.app().interaction().fitAll(false);
        },
        [] {},
        [&r] {
            // The pushed top face is still selected: its chip offers Text.
            r.check(r.clickItem(QStringLiteral("action_text")), "text: the top face's Text action");
        },
        [] {},
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool != nullptr, "text: the Text tool on the top face", r.app().operationTitle());
            r.check(r.app().operationTakesText(), "text: the chip has a text field");
            auto* field = r.findItem(QStringLiteral("textToolField"));
            r.check(field && field->isVisible() && field->hasActiveFocus(), "text: the text field takes the keys");
            r.check(!r.app().operationPrompt().isEmpty(), "text: it asks for the text");
            r.screenshot(QStringLiteral("text_tool_open"));
            // Enter with nothing typed: a plain message, nothing added.
            r.key(Qt::Key_Return);
        },
        [&r, s] {
            r.check(textTool(r) != nullptr && r.body(0).features().size() == 2, "text: nothing typed, nothing added");
            const QString said = s->messages.isEmpty() ? QString() : s->messages.last();
            r.check(said == QStringLiteral("Type the text first."), "text: it says to type the text", said);
            r.check(r.clickItem(QStringLiteral("textToolField")), "text: the text field");
        },
        [&r] { r.type(QStringLiteral("OK")); },
        [&r] {
            r.check(r.app().operationText() == QStringLiteral("OK"), "text: the words typed", r.app().operationText());
            r.check(r.app().operationCanCommit(), "text: it previews", r.app().operationError());
            r.check(r.clickItem(QStringLiteral("action_field:size")), "text: the Size field");
        },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("Size"), "text: the chip edits the size",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("6"));
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool && std::abs(tool->size() - 6) < 1e-9, "text: 6 mm capitals",
                    tool ? AcceptanceRunner::num(tool->size()) : QString());
            r.check(r.clickItem(QStringLiteral("action_field:depth")), "text: the Depth field");
        },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("Depth"), "text: the chip edits the depth",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("1"));
        },
        [&r] {
            r.screenshot(QStringLiteral("text_preview"));
            r.key(Qt::Key_Return);
        },
        [&r, s] {
            const double area = letterArea("OK", 6);
            r.check(area > 5, "text: the letters have area", AcceptanceRunner::num(area));
            r.check(textTool(r) == nullptr && r.body(0).features().size() == 3, "text: applied as one step");
            r.check(std::abs(r.bodyVolume() - (s->plate + area * 1.0)) < 1e-4 * area,
                    "text: the plate grows by the letters' area x 1 mm", AcceptanceRunner::num(r.bodyVolume() - s->plate));
            r.check(std::abs(r.bodyHeight() - 6) < 1e-6, "text: raised 1 mm above the plate", AcceptanceRunner::num(r.bodyHeight()));
            r.screenshot(QStringLiteral("text_raised"));
            // The Model panel: expand the step to change its words.
            r.check(r.clickItem(QStringLiteral("historyRow_") + idOf(*r.body(0).features().back())), "text: the step's Model panel row");
        },
        [] {},
        [&r] {
            const QString id = idOf(*r.body(0).features().back());
            r.check(r.clickItem(QStringLiteral("historyParam_") + id + QStringLiteral("_text")), "text: its Text field in the Model panel");
        },
        [&r] {
            r.key(Qt::Key_A, Qt::ControlModifier);
            r.type(QStringLiteral("HI"));
            r.key(Qt::Key_Return);
        },
        [] {}, [] {},
        [&r, s] {
            const auto* step = dynamic_cast<const doc::TextFeature*>(r.body(0).features().back().get());
            r.check(step && step->text == "HI", "text: the words changed from the Model panel",
                    step ? QString::fromStdString(step->text) : QString());
            const double area = letterArea("HI", 6);
            r.check(std::abs(r.bodyVolume() - (s->plate + area)) < 1e-4 * area, "text: the new words are raised",
                    AcceptanceRunner::num(r.bodyVolume() - s->plate));
            r.screenshot(QStringLiteral("text_edited"));
            r.key(Qt::Key_Z, Qt::ControlModifier);
        },
        [&r, s] {
            const auto* step = dynamic_cast<const doc::TextFeature*>(r.body(0).features().back().get());
            r.check(step && step->text == "OK", "text: undo brings the old words back");
            const double area = letterArea("OK", 6);
            r.check(std::abs(r.bodyVolume() - (s->plate + area)) < 1e-4 * area, "text: and their volume",
                    AcceptanceRunner::num(r.bodyVolume() - s->plate));
            // Saved and reopened: the same text.
            const double volume = r.bodyVolume();
            const QString path = QDir::temp().filePath(QStringLiteral("openshape_acceptance_text.openshape"));
            QFile::remove(path);
            r.check(r.app().saveProjectAs(QUrl::fromLocalFile(path)), "text: the project saves");
            r.app().newDocument();
            r.check(r.app().openProject(QUrl::fromLocalFile(path)), "text: and reopens");
            r.check(r.app().bodyCount() == 1 && std::abs(r.bodyVolume() - volume) < 1e-6, "text: the same text after reopening",
                    AcceptanceRunner::num(r.bodyVolume()));
            const auto* reopened = dynamic_cast<const doc::TextFeature*>(r.body(0).features().back().get());
            r.check(reopened && reopened->text == "OK" && std::abs(reopened->size - 6) < 1e-12
                        && std::abs(reopened->depth - 1) < 1e-12,
                    "text: its words, size and depth are kept");
            QFile::remove(path);
            r.app().interaction().fitAll(false);
        },
        [] {},
        // Cut in from the palette: a face, Text in the Modify palette, Deboss.
        [&r] {
            r.click(r.screenPoint(8, -8, 5));
            r.check(r.clickItem(QStringLiteral("tool_text")), "text: Text in the Modify palette");
        },
        [] {},
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool != nullptr, "text: the tool on the clicked face", r.app().operationTitle());
            r.check(tool && tool->text() == "OK", "text: it remembers the words");
            r.check(r.clickItem(QStringLiteral("action_deboss")), "text: Deboss");
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool && tool->depth() < 0, "text: cut in",
                    tool ? AcceptanceRunner::num(tool->depth()) : QString());
            // Move it by a click on the face (it snaps to the edge's middle row).
            r.click(r.screenPoint(0, -6.5, 5));
            r.check(tool && std::abs(tool->position().x) < 1e-6, "text: a click near the middle lines it up",
                    tool ? AcceptanceRunner::num(tool->position().x) : QString());
        },
        [&r, s] {
            const double before = r.bodyVolume();
            r.key(Qt::Key_Return);
            const double area = letterArea("OK", 6);
            r.check(std::abs((before - r.bodyVolume()) - area * 1.0) < 1e-4 * area, "text: the cut removes the letters' area x 1 mm",
                    AcceptanceRunner::num(before - r.bodyVolume()));
            r.screenshot(QStringLiteral("text_cut"));
            QObject::disconnect(s->listening);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("text"), 75, steps});

} // namespace
} // namespace os::app
