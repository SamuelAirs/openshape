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

namespace detail {
inline bool registerFontFile(const std::string& id, const std::string& path)
{
    if (path.empty())
        return false;
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return geom::registerFont(id, std::move(bytes));
}
} // namespace detail

// A font for text tests, registered under `id` (the app's default font id):
// the app's bundled Noto Sans (resources/fonts/), or the file named by
// OPENSHAPE_TEST_FONT to try another typeface (the invariants - areas,
// volumes, counters, extents - hold for any outline font). Returns the file
// used, or "" (the test then fails: the fonts are part of the repository).
inline std::string registerTestFont(const std::string& id)
{
    static std::string used;
    if (!used.empty() && geom::hasFont(id))
        return used;
    const char* chosen = std::getenv("OPENSHAPE_TEST_FONT");
    for (const std::string& path : {std::string(chosen ? chosen : ""),
                                    std::string(OPENSHAPE_SOURCE_DIR) + "/resources/fonts/NotoSans-Regular.ttf"})
        if (detail::registerFontFile(id, path)) {
            used = path;
            return used;
        }
    return {};
}

// The bundled Noto Sans Bold for the Bold switch, registered under `id`.
// Returns the file used, or "".
inline std::string registerTestBoldFont(const std::string& id)
{
    static std::string used;
    if (!used.empty() && geom::hasFont(id))
        return used;
    const std::string path = std::string(OPENSHAPE_SOURCE_DIR) + "/resources/fonts/NotoSans-Bold.ttf";
    if (detail::registerFontFile(id, path))
        used = path;
    return used;
}

} // namespace os::test

// Registers the test font under `id`; fails the test when it cannot be read.
#define OS_REQUIRE_TEST_FONT(id)                                                                                       \
    do {                                                                                                               \
        if (::os::test::registerTestFont(id).empty())                                                                  \
            GTEST_FAIL() << "no font for text tests: resources/fonts/NotoSans-Regular.ttf (or OPENSHAPE_TEST_FONT) "   \
                            "cannot be read";                                                                          \
    } while (false)
