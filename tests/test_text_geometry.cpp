// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Text as geometry: glyph outlines from a registered font as flat faces
// (counters as holes), sized by the capital height, centered, placed on a
// face and raised from it or cut into it. Checked by exact extents, areas
// and volume changes.
#include "TestFonts.h"

#include "geometry/Holes.h"
#include "geometry/Modeling.h"
#include "geometry/Text.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace os;
using namespace os::geom;

namespace {

const std::string kFont = "test-font";

TextSpec spec(const std::string& text, double capHeight = 10)
{
    return TextSpec{text, kFont, capHeight};
}

// A 60 x 30 x 5 plate centered on the origin in X and Y, top face at z = 5.
Shape plate(double thickness = 5)
{
    return makeBox({-30, -15, 0}, {60, 30, thickness}).value();
}

TextFrame onTop(double z = 5, Vec3 center = {0, 0, 0})
{
    return {{center.x, center.y, z}, {1, 0, 0}, {0, 0, 1}};
}

} // namespace

TEST(TextGeometry, FontsMustBeReadable)
{
    EXPECT_FALSE(registerFont("junk", "this is not a font file"));
    EXPECT_FALSE(hasFont("junk"));
    EXPECT_FALSE(registerFont("", "x"));
    OS_REQUIRE_TEST_FONT(kFont);
    EXPECT_TRUE(hasFont(kFont));
    bool listed = false;
    for (const auto& id : registeredFonts())
        listed = listed || id == kFont;
    EXPECT_TRUE(listed);
}

TEST(TextGeometry, RefusesTextItCannotMake)
{
    OS_REQUIRE_TEST_FONT(kFont);
    EXPECT_EQ(checkText(spec("Hello")), "");
    EXPECT_EQ(checkText(spec("")), "Type the text first.");
    EXPECT_EQ(checkText(spec("   ")), "Type the text first.");
    EXPECT_EQ(checkText(spec("two\nlines")), "Text is one line: type it without line breaks.");
    EXPECT_EQ(checkText(spec("tab\there")), "The text contains a control character.");
    EXPECT_EQ(checkText(spec("bad \xC3")), "The text contains characters that cannot be read.");
    EXPECT_EQ(checkText(spec("overlong \xC0\xAF")), "The text contains characters that cannot be read.");
    EXPECT_EQ(checkText(spec("emoji \xF0\x9F\x98\x80")), "This font has no \"\xF0\x9F\x98\x80\": use another character.");
    EXPECT_EQ(checkText(spec("tiny", 0.2)), "The size (the height of capital letters) must be between 0.5 and 1000 mm.");
    EXPECT_EQ(checkText(spec("huge", 5000)), "The size (the height of capital letters) must be between 0.5 and 1000 mm.");
    EXPECT_EQ(checkText(spec(std::string(201, 'x'))), "The text is too long: at most 200 characters.");
    EXPECT_EQ(checkText(TextSpec{"Hi", "NoSuchFont", 10}), "The font \"NoSuchFont\" is not available in this version of OpenShape.");
    EXPECT_EQ(checkText(TextSpec{"Hi", "", 10}), "No font is available for text in this build.");
    // Accented letters are fine.
    EXPECT_EQ(checkText(spec("Caf\xC3\xA9 \xC3\x85")), "");
    auto spaces = textFaces(spec("  "));
    ASSERT_FALSE(spaces);
    EXPECT_EQ(spaces.userMessage(), "Type the text first.");
}

// The size is the capital height; the text is centered on the origin.
TEST(TextGeometry, CapitalHeightAndCentering)
{
    OS_REQUIRE_TEST_FONT(kFont);
    for (double cap : {10.0, 3.5, 42.0}) {
        auto faces = textFaces(spec("HEH", cap));
        ASSERT_TRUE(faces) << faces.developerMessage();
        const BoundingBox box = boundingBox(faces.value());
        EXPECT_NEAR(box.min.y, -cap / 2, 1e-6 * cap) << cap;
        EXPECT_NEAR(box.max.y, cap / 2, 1e-6 * cap) << cap;
        EXPECT_NEAR(box.min.x, -box.max.x, 1e-6 * cap) << "centered horizontally";
        EXPECT_NEAR(box.size().z, 0, 1e-9) << "flat";
        EXPECT_GT(box.size().x, 2 * cap) << "three letters wide";
        // Every face points up, and all have area.
        for (int f = 0; f < faces.value().faceCount(); ++f) {
            const auto info = faceInfo(faces.value(), f);
            ASSERT_TRUE(info && info->isPlanar());
            EXPECT_GT(info->normal.z, 0.9999) << "face " << f;
        }
        EXPECT_GT(surfaceArea(faces.value()), 0);
    }
    // Twice the size: four times the area.
    const double a1 = surfaceArea(textFaces(spec("Size", 5)).value());
    const double a2 = surfaceArea(textFaces(spec("Size", 10)).value());
    EXPECT_NEAR(a2 / a1, 4.0, 1e-6);
    // A descender reaches below the baseline.
    const BoundingBox g = boundingBox(textFaces(spec("Hg", 10)).value());
    EXPECT_LT(g.min.y, -5.5);
    EXPECT_NEAR(g.max.y, 5.0, 0.3);
}

