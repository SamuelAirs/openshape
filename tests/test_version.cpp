// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "core/Version.h"

#include <gtest/gtest.h>

#include <string>

using namespace os;

// The generated header carries CMake's PROJECT_VERSION (passed to this test
// separately as OPENSHAPE_EXPECTED_VERSION), in all its forms.
TEST(Version, MatchesProjectVersion)
{
    EXPECT_STREQ(kAppVersion, OPENSHAPE_EXPECTED_VERSION);
    const std::string joined =
        std::to_string(kAppVersionMajor) + "." + std::to_string(kAppVersionMinor) + "." + std::to_string(kAppVersionPatch);
    EXPECT_EQ(joined, kAppVersion);
    EXPECT_GE(kAppVersionMajor, 0);
    EXPECT_GE(kAppVersionMinor, 0);
    EXPECT_GE(kAppVersionPatch, 0);
}
