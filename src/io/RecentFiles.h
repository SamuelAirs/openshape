// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

// File → Open Recent: the list logic (the app stores the list in its settings).
// Paths are UTF-8, absolute, most recent first.
namespace os::io {

inline constexpr std::size_t kMaxRecentFiles = 10;

// Whether two paths name the same file as far as a list of recent files is
// concerned: '/' and '\' are the same, and on Windows so are letter cases.
bool sameRecentPath(const std::string& a, const std::string& b);
using RecentFileExists = std::function<bool(const std::string& path)>;
// Whether a list entry is an existing file.
bool recentFileExists(const std::string& path);
// `path` moved (or added) to the front, without duplicates. Only entries that
// exist count toward `limit`, so files that are gone never push existing ones
// out of the menu: an existing entry is dropped only once `limit` newer
// existing ones come before it. Missing entries (maybe only for now: a USB
// stick, a network drive) keep their place, at most `limit` of them.
std::vector<std::string> withRecentFile(std::vector<std::string> list, const std::string& path,
                                        std::size_t limit = kMaxRecentFiles,
                                        const RecentFileExists& exists = recentFileExists);
// Only the entries that still exist as files (a project moved or deleted drops out).
std::vector<std::string> existingRecentFiles(const std::vector<std::string>& list);
// The list without `path` (Home: "Remove from list"; the file itself stays).
std::vector<std::string> withoutRecentFile(std::vector<std::string> list, const std::string& path);
// What the start screen lists: the recent files, then the projects of a
// folder the app owns (the iPad's Documents: projects put there with the
// Files app) that are not among them, in the order given.
std::vector<std::string> homeProjects(const std::vector<std::string>& recent, const std::vector<std::string>& folder);

// iPhone / iPad: the app's data folder moves when the app is updated (every
// TestFlight build): `.../Application/<id>/Documents` gets a new <id>, so a
// path remembered from before (Open Recent, a recovery copy's project) is
// stale. If `path` is missing and lies inside a folder that is `folder` but
// for one path component (that id; a leading "/private" is ignored), the
// same file inside `folder` when that exists; otherwise `path` unchanged.
std::string rebasedIntoFolder(const std::string& path, const std::string& folder,
                              const RecentFileExists& exists = recentFileExists);

// Where a Home card says its project is: `directory` (the project's folder)
// as the user knows it. Inside the app's own folder `appFolder` (iPhone /
// iPad: Documents, which the Files app shows by the app's name) that is
// "OpenShape (Files app)", or "OpenShape → Sub → Folder (Files app)" below
// it, not the sandbox path (/var/mobile/Containers/Data/Application/<id>/
// Documents); any other directory, or no app folder, is `directory` itself.
std::string homeFolderLabel(const std::string& directory, const std::string& appFolder);

} // namespace os::io
