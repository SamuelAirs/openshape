// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// The start screen's list logic (io/RecentFiles): removing an entry, and
// merging the recent files with the projects of the app's own folder.

#include "io/RecentFiles.h"

#include <gtest/gtest.h>

using namespace os;

TEST(HomeList, RemoveFromListKeepsTheRest)
{
    const std::vector<std::string> list{"C:/p/a.openshape", "C:/p/b.openshape", "C:/p/c.openshape"};
    EXPECT_EQ(io::withoutRecentFile(list, "C:/p/b.openshape"),
              (std::vector<std::string>{"C:/p/a.openshape", "C:/p/c.openshape"}));
    EXPECT_EQ(io::withoutRecentFile(list, "C:/p/missing.openshape"), list);
    EXPECT_TRUE(io::withoutRecentFile({}, "C:/p/a.openshape").empty());
    // The same file however its separators are written.
    EXPECT_EQ(io::withoutRecentFile(list, "C:\\p\\c.openshape"),
              (std::vector<std::string>{"C:/p/a.openshape", "C:/p/b.openshape"}));
}

TEST(HomeList, RecentFirstThenTheFolderWithoutDuplicates)
{
    const std::vector<std::string> recent{"/docs/b.openshape", "/elsewhere/x.openshape"};
    const std::vector<std::string> folder{"/docs/a.openshape", "/docs/b.openshape", "/docs/c.openshape"};
    EXPECT_EQ(io::homeProjects(recent, folder), (std::vector<std::string>{"/docs/b.openshape", "/elsewhere/x.openshape",
                                                                         "/docs/a.openshape", "/docs/c.openshape"}));
    EXPECT_EQ(io::homeProjects({}, folder), folder);
    EXPECT_EQ(io::homeProjects(recent, {}), recent);
}
