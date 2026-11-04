// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// AppController: the measurements kept in the project (docs/MEASURE.md,
// "Measurements in the project"). The document holds them
// (doc::Document::measurements); every change is one undo step
// (cmd::SetProjectMeasurementsCommand, through the interaction core).

#include "ui/AppController.h"

#include "core/Units.h"
#include "document/ProjectMeasurement.h"
#include "measure/Measurement.h"

#include <QtCore/QVariantMap>

#include <algorithm>
#include <cmath>

namespace os::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

measure::Quantity measureQuantity(ValueKind kind)
{
    return kind == ValueKind::Angle ? measure::Quantity::Angle : measure::Quantity::Length;
}

// A note as kept: at most kMaxMeasurementNoteBytes, cut at a character's start.
std::string keptNote(const QString& note)
{
    std::string text = note.trimmed().toStdString();
    if (text.size() > doc::kMaxMeasurementNoteBytes) {
        std::size_t end = doc::kMaxMeasurementNoteBytes;
        while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
            --end;
        text.resize(end);
    }
    return text;
}

} // namespace

QVariantList AppController::projectMeasurementsFromDocument() const
{
    QVariantList list;
    const LengthUnit unit = document_->displayUnit();
    for (const doc::ProjectMeasurement& m : document_->measurements()) {
        QVariantMap map;
        map.insert(QStringLiteral("name"), qs(m.name));
        map.insert(QStringLiteral("quantity"), QString::fromLatin1(doc::valueKindName(m.quantity)));
        map.insert(QStringLiteral("value"), m.value);
        map.insert(QStringLiteral("text"), qs(measure::formatMeasurement(measureQuantity(m.quantity), m.value, unit)));
        map.insert(QStringLiteral("typed"), qs(measure::valueText(measureQuantity(m.quantity), m.value, unit)));
        map.insert(QStringLiteral("note"), qs(m.note));
        list.append(map);
    }
    return list;
}

QString AppController::identifierFor(const QString& displayName) const
{
    return qs(valueNameFor(displayName.trimmed().toStdString()));
}

QString AppController::addProjectMeasurement(const QString& name, const QString& quantity, double value, const QString& note)
{
    const auto kind = doc::valueKindFromName(quantity.toStdString());
    if (!kind || !std::isfinite(value) || std::abs(value) > doc::kMaxMeasurementValue)
        return {};
    std::vector<doc::ProjectMeasurement> list = document_->measurements();
    std::string base;
    if (name.trimmed().isEmpty()) {
        // length_3: the third length kept.
        int count = 0;
        for (const auto& m : list)
            count += m.quantity == *kind ? 1 : 0;
        base = std::string(doc::valueKindName(*kind)) + "_" + std::to_string(count + 1);
    } else {
        // A name as given ("Wall", "w2"); a display name made one ("Length 3": length_3).
        const std::string given = name.trimmed().toStdString();
        base = isValueName(given) ? given : valueNameFor(given);
    }
    doc::ProjectMeasurement m;
    m.name = doc::uniqueMeasurementName(list, base);
    m.quantity = *kind;
    m.value = value;
    m.note = keptNote(note);
    list.push_back(m);
    const Status status = interaction_->setProjectMeasurements(std::move(list), "Add measurement");
    if (!status) {
        notifyMessage(qs(status.userMessage()));
        return {};
    }
    return qs(m.name);
}

