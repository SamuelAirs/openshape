// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

// Home's project cards (io/ProjectFiles, io/RecentFiles): the name rules of
// Rename..., the copy names of Duplicate, the file operations themselves in
// a temporary folder (sizes, bytes, what exists afterwards), the recent list
// after a rename, and that every message reads well on touch.

#include "interaction/TouchWording.h"
#include "io/ProjectFiles.h"
#include "io/RecentFiles.h"

#include <gtest/gtest.h>

#include <chrono>
#include <fstream>
#include <iterator>
#include <set>
#include <string>

using namespace os;

namespace {

// A folder of its own under the system's temporary folder, removed afterwards.
struct TempFolder {
    std::filesystem::path path;
    TempFolder()
    {
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() / ("openshape-projectfiles-" + std::to_string(stamp));
        std::filesystem::create_directories(path);
    }
    ~TempFolder()
    {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
    std::filesystem::path write(const std::string& utf8Name, const std::string& bytes) const
    {
        const auto file = path / io::pathFromUtf8(utf8Name);
        std::ofstream out(file, std::ios::binary);
        out << bytes;
        return file;
    }
    std::size_t fileCount() const
    {
        return std::size_t(std::distance(std::filesystem::directory_iterator(path), std::filesystem::directory_iterator()));
    }
};

std::string readAll(const std::filesystem::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// Stands in for a project: the operations copy bytes, whatever they are.
const std::string kProjectBytes = std::string("PK\x03\x04", 4) + std::string(1000, 'x') + "thumbnail.png";

} // namespace

TEST(ProjectFiles, UsableNamesAreTrimmedAndLoseATypedExtension)
{
    EXPECT_EQ(io::checkProjectName("Bracket").base, "Bracket");
    EXPECT_EQ(io::checkProjectName("Bracket").problem, "");
    EXPECT_EQ(io::checkProjectName("  Bracket v2  ").base, "Bracket v2");
    EXPECT_EQ(io::checkProjectName("Bracket.openshape").base, "Bracket");
    EXPECT_EQ(io::checkProjectName("Bracket.OpenShape ").base, "Bracket");
    EXPECT_EQ(io::checkProjectName("Box.v2").base, "Box.v2"); // dots inside are fine
    EXPECT_EQ(io::checkProjectName("Box.v2").problem, "");
    // Letters beyond ASCII (UTF-8), counted as characters, not bytes.
    const std::string umlauts = "Gr\xC3\xBC\xC3\x9F" "e"; // Grüße
    EXPECT_EQ(io::checkProjectName(umlauts).problem, "");
    std::string hundred;
    for (int i = 0; i < 100; ++i)
        hundred += "\xC3\xA9"; // é: 200 bytes, 100 characters
    EXPECT_EQ(io::checkProjectName(hundred).problem, "");
    EXPECT_NE(io::checkProjectName(hundred + "e").problem, "");
}

TEST(ProjectFiles, UnusableNamesAreRefusedWithAReason)
{
    EXPECT_EQ(io::checkProjectName("").problem, "Type a name for the project.");
    EXPECT_EQ(io::checkProjectName("   ").problem, "Type a name for the project.");
    EXPECT_EQ(io::checkProjectName(".openshape").problem, "Type a name for the project.");
    for (const char* bad : {"a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b", "a\tb"})
        EXPECT_EQ(io::checkProjectName(bad).problem, "A name cannot contain / \\ : * ? \" < > |") << bad;
    EXPECT_EQ(io::checkProjectName(".hidden").problem, "A name cannot start with a dot.");
    EXPECT_EQ(io::checkProjectName("..").problem, "A name cannot start with a dot.");
    EXPECT_EQ(io::checkProjectName("Box.").problem, "A name cannot end with a dot.");
    for (const char* device : {"CON", "con", "Nul", "aux", "PRN", "COM1", "lpt9", "NUL.txt", "con .x"})
        EXPECT_NE(io::checkProjectName(device).problem.find("Windows keeps"), std::string::npos) << device;
    for (const char* fine : {"CONSOLE", "COM0", "COM10", "Nullable", "icon"})
        EXPECT_EQ(io::checkProjectName(fine).problem, "") << fine;
    EXPECT_EQ(io::checkProjectName(std::string(101, 'a')).problem, "A name can have at most 100 characters.");
    EXPECT_EQ(io::checkProjectName(std::string(100, 'a')).problem, "");
}

TEST(ProjectFiles, CopiesAreNamedAsFinderNamesThem)
{
    std::set<std::string> taken;
    auto isTaken = [&](const std::string& name) { return taken.count(name) > 0; };
    EXPECT_EQ(io::duplicateProjectName("Box", isTaken), "Box copy");
    taken = {"Box copy"};
    EXPECT_EQ(io::duplicateProjectName("Box", isTaken), "Box copy 2");
    taken = {"Box copy", "Box copy 2"};
    EXPECT_EQ(io::duplicateProjectName("Box", isTaken), "Box copy 3");
    // A copy of a copy is the next copy, not "Box copy copy".
    EXPECT_EQ(io::duplicateProjectName("Box copy", isTaken), "Box copy 3");
    EXPECT_EQ(io::duplicateProjectName("Box copy 2", isTaken), "Box copy 3");
    // "copy" that is part of the name stays.
    taken.clear();
    EXPECT_EQ(io::duplicateProjectName("Photocopy", isTaken), "Photocopy copy");
    EXPECT_EQ(io::duplicateProjectName("Box copy 0", isTaken), "Box copy 0 copy");
    EXPECT_EQ(io::duplicateProjectName("Part 2", isTaken), "Part 2 copy");
    // Long names are shortened to fit, whole characters only.
    std::string longName;
    for (int i = 0; i < 100; ++i)
        longName += "\xC3\xA9";
    const std::string copy = io::duplicateProjectName(longName, isTaken);
    EXPECT_EQ(io::checkProjectName(copy).problem, "") << copy;
    EXPECT_TRUE(copy.ends_with(" copy"));
    EXPECT_EQ(copy.size(), 95u * 2 + 5); // 95 é and " copy": 100 characters
    // Every name taken: none.
    EXPECT_EQ(io::duplicateProjectName("Box", [](const std::string&) { return true; }), "");
}

TEST(ProjectFiles, DuplicateCopiesEveryByteNextToTheOriginal)
{
    TempFolder folder;
    const auto original = folder.write("Bracket.openshape", kProjectBytes);
    const auto first = io::duplicateProjectFile(original);
    ASSERT_TRUE(first) << first.developerMessage();
    EXPECT_EQ(first.value(), folder.path / "Bracket copy.openshape");
    EXPECT_EQ(std::filesystem::file_size(first.value()), kProjectBytes.size());
    EXPECT_EQ(readAll(first.value()), kProjectBytes);
    EXPECT_EQ(readAll(original), kProjectBytes); // the original is untouched
    const auto second = io::duplicateProjectFile(original);
    ASSERT_TRUE(second);
    EXPECT_EQ(second.value(), folder.path / "Bracket copy 2.openshape");
    const auto third = io::duplicateProjectFile(first.value());
    ASSERT_TRUE(third);
    EXPECT_EQ(third.value(), folder.path / "Bracket copy 3.openshape");
    EXPECT_EQ(folder.fileCount(), 4u);
    // A file that is gone: a message, nothing made.
    const auto missing = io::duplicateProjectFile(folder.path / "Gone.openshape");
    EXPECT_FALSE(missing);
    EXPECT_EQ(missing.error(), ErrorCode::FileNotFound);
    EXPECT_EQ(folder.fileCount(), 4u);
}

TEST(ProjectFiles, RenameMovesTheFileAndNeverReplacesAnother)
{
    TempFolder folder;
    const auto file = folder.write("Bracket.openshape", kProjectBytes);
    const auto other = folder.write("Hinge.openshape", "other");

    const auto renamed = io::renameProjectFile(file, "  Wall bracket ");
    ASSERT_TRUE(renamed) << renamed.developerMessage();
    EXPECT_EQ(renamed.value(), folder.path / "Wall bracket.openshape");
    EXPECT_FALSE(std::filesystem::exists(file));
    EXPECT_EQ(readAll(renamed.value()), kProjectBytes);

    // Taken: refused, both files as they were.
    const auto taken = io::renameProjectFile(renamed.value(), "Hinge");
    EXPECT_FALSE(taken);
    EXPECT_EQ(taken.userMessage(), "A project named \xE2\x80\x9CHinge\xE2\x80\x9D is there already.");
    EXPECT_EQ(readAll(other), "other");
    EXPECT_EQ(readAll(renamed.value()), kProjectBytes);

    // Unusable names: refused with the rule's message.
    const auto bad = io::renameProjectFile(renamed.value(), "a/b");
    EXPECT_FALSE(bad);
    EXPECT_EQ(bad.userMessage(), "A name cannot contain / \\ : * ? \" < > |");
    EXPECT_FALSE(io::renameProjectFile(renamed.value(), "  "));
    EXPECT_TRUE(std::filesystem::exists(renamed.value()));

    // The same name: nothing to do.
    const auto same = io::renameProjectFile(renamed.value(), "Wall bracket");
    ASSERT_TRUE(same);
    EXPECT_EQ(same.value(), renamed.value());

    // Letter case only: allowed where file names ignore case (Windows,
    // macOS) and where they do not.
    const auto cased = io::renameProjectFile(renamed.value(), "Wall Bracket");
    ASSERT_TRUE(cased) << cased.userMessage() << " / " << cased.developerMessage();
    EXPECT_EQ(cased.value().filename(), "Wall Bracket.openshape");
    EXPECT_EQ(folder.fileCount(), 2u);
    bool listedAsCased = false;
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
        listedAsCased = listedAsCased || entry.path().filename() == "Wall Bracket.openshape";
    EXPECT_TRUE(listedAsCased);

    // Names beyond ASCII round-trip through the file system.
    const std::string umlauts = "Gr\xC3\xBC\xC3\x9F" "e";
    const auto unicode = io::renameProjectFile(cased.value(), umlauts);
    ASSERT_TRUE(unicode);
    EXPECT_EQ(io::utf8FromPath(unicode.value().filename()), umlauts + ".openshape");
    EXPECT_EQ(std::filesystem::file_size(unicode.value()), kProjectBytes.size());

    EXPECT_EQ(io::renameProjectFile(folder.path / "Gone.openshape", "New").error(), ErrorCode::FileNotFound);
}

TEST(ProjectFiles, RemoveDeletesOnlyProjects)
{
    TempFolder folder;
    const auto project = folder.write("Old.openshape", kProjectBytes);
    const auto notes = folder.write("notes.txt", "keep me");
    EXPECT_TRUE(io::removeProjectFile(project));
    EXPECT_FALSE(std::filesystem::exists(project));
    const auto refused = io::removeProjectFile(notes);
    EXPECT_FALSE(refused);
    EXPECT_TRUE(std::filesystem::exists(notes));
    EXPECT_EQ(io::removeProjectFile(project).error(), ErrorCode::FileNotFound);
    EXPECT_EQ(folder.fileCount(), 1u);
}

TEST(ProjectFiles, RecentListFollowsARename)
{
    const std::vector<std::string> list{"C:/p/a.openshape", "C:/p/b.openshape", "C:/p/c.openshape"};
    EXPECT_EQ(io::withRenamedRecentFile(list, "C:/p/b.openshape", "C:/p/B2.openshape"),
              (std::vector<std::string>{"C:/p/a.openshape", "C:/p/B2.openshape", "C:/p/c.openshape"}));
    // Separators as written either way; not listed: the list as it was.
    EXPECT_EQ(io::withRenamedRecentFile(list, "C:\\p\\a.openshape", "C:/p/z.openshape").front(), "C:/p/z.openshape");
    EXPECT_EQ(io::withRenamedRecentFile(list, "C:/p/x.openshape", "C:/p/y.openshape"), list);
    // Renamed to a name that was listed (a file deleted and its name reused): one entry.
    EXPECT_EQ(io::withRenamedRecentFile(list, "C:/p/c.openshape", "C:/p/a.openshape"),
              (std::vector<std::string>{"C:/p/b.openshape", "C:/p/a.openshape"}));
}

TEST(ProjectFiles, InsideFolderIsBelowItNotBesideIt)
{
    const std::string documents = "/var/mobile/Containers/Data/Application/1F2E/Documents";
    EXPECT_TRUE(io::isInsideFolder(documents + "/Box.openshape", documents));
    EXPECT_TRUE(io::isInsideFolder(documents + "/Parts/Box.openshape", documents));
    EXPECT_TRUE(io::isInsideFolder("/private" + documents + "/Box.openshape", documents));
    EXPECT_FALSE(io::isInsideFolder(documents, documents));
    EXPECT_FALSE(io::isInsideFolder(documents + "2/Box.openshape", documents));
    EXPECT_FALSE(io::isInsideFolder("/var/mobile/Box.openshape", documents));
    EXPECT_FALSE(io::isInsideFolder("C:/Users/me/Box.openshape", ""));
    EXPECT_TRUE(io::isInsideFolder("C:\\Temp\\app\\Box.openshape", "C:/Temp/app"));
}

TEST(ProjectFiles, MessagesReadWellOnTouch)
{
    TempFolder folder;
    const auto file = folder.write("A.openshape", kProjectBytes);
    folder.write("B.openshape", kProjectBytes);
    std::vector<std::string> messages;
    for (const char* name : {"", "a/b", ".x", "x.", "CON", "B"})
        messages.push_back(io::renameProjectFile(file, name).userMessage());
    messages.push_back(io::checkProjectName(std::string(101, 'a')).problem);
    messages.push_back(io::duplicateProjectFile(folder.path / "Gone.openshape").userMessage());
    messages.push_back(io::removeProjectFile(folder.path / "Gone.openshape").userMessage());
    for (const std::string& message : messages) {
        EXPECT_FALSE(message.empty());
        EXPECT_FALSE(interact::mentionsMouseOrKeyboard(message)) << message;
    }
}
