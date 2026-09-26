// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "io/Recovery.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "io/ProjectFile.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>

namespace os::io {

using nlohmann::json;

namespace {

constexpr std::uintmax_t kMaxSidecarBytes = 64 * 1024;
constexpr std::size_t kMaxPathChars = 4096;
constexpr std::size_t kMaxTitleChars = 512;
constexpr std::size_t kMaxVersionChars = 64;
constexpr const char* kProjectSuffix = ".openshape";
constexpr const char* kSidecarSuffix = ".json";
constexpr const char* kTempSuffix = ".tmp";

std::string pathString(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string fileName(const std::filesystem::path& path)
{
    const auto u8 = path.filename().u8string();
    return std::string(u8.begin(), u8.end());
}

Result<std::string> readSmallFile(const std::filesystem::path& path, std::uintmax_t limit)
{
    using R = Result<std::string>;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec)
        return R::failure(ErrorCode::FileNotFound, "The recovery information is missing.", "cannot stat " + pathString(path));
    if (size > limit)
        return R::failure(ErrorCode::FileFormatError, "The recovery information is damaged.", "too large: " + pathString(path));
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return R::failure(ErrorCode::FileReadError, "The recovery information could not be read.", "cannot open " + pathString(path));
    std::ostringstream text;
    text << in.rdbuf();
    return R::success(text.str());
}

void removeQuietly(const std::filesystem::path& path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Shown a few seconds after an ordinary edit, with no Save pressed: it must
// not read like the user's Save failed.
constexpr const char* kRecoveryWriteFailed =
    "Unable to keep a recovery copy of your unsaved work: the app's data folder is full or not writable. "
    "Your file is not affected; save to keep your work.";

Status recoveryWriteFailure(const Status& cause)
{
    return Status::failure(cause.error(), kRecoveryWriteFailed,
                           "recovery copy: " + cause.developerMessage() + " (" + cause.userMessage() + ")");
}

} // namespace

std::int64_t unixTimeMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool isRecoverySessionName(std::string_view name)
{
    if (name.size() != 36)
        return false;
    for (std::size_t i = 0; i < name.size(); ++i) {
        const char c = name[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-')
                return false;
        } else if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    return true;
}

std::string recoverySidecarJson(const RecoveryInfo& info)
{
    return json{{"format", kRecoverySidecarFormat},
                {"version", kRecoverySidecarVersion},
                {"originalPath", info.originalPath},
                {"title", info.title},
                {"savedAt", info.savedAtMs},
                {"appVersion", info.appVersion}}
        .dump(2);
}

Result<RecoveryInfo> parseRecoverySidecar(const std::string& text)
{
    using R = Result<RecoveryInfo>;
    auto bad = [](const std::string& dev) {
        return R::failure(ErrorCode::FileFormatError, "The recovery information is damaged.", dev);
    };
    if (text.size() > kMaxSidecarBytes)
        return bad("sidecar too large");
    const json root = json::parse(text, nullptr, false);
    if (root.is_discarded() || !root.is_object())
        return bad("sidecar is not a JSON object");
    if (!root.contains("format") || root["format"] != kRecoverySidecarFormat)
        return bad("missing or wrong 'format'");
    // Newer versions may add fields; the ones read here keep their meaning.
    if (!root.contains("version") || !root["version"].is_number_integer() || root["version"].get<int>() < 1)
        return bad("missing or invalid 'version'");
    auto readText = [&](const char* key, std::size_t limit, std::string& out) {
        if (!root.contains(key))
            return true; // optional
        if (!root[key].is_string())
            return false;
        out = root[key].get<std::string>();
        return out.size() <= limit && out.find('\0') == std::string::npos;
    };
    RecoveryInfo info;
    if (!readText("originalPath", kMaxPathChars, info.originalPath))
        return bad("invalid 'originalPath'");
    if (!readText("title", kMaxTitleChars, info.title))
        return bad("invalid 'title'");
    if (!readText("appVersion", kMaxVersionChars, info.appVersion))
        return bad("invalid 'appVersion'");
    if (root.contains("savedAt")) {
        if (!root["savedAt"].is_number_integer())
            return bad("invalid 'savedAt'");
        info.savedAtMs = std::max<std::int64_t>(0, root["savedAt"].get<std::int64_t>());
    }
    return R::success(std::move(info));
}

// ---- Store --------------------------------------------------------------------

std::filesystem::path RecoveryStore::projectFile(const std::string& session) const
{
    return dir_ / (session + kProjectSuffix);
}

std::filesystem::path RecoveryStore::sidecarFile(const std::string& session) const
{
    return dir_ / (session + kSidecarSuffix);
}

std::filesystem::path RecoveryStore::lockFile(const std::string& session) const
{
    return dir_ / (session + ".lock");
}

Status RecoveryStore::write(const std::string& session, const doc::Document& document, RecoveryInfo info) const
{
    if (!isRecoverySessionName(session))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to keep a recovery copy.", "bad session name " + session);
    ScopedTimer timer("recovery copy");
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    if (ec)
        return Status::failure(ErrorCode::FileWriteError, kRecoveryWriteFailed,
                               "create_directories " + pathString(dir_) + ": " + ec.message());
    SaveOptions options;
    options.includeGeometryCache = false; // rebuilt on load; keeps copies small and quick
    options.announce = false;
    if (Status s = saveProject(document, projectFile(session), options); !s)
        return recoveryWriteFailure(s);
    if (info.savedAtMs == 0)
        info.savedAtMs = unixTimeMs();
    if (Status s = writeFileAtomically(sidecarFile(session), recoverySidecarJson(info)); !s)
        return recoveryWriteFailure(s);
    return okStatus();
}

bool RecoveryStore::exists(const std::string& session) const
{
    std::error_code ec;
    return std::filesystem::is_regular_file(projectFile(session), ec);
}

std::vector<RecoveryEntry> RecoveryStore::list() const
{
    std::vector<RecoveryEntry> entries;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir_, ec);
    if (ec)
        return entries; // no folder yet: nothing to recover
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        const std::string name = fileName(it->path());
        if (!endsWith(name, kProjectSuffix))
            continue;
        const std::string session = name.substr(0, name.size() - std::string_view(kProjectSuffix).size());
        std::error_code typeError;
        if (!isRecoverySessionName(session) || !it->is_regular_file(typeError))
            continue;
        RecoveryEntry entry;
        entry.session = session;
        entry.projectFile = it->path();
        if (auto text = readSmallFile(sidecarFile(session), kMaxSidecarBytes)) {
            if (auto info = parseRecoverySidecar(text.value())) {
                entry.info = std::move(info.value());
                entry.hasSidecar = true;
            } else {
                OS_LOG(Warning, File) << "recovery sidecar of " << session << ": " << info.developerMessage();
            }
        }
        entries.push_back(std::move(entry));
    }
    std::sort(entries.begin(), entries.end(), [](const RecoveryEntry& a, const RecoveryEntry& b) {
        return a.info.savedAtMs != b.info.savedAtMs ? a.info.savedAtMs > b.info.savedAtMs : a.session < b.session;
    });
    return entries;
}

std::vector<RecoveryEntry> RecoveryStore::orphaned(const IsAlive& isAlive, const std::string& ownSession) const
{
    std::vector<RecoveryEntry> entries = list();
    std::erase_if(entries, [&](const RecoveryEntry& e) { return e.session == ownSession || isAlive(e.session); });
    return entries;
}

Status RecoveryStore::remove(const std::string& session) const
{
    if (!isRecoverySessionName(session))
        return Status::failure(ErrorCode::InvalidArgument, "Unable to remove the recovery copy.", "bad session name " + session);
    Status result = okStatus();
    for (const auto& path : {projectFile(session), sidecarFile(session)}) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
        if (ec)
            result = Status::failure(ErrorCode::FileWriteError, "Unable to remove the recovery copy.",
                                     "remove " + pathString(path) + ": " + ec.message());
        auto tmp = path;
        tmp += kTempSuffix;
        removeQuietly(tmp);
    }
    return result;
}