QVariantMap AppController::addProjectMeasurementTyped(const QString& name, const QString& quantity, const QString& text)
{
    QVariantMap result;
    result.insert(QStringLiteral("name"), QString());
    const auto kind = doc::valueKindFromName(quantity.toStdString());
    if (!kind) {
        result.insert(QStringLiteral("error"), QStringLiteral("Choose Length or Angle."));
        return result;
    }
    // A name typed: it must be one (or become one); a taken name is refused here (the
    // panel shows what was typed, so no quiet _2).
    std::string chosen;
    if (!name.trimmed().isEmpty()) {
        const std::string typed = name.trimmed().toStdString();
        chosen = isValueName(typed) ? typed : valueNameFor(typed);
        if (document_->measurement(chosen)) {
            result.insert(QStringLiteral("error"), qs("There is already a measurement called " + chosen + "."));
            return result;
        }
    }
    const auto names = document_->valueNames();
    const std::string input = text.toStdString();
    const auto parsed = *kind == ValueKind::Angle ? parseAngle(input, names) : parseLength(input, document_->displayUnit(), names);
    if (!parsed.millimeters) {
        result.insert(QStringLiteral("error"), qs(parsed.error));
        return result;
    }
    const double value = *kind == ValueKind::Angle ? *parsed.millimeters * 180.0 / 3.14159265358979323846 : *parsed.millimeters;
    const QString used = addProjectMeasurement(qs(chosen), quantity, value);
    result.insert(QStringLiteral("name"), used);
    result.insert(QStringLiteral("error"), used.isEmpty() ? QStringLiteral("That value is too large.") : QString());
    return result;
}

QString AppController::renameProjectMeasurement(const QString& name, const QString& newName)
{
    std::vector<doc::ProjectMeasurement> list = document_->measurements();
    auto it = std::find_if(list.begin(), list.end(), [&](const doc::ProjectMeasurement& m) { return sameValueName(m.name, name.toStdString()); });
    if (it == list.end())
        return QStringLiteral("No measurement called '%1' in this project").arg(name);
    const std::string typed = newName.trimmed().toStdString();
    if (typed.empty())
        return QStringLiteral("Give it a name.");
    // As typed when it is a name ("W"); "Shelf depth" becomes shelf_depth.
    const std::string chosen = isValueName(typed) ? typed : valueNameFor(typed);
    if (const std::string error = valueNameError(chosen); !error.empty())
        return qs(error);
    for (const auto& other : list)
        if (&other != &*it && sameValueName(other.name, chosen))
            return qs("There is already a measurement called " + other.name + ".");
    if (it->name == chosen)
        return {};
    it->name = chosen;
    const Status status = interaction_->setProjectMeasurements(std::move(list), "Rename measurement");
    return status ? QString() : qs(status.userMessage());
}

QString AppController::setProjectMeasurementValue(const QString& name, const QString& text)
{
    std::vector<doc::ProjectMeasurement> list = document_->measurements();
    auto it = std::find_if(list.begin(), list.end(), [&](const doc::ProjectMeasurement& m) { return sameValueName(m.name, name.toStdString()); });
    if (it == list.end())
        return QStringLiteral("No measurement called '%1' in this project").arg(name);
    // As any value box reads it (other measurements by name too; its own
    // name is its value until now: "wall + 2").
    const auto names = document_->valueNames();
    const std::string input = text.toStdString();
    const auto parsed = it->quantity == ValueKind::Angle ? parseAngle(input, names) : parseLength(input, document_->displayUnit(), names);
    if (!parsed.millimeters)
        return qs(parsed.error);
    const double value = it->quantity == ValueKind::Angle ? *parsed.millimeters * 180.0 / 3.14159265358979323846 : *parsed.millimeters;
    if (std::abs(value) > doc::kMaxMeasurementValue)
        return QStringLiteral("That value is too large.");
    it->value = value;
    const Status status = interaction_->setProjectMeasurements(std::move(list), "Change measurement");
    return status ? QString() : qs(status.userMessage());
}

void AppController::removeProjectMeasurement(const QString& name)
{
    std::vector<doc::ProjectMeasurement> list = document_->measurements();
    const auto it = std::find_if(list.begin(), list.end(), [&](const doc::ProjectMeasurement& m) { return sameValueName(m.name, name.toStdString()); });
    if (it == list.end())
        return;
    list.erase(it);
    if (const Status status = interaction_->setProjectMeasurements(std::move(list), "Delete measurement"); !status)
        notifyMessage(qs(status.userMessage()));
}

} // namespace os::ui
