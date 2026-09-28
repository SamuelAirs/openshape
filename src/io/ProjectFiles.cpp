// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "io/ProjectFiles.h"

#include "io/ProjectFile.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <system_error>

namespace os::io {

namespace {

constexpr const char* kOpenQuote = "\xE2\x80\x9C";  // “
constexpr const char* kCloseQuote = "\xE2\x80\x9D"; // ”

std::string quoted(const std::string& name)
{
    return std::string(kOpenQuote) + name + kCloseQuote;
}

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string trimmed(const std::string& text)
{
    std::size_t begin = 0, end = text.size();
    while (begin < end && isSpace(text[begin]))
        ++begin;
    while (end > begin && isSpace(text[end - 1]))
        --end;
    return text.substr(begin, end - begin);
}

std::string lower(std::string text)
{
    for (char& c : text)
        if (c >= 'A' && c <= 'Z')
            c = char(c - 'A' + 'a');
    return text;
}

bool endsWithNoCase(const std::string& text, std::string_view suffix)
{
    return text.size() >= suffix.size() && lower(text.substr(text.size() - suffix.size())) == suffix;
}

// Code points in UTF-8 text (continuation bytes are 10xxxxxx).
std::size_t codePoints(const std::string& text)
{
    return std::size_t(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

// The first `count` code points of UTF-8 text.
std::string firstCodePoints(const std::string& text, std::size_t count)
{
    std::size_t seen = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if ((static_cast<unsigned char>(text[i]) & 0xC0) != 0x80) {
            if (seen == count)
                return text.substr(0, i);
            ++seen;
        }
    }
    return text;
}

bool isReservedOnWindows(const std::string& base)
{
    // The part before the first dot, as Windows compares it.
    std::string stem = lower(trimmed(base.substr(0, base.find('.'))));
    static constexpr std::array<std::string_view, 4> devices{"con", "prn", "aux", "nul"};
    if (std::find(devices.begin(), devices.end(), stem) != devices.end())
        return true;
    return stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9';
}

// "Box copy 3" -> "Box"; "Box copy" -> "Box"; anything else unchanged.
std::string withoutCopySuffix(const std::string& base)
{
    std::string stem = base;
    std::size_t end = stem.size();
    while (end > 0 && std::isdigit(static_cast<unsigned char>(stem[end - 1])))
        --end;
    if (end < stem.size()) {
        // " copy 3": digits after " copy ".
        if (end >= 6 && stem.compare(end - 6, 6, " copy ") == 0 && stem[end] != '0')
            return stem.substr(0, end - 6);
        return stem;
    }
    if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, " copy") == 0)
        return stem.substr(0, stem.size() - 5);
    return stem;
}

std::filesystem::path projectPath(const std::filesystem::path& folder, const std::string& base)
{
    return folder / pathFromUtf8(base + kProjectExtension);
}

} // namespace

