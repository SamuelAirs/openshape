// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "geometry/Text.h"

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

namespace os::test {

// A font for text tests, registered under `id` (the app's default font id):
// the file named by OPENSHAPE_TEST_FONT, else the app's bundled Noto Sans
// (resources/fonts/) when the source tree has it, else a system font
// (Arial, DejaVu Sans) read at test time. The checks (areas, volumes,
// counters, extents) hold for any outline font. Returns the file used, or
// "" (the test then skips).
inline std::string registerTestFont(const std::string& id)
{
    static std::string used;
    if (!used.empty() && geom::hasFont(id))
        return used;
    std::string candidates[] = {
        std::getenv("OPENSHAPE_TEST_FONT") ? std::getenv("OPENSHAPE_TEST_FONT") : "",
        std::string(OPENSHAPE_SOURCE_DIR) + "/resources/fonts/NotoSans-Regular.ttf",
        "C:/Windows/Fonts/arial.ttf",
        "/System/Library/Fonts/Supplemental/Arial.ttf",
        "/Library/Fonts/Arial.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    };
    for (const std::string& path : candidates) {
        if (path.empty())
            continue;
        std::ifstream in(path, std::ios::binary);
        if (!in)
            continue;
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (geom::registerFont(id, std::move(bytes))) {
            used = path;
            return used;
        }
    }
    return {};
}

} // namespace os::test

// Registers the test font under `id`, or skips the test when there is none.
#define OS_REQUIRE_TEST_FONT(id)                                                                                       \
    do {                                                                                                               \
        if (::os::test::registerTestFont(id).empty())                                                                  \
            GTEST_SKIP() << "no font for text tests (resources/fonts/NotoSans-Regular.ttf, OPENSHAPE_TEST_FONT or a " \
                            "system font)";                                                                            \
    } while (false)
