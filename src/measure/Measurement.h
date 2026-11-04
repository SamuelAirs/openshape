// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Units.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// The measurements kept for designing (docs/MEASURE.md, "Measurements"):
// whatever the ruler, the fold, the level, a photo or the camera measured,
// named and listed newest first, ready to be typed into any value box. The
// app keeps them between runs (AppSettings stores the JSON this writes).
namespace os::measure {

enum class Quantity { Length, Angle, Area };

struct Measurement {
    std::string id;     // unique within the log ("m12")
    std::string name;   // "Length 3", or what the owner called it
    Quantity quantity = Quantity::Length;
    double value = 0;   // mm, degrees or mm²
    std::string source; // "Ruler", "Fold", "Level", "Photo", "Camera", "Caliper"
    std::int64_t timeMs = 0; // when (milliseconds since the Unix epoch)
};

// "Length", "Angle", "Area".
std::string quantityName(Quantity q);
std::optional<Quantity> quantityFromName(const std::string& name);

// For reading: "42.3 mm", "1.665 in", "37.5°", "1,234 mm²", "1.91 in²".
std::string formatMeasurement(Quantity q, double value, LengthUnit unit);
// For typing into a value box (core/Units' parsers read it back exactly to
// the shown precision): "42.3mm", "1.665in", "37.5deg". Areas have no value
// box: their text is formatMeasurement's.
std::string valueText(Quantity q, double value, LengthUnit unit);

class MeasurementLog {
public:
    static constexpr std::size_t kMaxItems = 60;

    // Adds at the top; an empty name becomes "<Quantity> <n>". The oldest
    // drops off past kMaxItems. Returns the new item's id.
    std::string add(Quantity quantity, double value, const std::string& source, std::int64_t timeMs,
                    const std::string& name = {});
    bool rename(const std::string& id, const std::string& name);
    bool remove(const std::string& id);
    void clear() { items_.clear(); }
    const Measurement* find(const std::string& id) const;
    const std::vector<Measurement>& items() const { return items_; } // newest first

    std::string toJson() const;
    // Replaces the items with those in the JSON (bad entries skipped); false
    // when it is not a list of measurements at all.
    bool fromJson(const std::string& json);

private:
    std::vector<Measurement> items_;
    int nextId_ = 1;
};

} // namespace os::measure