std::filesystem::path pathFromUtf8(const std::string& path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::string utf8FromPath(const std::filesystem::path& path)
{
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

ProjectName checkProjectName(const std::string& typed)
{
    ProjectName name;
    name.base = trimmed(typed);
    if (endsWithNoCase(name.base, kProjectExtension))
        name.base = trimmed(name.base.substr(0, name.base.size() - std::string_view(kProjectExtension).size()));
    const std::string& base = name.base;
    if (base.empty()) {
        name.problem = "Type a name for the project.";
    } else if (std::any_of(base.begin(), base.end(), [](char c) {
                   return static_cast<unsigned char>(c) < 0x20 || c == 0x7F || std::string_view("/\\:*?\"<>|").find(c) != std::string_view::npos;
               })) {
        name.problem = "A name cannot contain / \\ : * ? \" < > |";
    } else if (base.front() == '.') {
        name.problem = "A name cannot start with a dot.";
    } else if (base.back() == '.') {
        name.problem = "A name cannot end with a dot.";
    } else if (isReservedOnWindows(base)) {
        name.problem = quoted(base) + " is a name Windows keeps for itself. Choose another.";
    } else if (codePoints(base) > kMaxProjectNameLength) {
        name.problem = "A name can have at most " + std::to_string(kMaxProjectNameLength) + " characters.";
    }
    return name;
}

std::string duplicateProjectName(const std::string& base, const std::function<bool(const std::string& name)>& taken)
{
    const std::string stem = withoutCopySuffix(base);
    for (int n = 1; n < 1000; ++n) {
        const std::string suffix = n == 1 ? std::string(" copy") : " copy " + std::to_string(n);
        std::string candidate = stem;
        if (codePoints(candidate) + suffix.size() > kMaxProjectNameLength)
            candidate = trimmed(firstCodePoints(candidate, kMaxProjectNameLength - suffix.size()));
        candidate += suffix;
        if (!taken(candidate))
            return candidate;
    }
    return {};
}

std::string projectRenameProblem(const std::filesystem::path& file, const std::string& typedName)
{
    const ProjectName name = checkProjectName(typedName);
    if (!name.problem.empty())
        return name.problem;
    if (name.base == utf8FromPath(file.stem()))
        return {};
    const std::filesystem::path target = projectPath(file.parent_path(), name.base);
    // Taken, unless it is this file under another letter case ("box" ->
    // "Box" on Windows and macOS, whose file names ignore case).
    std::error_code ec;
    if (std::filesystem::exists(target, ec) && !std::filesystem::equivalent(target, file, ec))
        return "A project named " + quoted(name.base) + " is there already.";
    return {};
}

Result<std::filesystem::path> renameProjectFile(const std::filesystem::path& file, const std::string& typedName)
{
    using R = Result<std::filesystem::path>;
    const std::string oldName = utf8FromPath(file.stem());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return R::failure(ErrorCode::FileNotFound, quoted(oldName) + " is not there any more.",
                          "rename: no file " + utf8FromPath(file));
    if (const std::string problem = projectRenameProblem(file, typedName); !problem.empty())
        return R::failure(ErrorCode::InvalidArgument, problem, "rename " + utf8FromPath(file) + " to '" + typedName + "': " + problem);
    const ProjectName name = checkProjectName(typedName);
    if (name.base == oldName)
        return R::success(file);
    const std::filesystem::path target = projectPath(file.parent_path(), name.base);
    const auto size = std::filesystem::file_size(file, ec);
    std::filesystem::rename(file, target, ec);
    if (ec)
        return R::failure(ErrorCode::FileWriteError,
                          "Could not rename " + quoted(oldName) + ". It may be read-only or in use by another app.",
                          "rename " + utf8FromPath(file) + " -> " + utf8FromPath(target) + ": " + ec.message());
    std::error_code sizeError;
    if (!std::filesystem::is_regular_file(target, sizeError) || std::filesystem::file_size(target, sizeError) != size)
        return R::failure(ErrorCode::FileWriteError, "Could not rename " + quoted(oldName) + ".",
                          "rename: " + utf8FromPath(target) + " is missing or has another size after renaming");
    return R::success(target);
}

Result<std::filesystem::path> duplicateProjectFile(const std::filesystem::path& file)
{
    using R = Result<std::filesystem::path>;
    const std::string name = utf8FromPath(file.stem());
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return R::failure(ErrorCode::FileNotFound, quoted(name) + " is not there any more.",
                          "duplicate: no file " + utf8FromPath(file));
    const std::filesystem::path folder = file.parent_path();
    const std::string copyName = duplicateProjectName(name, [&](const std::string& candidate) {
        std::error_code existsError;
        return std::filesystem::exists(projectPath(folder, candidate), existsError) || bool(existsError);
    });
    if (copyName.empty())
        return R::failure(ErrorCode::FileWriteError, "Could not find a free name for a copy of " + quoted(name) + ".",
                          "duplicate: every copy name is taken for " + utf8FromPath(file));
    const std::filesystem::path target = projectPath(folder, copyName);
    const auto size = std::filesystem::file_size(file, ec);
    // copy_options::none: fails rather than replace a file that appeared meanwhile.
    if (ec || !std::filesystem::copy_file(file, target, std::filesystem::copy_options::none, ec) || ec) {
        return R::failure(ErrorCode::FileWriteError,
                          "Could not duplicate " + quoted(name) + ". The device may be full.",
                          "duplicate " + utf8FromPath(file) + " -> " + utf8FromPath(target) + ": " + ec.message());
    }
    std::error_code sizeError;
    if (std::filesystem::file_size(target, sizeError) != size || sizeError) {
        std::filesystem::remove(target, sizeError);
        return R::failure(ErrorCode::FileWriteError, "Could not duplicate " + quoted(name) + ". The device may be full.",
                          "duplicate: " + utf8FromPath(target) + " has another size than the original");
    }
    return R::success(target);
}

Status removeProjectFile(const std::filesystem::path& file)
{
    const std::string name = utf8FromPath(file.stem());
    if (lower(utf8FromPath(file.extension())) != kProjectExtension)
        return Status::failure(ErrorCode::InvalidArgument, "Only projects can be deleted here.",
                               "remove: not a project file: " + utf8FromPath(file));
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec))
        return Status::failure(ErrorCode::FileNotFound, quoted(name) + " is not there any more.",
                               "remove: no file " + utf8FromPath(file));
    std::filesystem::remove(file, ec);
    std::error_code existsError;
    if (ec || std::filesystem::exists(file, existsError))
        return Status::failure(ErrorCode::FileWriteError,
                               "Could not delete " + quoted(name) + ". It may be read-only or in use by another app.",
                               "remove " + utf8FromPath(file) + ": " + ec.message());
    return okStatus();
}

} // namespace os::io
