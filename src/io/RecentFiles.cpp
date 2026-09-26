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

std::vector<std::string> withRecentFile(std::vector<std::string> list, const std::string& path, std::size_t limit)
{
    if (path.empty())
        return list;
    std::erase_if(list, [&](const std::string& entry) { return entry.empty() || sameRecentPath(entry, path); });
    list.insert(list.begin(), path);
    if (list.size() > limit)
        list.resize(limit);
    return list;
}

std::vector<std::string> existingRecentFiles(const std::vector<std::string>& list)
{
    std::vector<std::string> out;
    for (const std::string& entry : list) {
        std::error_code ec;
        const std::filesystem::path path(std::u8string(entry.begin(), entry.end()));
        if (!entry.empty() && std::filesystem::is_regular_file(path, ec))
            out.push_back(entry);
    }
    return out;
}

} // namespace os::io