// Counters (the holes in O, A, B) are holes in the letters' faces.
TEST(TextGeometry, LettersWithCountersHaveHoles)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const double cap = 20;
    for (const char* letter : {"O", "A", "D"}) {
        auto faces = textFaces(spec(letter, cap));
        ASSERT_TRUE(faces) << letter;
        ASSERT_EQ(faces.value().faceCount(), 1) << letter;
        const BoundingBox box = boundingBox(faces.value());
        // The middle of the letter (half the capital height) is in its counter.
        const Vec3 middle{(box.min.x + box.max.x) / 2, 0, 0};
        EXPECT_FALSE(faceContains(faces.value(), 0, middle)) << letter << ": its counter is open";
        // The left side of the letter is ink.
        bool ink = false;
        for (double t = 0.02; t < 0.3 && !ink; t += 0.02)
            ink = faceContains(faces.value(), 0, {box.min.x + t * box.size().x, 0, 0});
        EXPECT_TRUE(ink) << letter;
    }
    // B: two counters, above and below its middle bar.
    auto b = textFaces(spec("B", cap));
    ASSERT_TRUE(b);
    ASSERT_EQ(b.value().faceCount(), 1);
    const BoundingBox box = boundingBox(b.value());
    const double x = box.min.x + 0.45 * box.size().x;
    EXPECT_FALSE(faceContains(b.value(), 0, {x, -cap * 0.25, 0})) << "lower counter";
    EXPECT_FALSE(faceContains(b.value(), 0, {x, cap * 0.22, 0})) << "upper counter";
    EXPECT_TRUE(faceContains(b.value(), 0, {x, -cap * 0.5 + 0.2, 0})) << "bottom bar";
    // "i" is two pieces (dot and stem); "OO" two O's.
    EXPECT_EQ(textFaces(spec("i")).value().faceCount(), 2);
    EXPECT_EQ(textFaces(spec("OO")).value().faceCount(), 2);
}

// Raised: the body gains exactly area x depth; the counters stay open down
// to the face.
TEST(TextGeometry, EmbossAddsAreaTimesDepth)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const Shape body = plate();
    const double v0 = volume(body);
    const TextSpec text = spec("OpenShape", 6);
    const double area = surfaceArea(textFaces(text).value());
    ASSERT_GT(area, 50);
    auto raised = embossText(body, text, onTop(), 1.0);
    ASSERT_TRUE(raised) << raised.developerMessage();
    EXPECT_NEAR(volume(raised.value()) - v0, area * 1.0, 1e-5 * area);
    EXPECT_TRUE(isValid(raised.value()));
    EXPECT_EQ(raised.value().solidCount(), 1) << "the letters are joined to the plate";
    const BoundingBox box = boundingBox(raised.value());
    EXPECT_NEAR(box.max.z, 6.0, 1e-6);
    EXPECT_NEAR(box.min.z, 0.0, 1e-9);
    EXPECT_NEAR(box.size().x, 60, 1e-6) << "the text lies within the plate";

    // The O's counter: empty from above the letters down to the plate.
    auto o = embossText(body, spec("O", 20), onTop(), 2.0);
    ASSERT_TRUE(o);
    const double oArea = surfaceArea(textFaces(spec("O", 20)).value());
    EXPECT_NEAR(volume(o.value()) - v0, oArea * 2.0, 1e-5 * oArea);
    const auto empty = emptyDepth(o.value(), {{0, 0, 7.5}}, {0, 0, -1});
    ASSERT_TRUE(empty.has_value());
    EXPECT_NEAR(*empty, 2.5, 1e-6) << "the counter is open down to the plate";
}

