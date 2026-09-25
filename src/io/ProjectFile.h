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
};

Status saveProject(const doc::Document& document, const std::filesystem::path& path, const SaveOptions& options = {});
Result<std::unique_ptr<doc::Document>> loadProject(const std::filesystem::path& path);

// Exposed for tests: rejects absolute paths, drive letters, backslashes and
// ".." components in archive entry names.
bool isSafeArchiveEntryName(const std::string& name);

} // namespace os::io
