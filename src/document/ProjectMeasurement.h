// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Units.h"

#include <nlohmann/json_fwd.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace os::doc {

// A measurement kept in a project (docs/MEASURE.md, "Measurements in the
// project"): a named length or angle that any value box takes by name
// ("shelf_depth - 2"). The value is copied when it is typed: changing the
// measurement later does not change what was built with it.
struct ProjectMeasurement {
    std::string name; // a value name (core/Units isValueName): "shelf_depth"
    ValueKind quantity = ValueKind::Length;
    double value = 0.0; // millimeters for a length, degrees for an angle
    std::string note;   // where it came from ("Ruler", "Photo"), may be empty

    bool operator==(const ProjectMeasurement&) const = default;
};

inline constexpr std::size_t kMaxProjectMeasurements = 500;
inline constexpr std::size_t kMaxMeasurementNoteBytes = 200;
// Larger values (in millimeters or degrees) are refused: no model needs them.
inline constexpr double kMaxMeasurementValue = 1e7;

// "length", "angle".
const char* valueKindName(ValueKind kind);
std::optional<ValueKind> valueKindFromName(std::string_view name);

// Why this measurement (alone) cannot be kept, or empty.
std::string checkProjectMeasurement(const ProjectMeasurement& measurement);
// Why this list cannot be a project's measurements (each one, and the names
// unique ignoring case), or empty.
std::string checkProjectMeasurements(const std::vector<ProjectMeasurement>& measurements);
// `base` if no measurement has that name (ignoring case), else base_2, base_3...
std::string uniqueMeasurementName(const std::vector<ProjectMeasurement>& measurements, const std::string& base);

// The project file's form (docs/FILE_FORMAT.md, "measurements"): angles in
// radians, as everywhere in the file.
nlohmann::json toJson(const ProjectMeasurement& measurement);
// Null when the entry is not a valid measurement.
std::optional<ProjectMeasurement> projectMeasurementFromJson(const nlohmann::json& json);

} // namespace os::doc
