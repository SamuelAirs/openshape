// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Text on a face ("text"): the top face's Text action, the words typed into
// the chip's text field, the size and a 1 mm depth typed, the letters turned
// (90 degrees: their X and Y extents on the preview swap; the Angle field, 45
// typed; back to 0), applied (the volume grows by the letters' area x 1 mm),
// the words changed in the Model panel, undo, and a project saved and
// reopened with the text. Then from the palette on the same face: Deboss, a
// click on the face moves the words, a digit typed in the view goes to the
// words, Emboss and Deboss again, Bold, applied (the cut removes the bold
// letters' area x 1 mm). Needs the built-in font (resources/fonts/).

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

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <optional>

namespace os::app {
namespace {

const interact::TextOperation* textTool(AcceptanceRunner& r)
{
    return dynamic_cast<const interact::TextOperation*>(r.app().interaction().operation());
}

// The letters' area for `words` at `size` (capital height) in the built-in font.
double letterArea(const std::string& words, double size, bool bold = false)
{
    auto faces = geom::textFaces({words, bold ? doc::kTextFontBold : doc::kTextFontRegular, size});
    return faces ? geom::surfaceArea(faces.value()) : -1.0;
}

// The extent of the shown preview's vertices that pass `keep` (e.g. those
// above the plate: the raised letters), measured on the preview the worker made.
struct Extent {
    double minX = std::numeric_limits<double>::max(), maxX = -std::numeric_limits<double>::max();
    double minY = std::numeric_limits<double>::max(), maxY = -std::numeric_limits<double>::max();
    double maxZ = -std::numeric_limits<double>::max();
    double width() const { return maxX - minX; }
    double height() const { return maxY - minY; }
};
template <typename Keep>
std::optional<Extent> previewExtent(AcceptanceRunner& r, Keep keep)
{
    const auto* op = r.app().interaction().operation();
    if (!op || !op->previewMesh())
        return std::nullopt;
    const geom::Mesh& mesh = *op->previewMesh();
    Extent e;
    bool any = false;
    for (std::size_t i = 0; i < mesh.vertexCount(); ++i) {
        const Vec3 v = mesh.vertex(i);
        if (!keep(v))
            continue;
        any = true;
        e.minX = std::min(e.minX, v.x);
        e.maxX = std::max(e.maxX, v.x);
        e.minY = std::min(e.minY, v.y);
        e.maxY = std::max(e.maxY, v.y);
        e.maxZ = std::max(e.maxZ, v.z);
    }
    return any ? std::optional<Extent>(e) : std::nullopt;
}

QString extentText(const std::optional<Extent>& e)
{
    return e ? AcceptanceRunner::num(e->width()) + QStringLiteral(" x ") + AcceptanceRunner::num(e->height())
             : QStringLiteral("no preview");
}

// Above the plate's top (z = 5): raised letters.
bool raised(const Vec3& v)
{
    return v.z > 5 + 1e-3;
}

QString idOf(const doc::Feature& f)
{
    return QString::fromStdString(f.id().toString());
}

struct State {
    double plate = 0; // the plate's volume before the text
    Extent flat;      // the raised letters' extent at 0 degrees
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
        [&r, s] {
            r.screenshot(QStringLiteral("text_preview"));
            const auto flat = previewExtent(r, raised);
            r.check(flat && flat->width() > flat->height() + 1 && std::abs(flat->maxZ - 6) < 1e-4,
                    "text: the letters run along X, 1 mm high", extentText(flat));
            if (flat)
                s->flat = *flat;
            r.check(r.clickItem(QStringLiteral("action_angle:90")), "text: the 90 degrees button");
        },
        [&r, s] {
            const auto* tool = textTool(r);
            r.check(tool && std::abs(tool->angleDegrees() - 90) < 1e-9, "text: turned to 90 degrees",
                    tool ? AcceptanceRunner::num(tool->angleDegrees()) : QString());
            const auto turned = previewExtent(r, raised);
            r.check(turned && std::abs(turned->width() - s->flat.height()) < 0.2
                        && std::abs(turned->height() - s->flat.width()) < 0.2,
                    "text: the letters turned a quarter (X and Y extents swap)",
                    extentText(turned) + QStringLiteral(" (flat ") + extentText(s->flat) + QStringLiteral(")"));
            r.screenshot(QStringLiteral("text_turned"));
            r.check(r.clickItem(QStringLiteral("action_field:angle")), "text: the Angle field");
        },
        [&r] {
            r.check(r.app().operationValueLabel() == QStringLiteral("Angle"), "text: the chip edits the angle",
                    r.app().operationValueLabel());
            r.type(QStringLiteral("45"));
        },
        [&r, s] {
            const auto* tool = textTool(r);
            r.check(tool && std::abs(tool->angleDegrees() - 45) < 1e-9 && r.app().operationCanCommit(),
                    "text: 45 degrees typed, and it previews",
                    tool ? AcceptanceRunner::num(tool->angleDegrees()) + QStringLiteral(" ") + r.app().operationError() : QString());
            const auto slanted = previewExtent(r, raised);
            r.check(slanted && slanted->height() > s->flat.height() + 1 && slanted->width() < s->flat.width() + 1,
                    "text: the letters slant at 45 degrees", extentText(slanted));
            r.check(r.clickItem(QStringLiteral("action_angle:0")), "text: back to 0 degrees");
        },
        [&r, s] {
            const auto* tool = textTool(r);
            const auto flat = previewExtent(r, raised);
            r.check(tool && tool->angleDegrees() == 0 && flat && std::abs(flat->width() - s->flat.width()) < 1e-3
                        && std::abs(flat->height() - s->flat.height()) < 1e-3,
                    "text: flat again", extentText(flat));
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
            r.check(tool && tool->angleDegrees() == 0, "text: and the angle");
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
        [&r] {
            // The view has the keys after the click: a digit goes to the words.
            r.type(QStringLiteral("2"));
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(r.app().operationText() == QStringLiteral("OK2"), "text: a digit typed in the view goes to the words",
                    r.app().operationText());
            r.check(tool && std::abs(tool->depth() + 1) < 1e-9, "text: not to the depth",
                    tool ? AcceptanceRunner::num(tool->depth()) : QString());
            r.key(Qt::Key_Backspace);
        },
        [&r] {
            r.check(r.app().operationText() == QStringLiteral("OK"), "text: Backspace takes it back", r.app().operationText());
            r.check(r.clickItem(QStringLiteral("action_emboss")), "text: Emboss");
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool && tool->depth() > 0 && r.app().operationCanCommit(), "text: raised again",
                    tool ? AcceptanceRunner::num(tool->depth()) + QStringLiteral(" ") + r.app().operationError() : QString());
            // Raised where the click moved them (the first words end at y = -3.1).
            const auto moved = previewExtent(r, [](const Vec3& v) { return raised(v) && v.y < -3.3; });
            r.check(moved && std::abs(moved->maxZ - 6) < 1e-4 && std::abs((moved->minY + moved->maxY) / 2 + 6.5) < 0.3,
                    "text: the preview raises them where they were moved", extentText(moved));
            r.check(r.clickItem(QStringLiteral("action_deboss")), "text: Deboss again");
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool && tool->depth() < 0, "text: cut in again", tool ? AcceptanceRunner::num(tool->depth()) : QString());
            r.check(r.clickItem(QStringLiteral("action_bold")), "text: Bold");
        },
        [&r] {
            const auto* tool = textTool(r);
            r.check(tool && tool->bold() && r.app().operationCanCommit(), "text: bold letters", r.app().operationError());
        },
        [&r, s] {
            const double before = r.bodyVolume();
            r.key(Qt::Key_Return);
            const double regular = letterArea("OK", 6), bold = letterArea("OK", 6, true);
            r.check(bold > regular * 1.1, "text: bold letters are heavier",
                    AcceptanceRunner::num(bold) + QStringLiteral(" vs ") + AcceptanceRunner::num(regular));
            r.check(std::abs((before - r.bodyVolume()) - bold * 1.0) < 1e-4 * bold,
                    "text: the cut removes the bold letters' area x 1 mm", AcceptanceRunner::num(before - r.bodyVolume()));
            const auto* step = dynamic_cast<const doc::TextFeature*>(r.body(0).features().back().get());
            r.check(step && step->font == doc::kTextFontBold && step->depth < 0, "text: a bold, cut-in step",
                    step ? QString::fromStdString(step->font) : QString());
            r.screenshot(QStringLiteral("text_cut"));
            QObject::disconnect(s->listening);
        },
    };
}

const bool registered = registerAcceptanceScenario({QStringLiteral("text"), 75, steps});

} // namespace
} // namespace os::app
