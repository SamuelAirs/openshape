// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "document/Document.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The .openshape project format: a ZIP container holding
//   document.json          parametric document (source of truth)
//   metadata.json          application/version info
//   geometry/<body>.brep   exact geometry cache (informational; regenerated on load)
//   thumbnail.png          optional preview image
// See docs/FILE_FORMAT.md.
namespace os::io {

inline constexpr int kProjectFormatVersion = 1;
inline constexpr const char* kProjectFormatName = "OpenShape";
inline constexpr const char* kProjectExtension = ".openshape";

// Limits applied when reading untrusted project files.
inline constexpr std::uintmax_t kMaxProjectFileBytes = 1024ull * 1024 * 1024;
inline constexpr std::uint64_t kMaxEntryBytes = 256ull * 1024 * 1024;
inline constexpr std::int64_t kMaxEntries = 10000;

nlohmann::json documentToJson(const doc::Document& document);
Result<std::unique_ptr<doc::Document>> documentFromJson(const nlohmann::json& root);

struct SaveOptions {
    std::vector<unsigned char> thumbnailPng; // empty = no thumbnail
    bool includeGeometryCache = true;
    bool announce = true; // log "saved project" at Info level (recovery copies stay quiet)
};

Status saveProject(const doc::Document& document, const std::filesystem::path& path, const SaveOptions& options = {});

// saveProject in three steps, so the slow parts can run elsewhere:
// serializeProject reads the document (GUI thread; it touches shapes),
// buildProjectArchive and writeFileAtomically only use the resulting strings
// and are safe on a worker thread.
struct ProjectData {
    std::string documentJson;
    std::string metadataJson;
    std::vector<std::pair<std::string, std::string>> geometry; // body id, BRep text
    std::vector<unsigned char> thumbnailPng;
};
ProjectData serializeProject(const doc::Document& document, const SaveOptions& options = {});
Result<std::string> buildProjectArchive(const ProjectData& data);
// Writes <path>.tmp, then renames it over `path`: a reader never sees half a file.
Status writeFileAtomically(const std::filesystem::path& path, const std::string& bytes);
Result<std::unique_ptr<doc::Document>> loadProject(const std::filesystem::path& path);

// Exposed for tests: rejects absolute paths, drive letters, backslashes and
// ".." components in archive entry names.
bool isSafeArchiveEntryName(const std::string& name);

} // namespace os::io
