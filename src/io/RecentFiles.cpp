// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "io/RecentFiles.h"

#include <algorithm>
#include <filesystem>

namespace os::io {

namespace {

std::string normalized(const std::string& path)
{
    std::string out = path;
    for (char& c : out) {
        if (c == '\\')
            c = '/';
#if defined(_WIN32)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
#endif
    }
    return out;
}

} // namespace

bool sameRecentPath(const std::string& a, const std::string& b)
{
    return normalized(a) == normalized(b);
}

bool recentFileExists(const std::string& path)
{
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(std::filesystem::path(std::u8string(path.begin(), path.end())), ec);
}

std::vector<std::string> withRecentFile(std::vector<std::string> list, const std::string& path, std::size_t limit,
                                        const RecentFileExists& exists)
{
    if (path.empty() || limit == 0)
        return list;
    std::vector<std::string> out{path};
    std::size_t existing = 0, missing = 0;
    (exists(path) ? existing : missing) += 1;
    for (std::string& entry : list) {
        if (existing >= limit)
            break; // the menu is full: everything older drops out
        if (entry.empty() || sameRecentPath(entry, path))
            continue;
        if (exists(entry)) {
            out.push_back(std::move(entry));
            ++existing;
        } else if (missing < limit) {
            out.push_back(std::move(entry));
            ++missing;
        }
    }
    return out;
}

std::vector<std::string> withoutRecentFile(std::vector<std::string> list, const std::string& path)
{
    std::erase_if(list, [&](const std::string& entry) { return sameRecentPath(entry, path); });
    return list;
}

std::vector<std::string> homeProjects(const std::vector<std::string>& recent, const std::vector<std::string>& folder)
{
    std::vector<std::string> out = recent;
    for (const std::string& file : folder)
        if (std::none_of(out.begin(), out.end(), [&](const std::string& known) { return sameRecentPath(known, file); }))
            out.push_back(file);
    return out;
}

std::vector<std::string> existingRecentFiles(const std::vector<std::string>& list)
{
    std::vector<std::string> out;
    for (const std::string& entry : list)
        if (recentFileExists(entry))
            out.push_back(entry);
    return out;
}

} // namespace os::io
