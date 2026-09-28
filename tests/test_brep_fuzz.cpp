// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
//
// Portions of this file are adapted from OpenShape3D
// (https://github.com/laanlabs/openshape3d, commit be3f2fc),
// Copyright (c) 2026 Laan Labs and the openshape3d contributors,
// licensed under the MIT License (third_party/openshape3d/LICENSE):
// the mutation classes of their deserialization fuzz harness
// (openshape3dTests/OCCTFuzzTests.swift).

// Stored geometry (a project's imported bodies) is untrusted BRep text.
// Real texts are damaged in the ways that crashed or hung OCCT's reader
// elsewhere: inflated section counts, sub-shape references out of range,
// infinite and NaN numbers, bytes that are not text, overlong tokens and
// truncation. Reading must fail with a plain message or give a shape that
// meshes; never crash or hang. Undamaged texts, including ones with large
// negative coordinates, must all still be read.
#include "TestFonts.h"

#include "geometry/KernelSignals.h"
#include "geometry/Modeling.h"
#include "geometry/Tessellation.h"
#include "geometry/Text.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <sstream>

using namespace os;
using namespace os::geom;

namespace {

struct Blob {
    std::string name;
    std::string text;
    double volume = 0;
};

std::vector<Blob> blobs()
{
    std::vector<Blob> out;
    auto add = [&out](const std::string& name, const Shape& shape) {
        out.push_back({name, toBrepString(shape, false), volume(shape)});
    };
    const Shape box = makeBox({0, 0, 0}, {30, 20, 10}).value();
    add("box", box);
    // Far from the origin, with integral negative coordinates, in a small text.
    add("far box", makeBox({-20000, -5000, -3}, {7, 5, 3}).value());
    std::vector<int> vertical;
    for (int i = 0; i < box.edgeCount(); ++i) {
        const auto info = edgeInfo(box, i);
        if (info && info->kind == CurveKind::Line && std::abs(std::abs(info->tangent.z) - 1) < 1e-9)
            vertical.push_back(i);
    }
    add("rounded box", filletEdges(box, vertical, 3).value());
    add("holed box", booleanOp(box, makeCylinder({15, 10, -5}, {0, 0, 1}, 3, 20).value(), BooleanKind::Subtract).value());
    if (!test::registerTestFont("fuzz-font").empty())
        add("lettered box", embossText(box, {"Rg", "fuzz-font", 6}, {{15, 10, 10}, {1, 0, 0}, {0, 0, 1}}, 1).value());
    return out;
}

// Every line of the text, and the text from lines.
std::vector<std::string> linesOf(const std::string& text)
{
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line))
        lines.push_back(line);
    return lines;
}

std::string join(const std::vector<std::string>& lines)
{
    std::string out;
    for (const auto& line : lines)
        out += line + "\n";
    return out;
}

// What reading one damaged text did.
struct Outcome {
    bool read = false;
    std::string userMessage, developerMessage;
};

// Reads `text`; anything that reads must also mesh (within the time limit).
Outcome readAndMesh(const std::string& text)
{
    const ScopedKernelTimeLimit limit(std::chrono::seconds(10));
    Outcome outcome;
    auto shape = fromBrepString(text);
    outcome.read = shape.ok();
    outcome.userMessage = shape.userMessage();
    outcome.developerMessage = shape.developerMessage();
    if (shape.ok())
        (void)tessellate(shape.value());
    return outcome;
}

// OPENSHAPE_FUZZ_TRACE=1 names each case before it runs (a crash then
// shows which one it was).
void trace(const std::string& line)
{
    if (std::getenv("OPENSHAPE_FUZZ_TRACE")) {
        std::fprintf(stderr, "%s\n", line.c_str());
        std::fflush(stderr);
    }
}

void expectRefused(const std::string& text, const std::string& what)
{
    trace("case " + what);
    const Outcome outcome = readAndMesh(text);
    trace("  -> " + outcome.developerMessage);
    EXPECT_FALSE(outcome.read) << what;
    if (!outcome.read) {
        EXPECT_EQ(outcome.userMessage, "The stored geometry could not be read.") << what << ": " << outcome.developerMessage;
    }
}