Status RecoveryStore::adopt(const std::string& from, const std::string& to, const RecoveryInfo& info) const
{
    if (!isRecoverySessionName(from) || !isRecoverySessionName(to) || from == to)
        return Status::failure(ErrorCode::InvalidArgument, "Unable to keep the recovery copy.", "bad sessions " + from + " -> " + to);
    if (!exists(from))
        return Status::failure(ErrorCode::FileNotFound, "The recovery copy is gone.", "no copy for " + from);
    (void)remove(to);
    std::error_code ec;
    std::filesystem::rename(projectFile(from), projectFile(to), ec);
    if (ec)
        return Status::failure(ErrorCode::FileWriteError, "Unable to keep the recovery copy.",
                               "rename " + pathString(projectFile(from)) + ": " + ec.message());
    // The new sidecar is written, not moved: a rename can fail while another
    // program holds the file, and a copy without its sidecar would lose the
    // user's file path at the next crash.
    if (Status s = writeFileAtomically(sidecarFile(to), recoverySidecarJson(info)); !s)
        return Status::failure(ErrorCode::FileWriteError, "Unable to keep the recovery copy.",
                               "sidecar of " + to + ": " + s.developerMessage());
    std::error_code sidecarError;
    std::filesystem::remove(sidecarFile(from), sidecarError);
    if (sidecarError) // a leftover: removeLeftovers deletes it once it is free
        OS_LOG(Warning, File) << "old recovery sidecar not removed yet: " << sidecarError.message();
    return okStatus();
}

int RecoveryStore::removeLeftovers(const IsAlive& isAlive, const std::string& ownSession) const
{
    std::vector<std::filesystem::path> doomed;
    std::error_code ec;
    std::filesystem::directory_iterator it(dir_, ec);
    if (ec)
        return 0;
    for (; it != std::filesystem::directory_iterator(); it.increment(ec)) {
        if (ec)
            break;
        std::string name = fileName(it->path());
        const bool temp = endsWith(name, kTempSuffix);
        if (temp)
            name.resize(name.size() - std::string_view(kTempSuffix).size());
        std::string session;
        bool sidecar = false;
        if (endsWith(name, kProjectSuffix)) {
            session = name.substr(0, name.size() - std::string_view(kProjectSuffix).size());
        } else if (endsWith(name, kSidecarSuffix)) {
            session = name.substr(0, name.size() - std::string_view(kSidecarSuffix).size());
            sidecar = true;
        } else {
            continue;
        }
        if (!isRecoverySessionName(session) || session == ownSession)
            continue;
        const bool orphanSidecar = sidecar && !temp && !exists(session);
        if ((temp || orphanSidecar) && !isAlive(session))
            doomed.push_back(it->path());
    }
    int removed = 0;
    for (const auto& path : doomed) {
        std::error_code removeError;
        if (std::filesystem::remove(path, removeError))
            ++removed;
    }
    return removed;
}

} // namespace os::io
