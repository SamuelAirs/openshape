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

} // namespace os::io
