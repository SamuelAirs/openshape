// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE).

#include "geometry/internal/BrepScreen.h"

#include <BRepBndLib.hxx>
#include <Bnd_Box.hxx>

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace os::geom::detail {

namespace {

constexpr long long kLooseCeiling = 10'000'000;
// OCCT reads several tokens into 255-byte buffers on the stack (shape
// types, flags, references). BRepTools::Write's longest is a number of
// about 25 characters.
constexpr std::size_t kLongestToken = 128;

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

bool allDigits(std::string_view s)
{
    if (s.empty())
        return false;
    for (char c : s)
        if (!isDigit(c))
            return false;
    return true;
}

// The value of a run of digits; nullopt-like -1 when it has 19 digits or more
// (more than any count or index can be, and beyond long long's range).
long long digitsValue(std::string_view digits)
{
    if (digits.size() >= 19)
        return -1;
    long long value = 0;
    std::from_chars(digits.data(), digits.data() + digits.size(), value);
    return value;
}

bool refuse(std::string* why, std::string reason)
{
    if (why)
        *why = std::move(reason);
    return false;
}

std::vector<std::string_view> tokensOf(std::string_view line)
{
    std::vector<std::string_view> tokens;
    std::size_t i = 0;
    while (i < line.size()) {
        while (i < line.size() && isSpace(line[i]))
            ++i;
        const std::size_t start = i;
        while (i < line.size() && !isSpace(line[i]))
            ++i;
        if (i > start)
            tokens.push_back(line.substr(start, i - start));
    }
    return tokens;
}

// A sub-shape reference: an orientation mark (+ forward, - reversed,
// i internal, e external) glued to a 1-based index into the TShapes table.
bool isReference(std::string_view token)
{
    return token.size() >= 2 && (token[0] == '+' || token[0] == '-' || token[0] == 'i' || token[0] == 'e')
        && allDigits(token.substr(1));
}

// Whether the line is a list of sub-shape references: pairs of a reference
// and a location index ("+12 0 -3 2"), ten to a line, the last line of a
// list ending with "*" (an empty list is a lone "*"). The root shape's
// reference, the text's last line, is one pair.
bool isReferenceLine(const std::vector<std::string_view>& tokens)
{
    std::size_t count = tokens.size();
    if (count > 0 && tokens.back() == "*")
        --count;
    if (count % 2 != 0)
        return false;
    for (std::size_t k = 0; k < count; k += 2)
        if (!isReference(tokens[k]) || !allDigits(tokens[k + 1]))
            return false;
    return true;
}

// References count back from the end of the table (OCCT reads shape
// shapes - index + 1): a sub-shape is written before the shapes that hold
// it, so the record numbered `record` may only name records before it.
// `record` is 0 for the root reference, which may name any record.
bool checkReferences(const std::vector<std::string_view>& tokens, long long shapes, long long record, long long locations,
                     std::string* why)
{
    const std::size_t count = tokens.size() - (tokens.back() == "*" ? 1 : 0);
    const long long lowest = record > 0 ? shapes - record + 2 : 1;
    for (std::size_t k = 0; k < count; k += 2) {
        const long long index = digitsValue(tokens[k].substr(1));
        const long long location = digitsValue(tokens[k + 1]);
        if (index < lowest || index > shapes)
            return refuse(why, "a sub-shape reference to shape " + std::string(tokens[k]) + " of " + std::to_string(shapes)
                                   + (record > 0 ? " in record " + std::to_string(record) : std::string(" at the root")));
        if (location < 0 || location > locations)
            return refuse(why, "a sub-shape reference with location " + std::string(tokens[k + 1]) + " of "
                                   + std::to_string(locations));
    }
    return true;
}

// Every token outside the reference lines.
bool checkNumber(std::string_view token, long long ceiling, std::string* why)
{
    std::string_view body = token;
    if (!body.empty() && (body[0] == '+' || body[0] == '-')) {
        body.remove_prefix(1);
        if (!body.empty() && (body[0] == '+' || body[0] == '-'))
            return refuse(why, "a doubled sign in '" + std::string(token) + "'");
    }
    if (body.size() >= 2 && body[0] == '0' && (body[1] == 'x' || body[1] == 'X'))
        return refuse(why, "a hexadecimal number '" + std::string(token) + "'");
    if (allDigits(body)) {
        const long long value = digitsValue(body);
        if (value < 0 || value > ceiling)
            return refuse(why, "the number " + std::string(token) + " is out of range");
        return true;
    }
    // Words (shape types, the header) and finite decimals pass; a number
    // that parses as a whole to infinity or NaN does not.
    if (token.size() < 64) {
        char buffer[64];
        std::copy(token.begin(), token.end(), buffer);
        buffer[token.size()] = '\0';
        char* end = nullptr;
        const double value = std::strtod(buffer, &end);
        if (end == buffer + token.size() && !std::isfinite(value))
            return refuse(why, "the non-finite number '" + std::string(token) + "'");
    }
    return true;
}

} // namespace

// Adapted from OS3DPlausibleSectionCounts (OCCTBridge.mm): the byte, count,
// reference and number rules; the reference rule here reads the TShapes
// section's reference lines, so negative coordinates are not mistaken for
// references.
bool plausibleBrepText(std::string_view text, std::string* why)
{
    for (char c : text) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte == '\t' || byte == '\n' || byte == '\r')
            continue;
        if (byte < 0x20 || byte > 0x7E)
            return refuse(why, "a byte that is not text (" + std::to_string(int(byte)) + ")");
    }
    const long long length = static_cast<long long>(text.size());
    const long long ceiling = std::max(length, kLooseCeiling);
    static constexpr std::array<std::string_view, 8> kSections = {
        "Locations", "Curve2ds", "Curves", "Polygon3D", "PolygonOnTriangulations", "Surfaces", "Triangulations", "TShapes"};

    static constexpr std::array<std::string_view, 8> kShapeTypes = {"Ve", "Ed", "Wi", "Fa", "Sh", "So", "CS", "Co"};
    long long locations = 0, shapes = -1; // -1: the TShapes section has not started
    long long records = 0;                 // TShapes records seen so far
    bool listOpen = false;                 // inside a record's list of sub-shapes (ends with "*")
    bool rootSeen = false;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos)
            end = text.size();
        const std::string_view line = text.substr(pos, end - pos);
        pos = end + 1;
        const auto tokens = tokensOf(line);
        if (tokens.empty())
            continue;

        // "Curves 12": a section and the number of records in it.
        if (tokens.size() == 2 && allDigits(tokens[1])) {
            bool section = false;
            for (std::string_view name : kSections)
                section = section || tokens[0] == name;
            if (section) {
                const long long count = digitsValue(tokens[1]);
                if (count < 0 || count > length)
                    return refuse(why, "the section " + std::string(tokens[0]) + " claims " + std::string(tokens[1])
                                           + " records in " + std::to_string(length) + " bytes");
                if (tokens[0] == "Locations")
                    locations = count;
                if (tokens[0] == "TShapes")
                    shapes = count;
                continue;
            }
        }
        for (std::string_view token : tokens)
            if (token.size() >= kLongestToken)
                return refuse(why, "a token of " + std::to_string(token.size()) + " characters");
        if (shapes >= 0 && tokens.size() == 1) {
            bool type = false;
            for (std::string_view name : kShapeTypes)
                type = type || tokens[0] == name;
            if (type) {
                if (++records > shapes)
                    return refuse(why, "more TShapes records than the " + std::to_string(shapes) + " declared");
                listOpen = true;
                continue;
            }
        }
        if (shapes >= 0 && isReferenceLine(tokens)) {
            const bool root = !listOpen;
            if (root && (rootSeen || tokens.back() == "*" || records != shapes))
                return refuse(why, "a sub-shape reference outside the TShapes records");
            if (!checkReferences(tokens, shapes, root ? 0 : records, locations, why))
                return false;
            rootSeen = rootSeen || root;
            if (tokens.back() == "*")
                listOpen = false;
            continue;
        }
        for (std::string_view token : tokens)
            if (!checkNumber(token, ceiling, why))
                return false;
    }
    if (shapes < 0)
        return refuse(why, "no TShapes section");
    if (records != shapes)
        return refuse(why, std::to_string(records) + " TShapes records of the " + std::to_string(shapes) + " declared");
    if (!rootSeen)
        return refuse(why, "no root shape reference");
    return true;
}

// Adapted from OS3DFiniteBounds (OCCTBridge.mm).
bool finiteBounds(const TopoDS_Shape& shape)
{
    if (shape.IsNull())
        return false;
    Bnd_Box box;
    BRepBndLib::Add(shape, box, false);
    // An open box stands for an infinite shape (its Get() reports a large
    // finite number); 10^9 mm (1000 km) is beyond any real part.
    if (box.IsVoid() || box.IsOpen())
        return false;
    double x0, y0, z0, x1, y1, z1;
    box.Get(x0, y0, z0, x1, y1, z1);
    for (double v : {x0, y0, z0, x1, y1, z1})
        if (!std::isfinite(v) || std::abs(v) > 1e9)
            return false;
    return true;
}

} // namespace os::geom::detail
