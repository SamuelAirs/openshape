// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Project thumbnails: the CPU rendering of the model saved with projects.

#include "commands/Command.h"
#include "document/Document.h"
#include "interaction/InteractionController.h"
#include "interaction/Thumbnail.h"

#include <gtest/gtest.h>

#include <chrono>

using namespace os;
using namespace os::interact;

namespace {

struct Harness {
    doc::Document document;
    cmd::UndoStack stack;
    InteractionController controller{document, stack};
    Harness() { controller.setViewportSize({1200, 800}); }
};

struct Coverage {
    int opaque = 0, transparent = 0, dark = 0;
    int minX = 1 << 30, maxX = -1, minY = 1 << 30, maxY = -1;
};

Coverage coverage(const ThumbnailImage& image)
{
    Coverage c;
    for (int y = 0; y < image.height; ++y)
        for (int x = 0; x < image.width; ++x) {
            const std::uint8_t* p = &image.rgba[(std::size_t(y) * image.width + x) * 4];
            if (p[3] == 0) {
                ++c.transparent;
                continue;
            }
            if (p[3] == 255) {
                ++c.opaque;
                if (p[0] < 90 && p[1] < 90 && p[2] < 90)
                    ++c.dark; // an edge
            }
            c.minX = std::min(c.minX, x);
            c.maxX = std::max(c.maxX, x);
            c.minY = std::min(c.minY, y);
            c.maxY = std::max(c.maxY, y);
        }
    return c;
}

const std::uint8_t* pixel(const ThumbnailImage& image, int x, int y)
{
    return &image.rgba[(std::size_t(y) * image.width + x) * 4];
}

} // namespace

TEST(Thumbnail, NothingToDrawGivesNoImage)
{
    Harness h;
    EXPECT_TRUE(h.controller.renderThumbnail(256).empty());
    EXPECT_TRUE(renderThumbnail({}, 256).empty());
    ASSERT_TRUE(h.controller.createBox(20).ok());
    EXPECT_TRUE(h.controller.renderThumbnail(0).empty());
}

TEST(Thumbnail, ABoxIsFramedShadedAndOutlined)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    const ThumbnailImage image = h.controller.renderThumbnail(256);
    ASSERT_EQ(image.width, 256);
    ASSERT_EQ(image.height, 256);
    ASSERT_EQ(image.rgba.size(), 256u * 256u * 4u);
    const Coverage c = coverage(image);
    // Framed: the model fills the image, with a margin of about 6 %.
    const int w = c.maxX - c.minX + 1, hgt = c.maxY - c.minY + 1;
    EXPECT_GE(std::max(w, hgt), 215) << w << " x " << hgt;
    EXPECT_LE(std::max(w, hgt), 235) << w << " x " << hgt;
    EXPECT_NEAR((c.minX + c.maxX) / 2.0, 127.5, 3.0);
    EXPECT_NEAR((c.minY + c.maxY) / 2.0, 127.5, 3.0);
    EXPECT_GT(c.opaque, 256 * 256 / 3) << "a cube seen from the corner covers much of the square";
    EXPECT_GT(c.dark, 300) << "its edges are drawn";
    // Transparent corners; shaded faces (the top brighter than the sides).
    EXPECT_EQ(pixel(image, 2, 2)[3], 0);
    EXPECT_EQ(pixel(image, 253, 253)[3], 0);
    const std::uint8_t* top = pixel(image, 128, 70);   // the top face, above the center corner
    const std::uint8_t* left = pixel(image, 90, 150);  // the front face
    const std::uint8_t* right = pixel(image, 166, 150); // the right face
    EXPECT_EQ(top[3], 255);
    EXPECT_EQ(left[3], 255);
    EXPECT_EQ(right[3], 255);
    EXPECT_GT(top[0], 120);
    EXPECT_NE(left[0], right[0]) << "the two side faces are lit differently";
}

TEST(Thumbnail, OnlyVisibleBodiesAndTheWholeModel)
{
    Harness h;
    ASSERT_TRUE(h.controller.createBox(20).ok());
    ASSERT_TRUE(h.controller.createBox(20).ok()); // placed beside the first
    const Coverage both = coverage(h.controller.renderThumbnail(128));
    ASSERT_TRUE(h.controller.setBodyVisible(h.document.bodies()[1]->id(), false).ok());
    const Coverage one = coverage(h.controller.renderThumbnail(128));
    // One cube alone is framed larger than one of two cubes side by side.
    EXPECT_GT(one.opaque, both.opaque / 2 + 500);
    // And the view itself is not changed by rendering the thumbnail.
    const Camera before = h.controller.camera();
    (void)h.controller.renderThumbnail(64);
    EXPECT_EQ(h.controller.camera().yaw, before.yaw);
    EXPECT_EQ(h.controller.camera().orthoHeight, before.orthoHeight);
}

TEST(Thumbnail, IsQuickForAManyBodyModel)
{
    Harness h;
    for (int i = 0; i < 30; ++i)
        ASSERT_TRUE(h.controller.createBox(10).ok());
    (void)h.controller.renderThumbnail(256); // tessellation happens once, for the view anyway
    const auto start = std::chrono::steady_clock::now();
    const ThumbnailImage image = h.controller.renderThumbnail(256);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    EXPECT_FALSE(image.empty());
    EXPECT_LT(ms, 250.0) << "a save must not wait long for its thumbnail";
    std::printf("thumbnail of 30 bodies: %.1f ms\n", ms);
}