// Survives: refused with the plain message, or read.
void expectSurvives(const std::string& text, const std::string& what)
{
    trace("case " + what);
    const Outcome outcome = readAndMesh(text);
    trace("  -> " + outcome.developerMessage);
    if (!outcome.read) {
        EXPECT_EQ(outcome.userMessage, "The stored geometry could not be read.") << what;
    }
}

} // namespace

TEST(BrepFuzz, UndamagedTextsAreRead)
{
    for (const Blob& blob : blobs()) {
        auto shape = fromBrepString(blob.text);
        ASSERT_TRUE(shape.ok()) << blob.name << ": " << shape.developerMessage();
        EXPECT_NEAR(volume(shape.value()), blob.volume, 1e-9 * blob.volume) << blob.name;
        EXPECT_GT(tessellate(shape.value()).triangleCount(), 0u) << blob.name;
    }
}

TEST(BrepFuzz, InflatedSectionCounts)
{
    for (const Blob& blob : blobs()) {
        const auto lines = linesOf(blob.text);
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::istringstream in(lines[i]);
            std::string section, count;
            in >> section >> count;
            static const std::vector<std::string> kSections = {"Locations", "Curve2ds", "Curves", "Polygon3D",
                                                               "PolygonOnTriangulations", "Surfaces", "Triangulations",
                                                               "TShapes"};
            if (std::find(kSections.begin(), kSections.end(), section) == kSections.end() || count.empty())
                continue;
            for (const std::string& inflated : {std::to_string(blob.text.size() + 1), std::string("2147483647"),
                                                std::string("99999999999999999999999")}) {
                auto damaged = lines;
                damaged[i] = section + " " + inflated;
                expectRefused(join(damaged), blob.name + ": " + section + " " + inflated);
            }
        }
    }
}

TEST(BrepFuzz, ReferencesOutOfRange)
{
    for (const Blob& blob : blobs()) {
        auto lines = linesOf(blob.text);
        // The TShapes count, and the last line: the root reference ("+27 0").
        std::size_t shapes = 0;
        for (const auto& line : lines)
            if (line.rfind("TShapes ", 0) == 0)
                shapes = std::stoul(line.substr(8));
        ASSERT_GT(shapes, 0u) << blob.name;
        while (!lines.empty() && lines.back().find_first_not_of(" \r") == std::string::npos)
            lines.pop_back();
        const std::size_t root = lines.size() - 1;
        ASSERT_EQ(lines[root].front(), '+') << blob.name << ": " << lines[root];
        for (const std::string& ref : {"+" + std::to_string(shapes + 1), std::string("+999999"), std::string("+0"),
                                       std::string("-2147483647")}) {
            auto damaged = lines;
            damaged[root] = ref + " 0";
            expectRefused(join(damaged), blob.name + ": root " + ref);
        }
        auto badLocation = lines;
        badLocation[root] = "+1 77";
        expectRefused(join(badLocation), blob.name + ": root location 77");
        // A reference inside the table: the first line that is a list ending with "*".
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].size() < 3 || lines[i].back() != '*' || (lines[i][0] != '+' && lines[i][0] != '-'))
                continue;
            auto damaged = lines;
            damaged[i] = "+" + std::to_string(shapes + 5) + " 0 *";
            expectRefused(join(damaged), blob.name + ": sub-shape past the table");
            damaged[i] = "+1 2147483647 *";
            expectRefused(join(damaged), blob.name + ": location past the table");
            break;
        }
    }
}

