// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "document/ProjectMeasurement.h"

#include <nlohmann/json.hpp>

#include <cmath>

namespace os::doc {

namespace {
constexpr double kPiMeasurements = 3.14159265358979323846;
}

const char* valueKindName(ValueKind kind) { return kind == ValueKind::Angle ? "angle" : "length"; }

std::optional<ValueKind> valueKindFromName(std::string_view name)
{
    if (name == "length")
        return ValueKind::Length;
    if (name == "angle")
        return ValueKind::Angle;
    return std::nullopt;
}

std::string checkProjectMeasurement(const ProjectMeasurement& measurement)
{
    if (std::string error = valueNameError(measurement.name); !error.empty())
        return error;
    if (!std::isfinite(measurement.value) || std::abs(measurement.value) > kMaxMeasurementValue)
        return "That value is too large.";
    if (measurement.note.size() > kMaxMeasurementNoteBytes)
        return "The note is too long.";
    return {};
}

std::string checkProjectMeasurements(const std::vector<ProjectMeasurement>& measurements)
{
    if (measurements.size() > kMaxProjectMeasurements)
        return "A project keeps at most " + std::to_string(kMaxProjectMeasurements) + " measurements.";
    for (std::size_t i = 0; i < measurements.size(); ++i) {
        if (std::string error = checkProjectMeasurement(measurements[i]); !error.empty())
            return error;
        for (std::size_t j = 0; j < i; ++j)
            if (sameValueName(measurements[i].name, measurements[j].name))
                return "There is already a measurement called " + measurements[j].name + ".";
    }
    return {};
}

std::string uniqueMeasurementName(const std::vector<ProjectMeasurement>& measurements, const std::string& base)
{
    auto taken = [&](const std::string& name) {
        for (const auto& m : measurements)
            if (sameValueName(m.name, name))
                return true;
        return false;
    };
    if (!taken(base))
        return base;
    for (int n = 2;; ++n) {
        std::string name = base + "_" + std::to_string(n);
        if (!taken(name))
            return name;
    }
}

nlohmann::json toJson(const ProjectMeasurement& measurement)
{
    nlohmann::json out{{"name", measurement.name},
                       {"quantity", valueKindName(measurement.quantity)},
                       {"value", measurement.quantity == ValueKind::Angle ? measurement.value * kPiMeasurements / 180.0 : measurement.value}};
    if (!measurement.note.empty())
        out["note"] = measurement.note;
    return out;
}

std::optional<ProjectMeasurement> projectMeasurementFromJson(const nlohmann::json& json)
{
    if (!json.is_object() || !json.contains("name") || !json["name"].is_string() || !json.contains("quantity")
        || !json["quantity"].is_string() || !json.contains("value") || !json["value"].is_number())
        return std::nullopt;
    const auto kind = valueKindFromName(json["quantity"].get<std::string>());
    if (!kind)
        return std::nullopt;
    ProjectMeasurement m;
    m.name = json["name"].get<std::string>();
    m.quantity = *kind;
    const double stored = json["value"].get<double>();
    // (Degrees to a billionth: 45° comes back as 45, not 45.000000000000007.)
    m.value = *kind == ValueKind::Angle ? std::round(stored * 180.0 / kPiMeasurements * 1e9) / 1e9 : stored;
    if (json.contains("note") && json["note"].is_string())
        m.note = json["note"].get<std::string>();
    if (!checkProjectMeasurement(m).empty())
        return std::nullopt;
    return m;
}

} // namespace os::doc
