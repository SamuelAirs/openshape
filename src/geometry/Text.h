// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "geometry/Shape.h"

#include <string>
#include <vector>

// Text raised from or cut into a flat face (emboss / deboss). The letters
// come from a TrueType font through OpenCASCADE's font support (FreeType):
// each glyph's outline becomes a flat face (with holes for counters, as in
// O, A, B), extruded and fused with or cut from the body.
namespace os::geom {

// ---- Fonts ----------------------------------------------------------------------
// Fonts are registered by id before text is made: the app registers the
// Noto Sans it ships from its resources at startup, tests from the source
// tree. The geometry layer never looks for fonts on the system, so the same
// step gives the same letters everywhere. Registering an id again replaces
// it. Returns false (registering nothing) for bytes FreeType cannot read or
// when this build of OpenCASCADE has no font support.
bool registerFont(const std::string& id, std::string bytes);
bool hasFont(const std::string& id);
std::vector<std::string> registeredFonts();

// One line of text in a registered font. The size is the height of the
// capital letters (an "H"), in mm: 10 makes capitals 10 mm tall, whatever
// the font; lowercase descenders (g, p, y) reach below the baseline.
struct TextSpec {
    std::string text;      // UTF-8
    std::string font;      // a registered id
    double capHeight = 10; // mm
};
inline constexpr double kMinCapHeight = 0.5;    // mm: finer than a nozzle cannot print
inline constexpr double kMaxCapHeight = 1000.0; // mm
inline constexpr std::size_t kMaxTextLength = 200; // characters

// Why this text cannot be made, in plain words ("" = it can): nothing typed,
// not valid UTF-8, more than one line, a character the font does not have,
// a font that is not there, a size out of range.
std::string checkText(const TextSpec& spec);

// The letters as flat faces on the XY plane (outward normal +Z), in mm,
// centered on the origin: horizontally on the letters' extent, vertically on
// the capital height (the baseline at y = -capHeight / 2). Spaces take their
// width; a text of only spaces has no letters and is refused.
Result<Shape> textFaces(const TextSpec& spec);

// Where text goes on a flat face.
struct TextFrame {
    Vec3 center; // the text's center (as in textFaces), on the face
    Vec3 xAxis;  // the direction the text runs (unit, in the face's plane)
    Vec3 normal; // the face's outward normal (unit)
};
// The letters' faces placed in `frame` (for tests and previews).
Result<Shape> placedTextFaces(const TextSpec& spec, const TextFrame& frame);

// Letters extruded by |depth| mm from the face's plane: depth > 0 raises
// them outward and joins them to the body (emboss), depth < 0 cuts them into
// it (deboss). Checked: the body gains (or loses) some volume and no more
// than the letters' area x |depth|; a text that misses the body is refused.
Result<Shape> embossText(const Shape& body, const TextSpec& spec, const TextFrame& frame, double depth);

} // namespace os::geom
