// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "measure/Measurement.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace os::measure {

namespace {

int lengthDecimals(LengthUnit unit)
{
    switch (unit) {
    case LengthUnit::Millimeter: return 1;
    case LengthUnit::Centimeter: return 2;
    case LengthUnit::Meter: return 4;
    case LengthUnit::Inch: return 3;
    }
    return 2;
}

std::string number(double value, int decimals)
{
    if (std::abs(value) < 0.5 * std::pow(10.0, -decimals))
        value = 0.0; // no "-0.0"
    char buffer[64];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

} // namespace

std::string quantityName(Quantity q)
{
    switch (q) {
    case Quantity::Length: return "Length";
    case Quantity::Angle: return "Angle";
    case Quantity::Area: return "Area";
    }
    return "Length";
}

std::optional<Quantity> quantityFromName(const std::string& name)
{
    if (name == "Length")
        return Quantity::Length;
    if (name == "Angle")
        return Quantity::Angle;
    if (name == "Area")
        return Quantity::Area;
    return std::nullopt;
}

std::string formatMeasurement(Quantity q, double value, LengthUnit unit)
{
    switch (q) {
    case Quantity::Length:
        return number(fromMillimeters(value, unit), lengthDecimals(unit)) + " " + std::string(unitSymbol(unit));
    case Quantity::Angle:
        return number(value, 1) + "\xC2\xB0";
    case Quantity::Area:
        if (unit == LengthUnit::Inch)
            return number(value / (25.4 * 25.4), 2) + " in\xC2\xB2";
        if (std::abs(value) >= 10000.0)
            return number(value / 100.0, 1) + " cm\xC2\xB2";
        return number(value, 0) + " mm\xC2\xB2";
    }
    return number(value, 2);
}

std::string valueText(Quantity q, double value, LengthUnit unit)
{
    switch (q) {
    case Quantity::Length:
        return number(fromMillimeters(value, unit), lengthDecimals(unit)) + std::string(unitSymbol(unit));
    case Quantity::Angle:
        return number(value, 1) + "\xC2\xB0";
    case Quantity::Area:
        return formatMeasurement(q, value, unit);
    }
    return number(value, 2);
}

std::string MeasurementLog::add(Quantity quantity, double value, const std::string& source, std::int64_t timeMs,
                                const std::string& name)
{
    Measurement m;
    m.id = "m" + std::to_string(nextId_++);
    m.quantity = quantity;
    m.value = value;
    m.source = source;
    m.timeMs = timeMs;
    if (!name.empty()) {
        m.name = name;
    } else {
        // "Length 3": one more than the highest number that name has now.
        const std::string base = quantityName(quantity) + " ";
        int highest = 0;
        for (const Measurement& other : items_) {
            if (other.name.rfind(base, 0) != 0)
                continue;
            try {
                std::size_t used = 0;
                const int n = std::stoi(other.name.substr(base.size()), &used);
                if (used == other.name.size() - base.size())
                    highest = std::max(highest, n);
            } catch (...) {
            }
        }
        m.name = base + std::to_string(highest + 1);
    }
    items_.insert(items_.begin(), m);
    if (items_.size() > kMaxItems)
        items_.resize(kMaxItems);
    return m.id;
}

bool MeasurementLog::rename(const std::string& id, const std::string& name)
{
    if (name.empty())
        return false;
    for (Measurement& m : items_)
        if (m.id == id) {
            m.name = name;
            return true;
        }
    return false;
}

bool MeasurementLog::remove(const std::string& id)
{
    const auto it = std::find_if(items_.begin(), items_.end(), [&](const Measurement& m) { return m.id == id; });
    if (it == items_.end())
        return false;
    items_.erase(it);
    return true;
}

const Measurement* MeasurementLog::find(const std::string& id) const
{
    for (const Measurement& m : items_)
        if (m.id == id)
            return &m;
    return nullptr;
}

std::string MeasurementLog::toJson() const
{
    nlohmann::json list = nlohmann::json::array();
    for (const Measurement& m : items_)
        list.push_back({{"id", m.id},
                        {"name", m.name},
                        {"quantity", quantityName(m.quantity)},
                        {"value", m.value},
                        {"source", m.source},
                        {"time", m.timeMs}});
    return list.dump();
}

bool MeasurementLog::fromJson(const std::string& json)
{
    const nlohmann::json list = nlohmann::json::parse(json, nullptr, false);
    if (list.is_discarded() || !list.is_array())
        return false;
    std::vector<Measurement> items;
    int highestId = 0;
    for (const auto& entry : list) {
        if (!entry.is_object())
            continue;
        const auto quantity = quantityFromName(entry.value("quantity", std::string()));
        const auto value = entry.find("value");
        if (!quantity || value == entry.end() || !value->is_number())
            continue;
        Measurement m;
        m.id = entry.value("id", std::string());
        m.name = entry.value("name", std::string());
        m.quantity = *quantity;
        m.value = value->get<double>();
        m.source = entry.value("source", std::string());
        m.timeMs = entry.value("time", std::int64_t(0));
        if (!std::isfinite(m.value) || m.name.empty())
            continue;
        if (m.id.size() > 1 && m.id[0] == 'm') {
            try {
                highestId = std::max(highestId, std::stoi(m.id.substr(1)));
            } catch (...) {
            }
        }
        items.push_back(std::move(m));
        if (items.size() >= kMaxItems)
            break;
    }
    // Ids made fresh where missing or repeated.
    items_.clear();
    nextId_ = highestId + 1;
    for (Measurement& m : items) {
        if (m.id.empty() || find(m.id))
            m.id = "m" + std::to_string(nextId_++);
        items_.push_back(std::move(m));
    }
    return true;
}

} // namespace os::measure
