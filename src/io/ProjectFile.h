// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#pragma once

#include "core/Result.h"
#include "document/Document.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// The .openshape project format: a ZIP container holding
//   document.json          parametric document (source of truth)
//   metadata.json          application/version info
//   imports/<step>.brep    exact geometry of imported bodies (source of truth,
//                          referenced by their Imported steps)
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
// document.json nests about 8 levels; anything far deeper is hostile.
inline constexpr std::size_t kMaxJsonDepth = 256;
inline constexpr std::uint64_t kMaxThumbnailBytes = 4ull * 1024 * 1024;
static_assert(doc::kMaxImportedBodyBytes <= kMaxEntryBytes, "an imported body must fit in one archive entry");

nlohmann::json documentToJson(const doc::Document& document);
// Reads one archive entry by name for documentFromJson (the geometry of
// imported bodies); fails when the entry is missing or unreadable.
using EntryReader = std::function<Result<std::string>(const std::string& name)>;
// Imported steps' geometry comes from `readEntry` (checked: it must parse, be
// valid and keep the volume recorded in the step); without a reader a
// document with imported bodies is refused.
Result<std::unique_ptr<doc::Document>> documentFromJson(const nlohmann::json& root, const EntryReader& readEntry = {});

struct SaveOptions {
    std::vector<unsigned char> thumbnailPng; // empty = no thumbnail
    bool includeGeometryCache = true;
    bool announce = true; // log "saved project" at Info level (recovery copies stay quiet)
    // Imported geometry the project may hold in all (each step at most
    // doc::kMaxImportedBodyBytes): more fails the save plainly, rather than
    // writing a file that could not be opened again. Lower only in tests.
    std::uint64_t maxImportedGeometryBytes = doc::kMaxImportedGeometryBytes;
};

Status saveProject(const doc::Document& document, const std::filesystem::path& path, const SaveOptions& options = {});

// saveProject in three steps, so the slow parts can run elsewhere:
// serializeProject reads the document (GUI thread; it touches shapes),
// buildProjectArchive and writeFileAtomically only use the resulting strings
// and are safe on a worker thread.
struct ProjectData {
    std::string documentJson;
    std::string metadataJson;
    std::vector<std::pair<std::string, std::string>> imports;  // entry name, BRep text (always written)
    std::vector<std::pair<std::string, std::string>> geometry; // body id, BRep text
    std::vector<unsigned char> thumbnailPng;
    std::uint64_t maxImportedGeometryBytes = doc::kMaxImportedGeometryBytes; // checked by buildProjectArchive
};
ProjectData serializeProject(const doc::Document& document, const SaveOptions& options = {});
Result<std::string> buildProjectArchive(const ProjectData& data);
// Writes <path>.tmp, then renames it over `path`: a reader never sees half a file.
Status writeFileAtomically(const std::filesystem::path& path, const std::string& bytes);

struct LoadOptions {
    // Imported geometry read from imports/ in all (each entry at most
    // doc::kMaxImportedBodyBytes, and read once: two steps naming one entry
    // make the file damaged). Lower only in tests.
    std::uint64_t maxImportedGeometryBytes = doc::kMaxImportedGeometryBytes;
};
Result<std::unique_ptr<doc::Document>> loadProject(const std::filesystem::path& path, const LoadOptions& options = {});

// Only the thumbnail.png of a project (the start screen shows many): reads
// the archive's directory and that one entry, nothing else. Fails when the
// file is not a project, has no thumbnail, or it is not a PNG of at most
// kMaxThumbnailBytes.
Result<std::vector<unsigned char>> readProjectThumbnail(const std::filesystem::path& path);

// Exposed for tests: rejects absolute paths, drive letters, backslashes and
// ".." components in archive entry names.
bool isSafeArchiveEntryName(const std::string& name);

} // namespace os::io