// Cut in: the body loses exactly area x depth; a cut deeper than the plate
// takes out only what is there, and leaves an O's middle as a loose piece.
TEST(TextGeometry, DebossRemovesAreaTimesDepth)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const Shape body = plate();
    const double v0 = volume(body);
    const TextSpec text = spec("ABO 123", 8);
    const double area = surfaceArea(textFaces(text).value());
    auto cut = embossText(body, text, onTop(), -1.0);
    ASSERT_TRUE(cut) << cut.developerMessage();
    EXPECT_NEAR(v0 - volume(cut.value()), area * 1.0, 1e-5 * area);
    EXPECT_TRUE(isValid(cut.value()));
    EXPECT_EQ(cut.value().solidCount(), 1);
    EXPECT_NEAR(boundingBox(cut.value()).max.z, 5.0, 1e-6);
    // An O's counter keeps the plate's full thickness.
    auto o = embossText(body, spec("O", 20), onTop(), -1.5);
    ASSERT_TRUE(o);
    const auto full = materialDepth(o.value(), {0, 0, 5}, {0, 0, -1});
    ASSERT_TRUE(full.has_value());
    EXPECT_NEAR(*full, 5.0, 1e-6) << "the O's middle is not cut";

    // Through a 1 mm plate: only 1 mm is removed and the O's middle comes loose.
    const Shape thin = plate(1);
    auto through = embossText(thin, spec("O", 20), onTop(1), -3);
    ASSERT_TRUE(through) << through.developerMessage();
    EXPECT_NEAR(volume(thin) - volume(through.value()), surfaceArea(textFaces(spec("O", 20)).value()) * 1.0, 1e-4);
    EXPECT_EQ(through.value().solidCount(), 2);
    EXPECT_FALSE(through.warnings().empty()) << "a warning says it is in pieces";
}

// Letters over a hole in the face bridge it at the face's height: nothing
// hangs into the hole (the letters start exactly on the face's plane).
TEST(TextGeometry, RaisedLettersBridgeAHoleExactly)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const Shape holed = booleanOp(plate(), makeCylinder({0, 0, -1}, {0, 0, 1}, 3.0, 7).value(), BooleanKind::Subtract).value();
    const TextSpec text = spec("H", 10); // its crossbar runs over the hole
    const double area = surfaceArea(textFaces(text).value());
    ASSERT_TRUE(faceContains(textFaces(text).value(), 0, {0, 0, 0})) << "the crossbar crosses the middle";
    auto raised = embossText(holed, text, onTop(), 1.0);
    ASSERT_TRUE(raised) << raised.developerMessage();
    EXPECT_NEAR(volume(raised.value()) - volume(holed), area * 1.0, 1e-5 * area);
    EXPECT_TRUE(isValid(raised.value()));
    // Cut in beside a boss standing on the face: the boss is untouched.
    const Shape boss = booleanOp(plate(), makeBox({15, -5, 5}, {10, 10, 10}).value(), BooleanKind::Union).value();
    auto cut = embossText(boss, spec("HI", 6), onTop(5, {-10, 0, 0}), -1.0);
    ASSERT_TRUE(cut) << cut.developerMessage();
    EXPECT_NEAR(volume(boss) - volume(cut.value()), surfaceArea(textFaces(spec("HI", 6)).value()), 1e-4);
    EXPECT_NEAR(boundingBox(cut.value()).max.z, 15.0, 1e-6);
}

// Turned (running along Y) and placed on a side face (normal +X).
TEST(TextGeometry, PlacedOnAnyFlatFaceAndTurned)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const TextSpec text = spec("HI", 4);
    const auto flat = boundingBox(textFaces(text).value());
    // Turned 90 degrees on the top: it runs along Y.
    const TextFrame turned{{0, 0, 5}, {0, 1, 0}, {0, 0, 1}};
    auto faces = placedTextFaces(text, turned);
    ASSERT_TRUE(faces);
    const BoundingBox box = boundingBox(faces.value());
    EXPECT_NEAR(box.size().y, flat.size().x, 1e-6);
    EXPECT_NEAR(box.size().x, 4.0, 1e-6);
    EXPECT_NEAR(box.min.z, 5.0, 1e-9);
    EXPECT_NEAR(box.max.z, 5.0, 1e-9);
    // On the plate's +X side face (x = 30): raised along +X.
    const Shape body = plate();
    const TextFrame side{{30, 0, 2.5}, {0, 1, 0}, {1, 0, 0}};
    auto raised = embossText(body, text, side, 0.5);
    ASSERT_TRUE(raised) << raised.developerMessage();
    const double area = surfaceArea(textFaces(text).value());
    EXPECT_NEAR(volume(raised.value()) - volume(body), area * 0.5, 1e-5 * area);
    EXPECT_NEAR(boundingBox(raised.value()).max.x, 30.5, 1e-6);
}

TEST(TextGeometry, TextThatMissesTheBodyIsRefused)
{
    OS_REQUIRE_TEST_FONT(kFont);
    const Shape body = plate();
    auto miss = embossText(body, spec("Hi"), onTop(5, {200, 0, 0}), -1.0);
    ASSERT_FALSE(miss);
    EXPECT_EQ(miss.error(), ErrorCode::NoEffect);
    EXPECT_EQ(miss.userMessage(), "The text does not reach into the part here.");
    auto zero = embossText(body, spec("Hi"), onTop(), 0.0);
    ASSERT_FALSE(zero);
    EXPECT_EQ(zero.userMessage(), "Type a depth: positive raises the text, negative cuts it in.");
    auto empty = embossText(body, spec(""), onTop(), 1.0);
    ASSERT_FALSE(empty);
    EXPECT_EQ(empty.userMessage(), "Type the text first.");
}
