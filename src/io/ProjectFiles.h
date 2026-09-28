// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>

// Home's project cards: renaming, duplicating and deleting project files
// (Rename…, Duplicate, Delete… in a card's menu). Names are UTF-8; a
// project's name is its file's name without ".openshape". Qt-free, so the
// rules are unit-tested (tests/test_project_files.cpp); moving a file to
// the system's trash is the app's (AppController, Qt's QFile::moveToTrash).
namespace os::io {

// At most this many characters (Unicode code points) in a project's name.
inline constexpr std::size_t kMaxProjectNameLength = 100;

// A typed project name as a file's base name: surrounding spaces and a
// typed ".openshape" dropped. `problem` says in plain words why it cannot
// be used ("" when it can): empty; characters no file system takes
// (/ \ : * ? " < > | and control characters); a leading dot (hidden
// file) or a trailing one (Windows drops it); a name Windows keeps for
// devices (CON, NUL, COM1, ... also with an extension); too long. Other
// systems take some of these, but a project's file is shared with them.
struct ProjectName {
    std::string base;
    std::string problem;
};
ProjectName checkProjectName(const std::string& typed);

// The name for a duplicate of project `base`, as Finder and the Files app
// name copies: "Box copy", then "Box copy 2", "Box copy 3", ... the first
// one `taken` says is free. A copy of "Box copy" or "Box copy 2" is again
// "Box copy 3" and so on (not "Box copy copy"). Shortened to fit
// kMaxProjectNameLength. "" when every candidate up to "copy 999" is taken.
std::string duplicateProjectName(const std::string& base, const std::function<bool(const std::string& name)>& taken);

// UTF-8 strings and paths (std::filesystem takes the system code page on
// Windows for a plain std::string).
std::filesystem::path pathFromUtf8(const std::string& path);
std::string utf8FromPath(const std::filesystem::path& path);

// Why project file `file` cannot be renamed to `typedName` ("" when it can,
// and for its current name): checkProjectName's problem, or "A project
// named “X” is there already." for another file of that name in its folder
// (the same file under another letter case is not another file).
std::string projectRenameProblem(const std::filesystem::path& file, const std::string& typedName);

// Renames project file `file` to `typedName` (checkProjectName) in the same
// folder, keeping ".openshape". Refuses, with a plain message, an unusable
// name or one another file in that folder has (a change of letter case
// only is allowed). Nothing is replaced. The new path on success; a name
// equal to the current one succeeds without touching the file.
Result<std::filesystem::path> renameProjectFile(const std::filesystem::path& file, const std::string& typedName);

// Copies project file `file` next to itself as duplicateProjectName (the
// preview inside the file comes along). Verified by size; nothing is
// replaced. The copy's path on success.
Result<std::filesystem::path> duplicateProjectFile(const std::filesystem::path& file);

// Removes project file `file` for good (where it cannot go to a trash; see
// AppController::deleteProject). Only ".openshape" files.
Status removeProjectFile(const std::filesystem::path& file);

} // namespace os::io
