// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "document/Document.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// Recovery copies: while a document has unsaved changes, the app keeps a copy
// of it in a recovery folder in its own data folder (never beside the user's
// file, which changes only on Save), so the work survives a crash.
//
// Every running app instance is one *session* (a UUID) and owns these files:
//   <session>.openshape   the document: a normal project file without the
//                         geometry cache (it is rebuilt on load anyway)
//   <session>.json        sidecar: the user's file, title, time, app version
//   <session>.lock        written by the app (QLockFile): the owner is alive
// This code is Qt-free: whether a session is alive is the caller's question.
namespace os::io {

inline constexpr const char* kRecoverySidecarFormat = "OpenShapeRecovery";
inline constexpr int kRecoverySidecarVersion = 1;

struct RecoveryInfo {
    std::string originalPath;   // UTF-8; the user's file, empty if never saved
    std::string title;          // as shown in the window title
    std::int64_t savedAtMs = 0; // Unix time in milliseconds; 0 = unknown
    std::string appVersion;
};

struct RecoveryEntry {
    std::string session;
    std::filesystem::path projectFile;
    RecoveryInfo info;
    bool hasSidecar = false; // false: a crash between the two writes; shown as "Untitled"
};

std::string recoverySidecarJson(const RecoveryInfo& info);
// The sidecar is read from disk, so it is untrusted: strict types, size limits.
Result<RecoveryInfo> parseRecoverySidecar(const std::string& text);
// Session names are lowercase UUIDs (8-4-4-4-12); anything else in the folder is ignored.
bool isRecoverySessionName(std::string_view name);
std::int64_t unixTimeMs();

class RecoveryStore {
public:
    using IsAlive = std::function<bool(const std::string& session)>;

    explicit RecoveryStore(std::filesystem::path directory) : dir_(std::move(directory)) {}

    const std::filesystem::path& directory() const { return dir_; }
    std::filesystem::path projectFile(const std::string& session) const;
    std::filesystem::path sidecarFile(const std::string& session) const;
    std::filesystem::path lockFile(const std::string& session) const;

    // Writes the session's copy (creating the folder), then its sidecar, each
    // atomically. info.savedAtMs == 0 means "now". A failure says it is only
    // the recovery copy (the user's file is not affected).
    Status write(const std::string& session, const doc::Document& document, RecoveryInfo info) const;
    bool exists(const std::string& session) const;
    // Every copy in the folder (with or without a readable sidecar), newest first.
    std::vector<RecoveryEntry> list() const;
    // Copies whose session is not alive and not `ownSession`: work left by a
    // crash (or a killed app), to offer for restoring.
    std::vector<RecoveryEntry> orphaned(const IsAlive& isAlive, const std::string& ownSession) const;
    // Removes a session's copy and sidecar (not its lock). Missing files are fine.
    Status remove(const std::string& session) const;
    // Moves session `from`'s copy to session `to` (replacing `to`'s) and
    // writes `to`'s sidecar from `info` (what was read from `from`'s): a
    // restored document keeps its copy, and its original file, under the new
    // owner even if `from`'s sidecar cannot be moved (a virus scanner holding
    // it); that one is removed now or later as a leftover. Fails if the new
    // sidecar cannot be written (the caller then writes a fresh copy).
    Status adopt(const std::string& from, const std::string& to, const RecoveryInfo& info) const;
    // Deletes half-written temporary files and sidecars without a copy that
    // belong to sessions that are not alive. Returns how many files it removed.
    int removeLeftovers(const IsAlive& isAlive, const std::string& ownSession) const;

private:
    std::filesystem::path dir_;
};

} // namespace os::io