TEST(BrepFuzz, NonFiniteAndOddNumbers)
{
    for (const Blob& blob : blobs()) {
        // Numbers written with a fraction or an exponent ("0.5", "1e-07"),
        // a few of them in turn, become each bad value.
        const auto lines = linesOf(blob.text);
        std::vector<std::pair<std::size_t, std::size_t>> decimals; // line, offset
        for (std::size_t i = 0; i < lines.size(); ++i) {
            std::size_t start = 0;
            while (start < lines[i].size()) {
                std::size_t end = lines[i].find(' ', start);
                if (end == std::string::npos)
                    end = lines[i].size();
                const std::string token = lines[i].substr(start, end - start);
                if (!token.empty() && (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '-')
                    && token.find_first_of(".e") != std::string::npos)
                    decimals.emplace_back(i, start);
                start = end + 1;
            }
        }
        ASSERT_FALSE(decimals.empty()) << blob.name;
        for (std::size_t k = 0; k < decimals.size(); k += decimals.size() / 5 + 1) {
            const auto [line, start] = decimals[k];
            std::size_t end = lines[line].find(' ', start);
            if (end == std::string::npos)
                end = lines[line].size();
            for (const char* bad : {"inf", "-inf", "nan", "1e999", "-1e999", "0x1p3", "+-5", "1e400"}) {
                auto damaged = lines;
                damaged[line] = lines[line].substr(0, start) + bad + lines[line].substr(end);
                expectRefused(join(damaged), blob.name + ": " + bad + " at line " + std::to_string(line));
            }
        }
    }
}

TEST(BrepFuzz, BytesThatAreNotText)
{
    for (const Blob& blob : blobs()) {
        for (unsigned char byte : {0x00, 0x01, 0x7F, 0x80, 0xFF}) {
            for (double at : {0.05, 0.3, 0.6, 0.95}) {
                std::string damaged = blob.text;
                damaged[std::size_t(at * double(damaged.size()))] = static_cast<char>(byte);
                expectRefused(damaged, blob.name + ": byte " + std::to_string(byte));
            }
        }
        // A token longer than OCCT's 255-byte read buffers.
        std::string damaged = blob.text;
        const std::size_t at = damaged.find("TShapes");
        ASSERT_NE(at, std::string::npos);
        const std::size_t lineEnd = damaged.find('\n', damaged.find('\n', at) + 1);
        damaged.insert(lineEnd, " " + std::string(300, 'V'));
        expectRefused(damaged, blob.name + ": overlong token");
    }
}

TEST(BrepFuzz, Truncation)
{
    for (const Blob& blob : blobs()) {
        // Records are missing: refused before OCCT reads them.
        for (int percent = 1; percent < 100; percent += 7)
            expectRefused(blob.text.substr(0, blob.text.size() * std::size_t(percent) / 100),
                          blob.name + ": cut at " + std::to_string(percent) + "%");
        // Cut inside the root reference and just before the end.
        for (std::size_t tail : {1, 2, 3, 5, 9})
            expectSurvives(blob.text.substr(0, blob.text.size() - tail), blob.name + ": " + std::to_string(tail) + " bytes short");
    }
}

// The TShapes count must be the number of records, and a record may only
// name records written before it: OCCT sizes its table by the count and
// indexes it with the references unchecked (both crashed the reader here
// before they were screened).
TEST(BrepFuzz, ShapeTableStructure)
{
    for (const Blob& blob : blobs()) {
        auto lines = linesOf(blob.text);
        std::size_t header = 0, shapes = 0;
        for (std::size_t i = 0; i < lines.size(); ++i)
            if (lines[i].rfind("TShapes ", 0) == 0) {
                header = i;
                shapes = std::stoul(lines[i].substr(8));
            }
        ASSERT_GT(shapes, 1u) << blob.name;
        for (std::size_t count : {shapes - 1, shapes + 1, shapes * 2}) {
            auto damaged = lines;
            damaged[header] = "TShapes " + std::to_string(count);
            expectRefused(join(damaged), blob.name + ": TShapes " + std::to_string(count) + " for " + std::to_string(shapes));
        }
        // The first record with sub-shapes names the last record instead
        // (reference 1 is the last one written: not read yet).
        for (std::size_t i = header + 1; i < lines.size(); ++i) {
            if (lines[i].size() < 3 || lines[i].back() != '*' || (lines[i][0] != '+' && lines[i][0] != '-'))
                continue;
            auto damaged = lines;
            damaged[i] = "+1 0 *";
            expectRefused(join(damaged), blob.name + ": a record naming one not read yet");
            break;
        }
    }
}
