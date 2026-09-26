// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

#include "io/ProjectFile.h"

#include "core/Log.h"
#include "core/Timer.h"
#include "core/Version.h"
#include "document/JsonHelpers.h"
#include "geometry/Modeling.h"

#include <nlohmann/json.hpp>
#include <zip.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace os::io {

using nlohmann::json;

namespace {

std::string pathString(const std::filesystem::path& path)
{
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
}

Status formatError(const std::string& dev)
{
    OS_LOG(Warning, File) << "project format error: " << dev;
    return Status::failure(ErrorCode::FileFormatError, "This project file is damaged or not an OpenShape project.", dev);
}

// ---- Version migrations ------------------------------------------------------
// Each migration upgrades a document.json root from version N to N+1 in place.
// There are none yet; version 1 is the first format.
using Migration = Status (*)(json&);
const std::map<int, Migration>& migrations()
{
    static const std::map<int, Migration> table{};
    return table;
}

Status migrate(json& root)
{
    int version = root["version"].get<int>();
    while (version < kProjectFormatVersion) {
        const auto it = migrations().find(version);
        if (it == migrations().end())
            return Status::failure(ErrorCode::FileVersionUnsupported, "This project was saved in an unsupported format version.",
                                   "no migration from version " + std::to_string(version));
        if (Status s = it->second(root); !s)
            return s;
        root["version"] = ++version;
        OS_LOG(Info, File) << "migrated project to format version " << version;
    }
    return okStatus();
}

// ---- Zip helpers ---------------------------------------------------------------

struct ZipArchive {
    zip_t* archive = nullptr;
    ~ZipArchive()
    {
        if (archive)
            zip_discard(archive);
    }
};

Status addEntry(zip_t* archive, const std::string& name, const std::string& data)
{
    // libzip copies nothing until zip_close, so the buffer must be owned by libzip.
    void* copy = std::malloc(data.size() ? data.size() : 1);
    if (!copy)
        return Status::failure(ErrorCode::FileWriteError, "Out of memory while saving.", "malloc failed");
    std::memcpy(copy, data.data(), data.size());
    zip_source_t* source = zip_source_buffer(archive, copy, data.size(), 1 /* free when done */);
    if (!source) {
        std::free(copy);
        return Status::failure(ErrorCode::FileWriteError, "Unable to save the project.", "zip_source_buffer failed");
    }
    if (zip_file_add(archive, name.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8) < 0) {
        zip_source_free(source);
        return Status::failure(ErrorCode::FileWriteError, "Unable to save the project.",
                               std::string("zip_file_add failed: ") + zip_strerror(archive));
    }
    return okStatus();
}

Result<std::string> readEntry(zip_t* archive, zip_int64_t index)
{
    using R = Result<std::string>;
    zip_stat_t st;
    zip_stat_init(&st);
    if (zip_stat_index(archive, static_cast<zip_uint64_t>(index), 0, &st) != 0 || !(st.valid & ZIP_STAT_SIZE))
        return R::failureFrom(formatError("zip_stat_index failed"));
    if (st.size > kMaxEntryBytes)
        return R::failureFrom(formatError("entry too large: " + std::to_string(st.size)));
    zip_file_t* file = zip_fopen_index(archive, static_cast<zip_uint64_t>(index), 0);
    if (!file)
        return R::failureFrom(formatError("zip_fopen_index failed"));
    std::string data(static_cast<std::size_t>(st.size), '\0');
    const zip_int64_t read = zip_fread(file, data.data(), st.size);
    zip_fclose(file);
    if (read < 0 || static_cast<zip_uint64_t>(read) != st.size)
        return R::failureFrom(formatError("short read in archive entry"));
    return R::success(std::move(data));
}

// Deepest bracket nesting in JSON text (brackets inside strings do not
// count). The parser itself copes with any depth, but copying, comparing and
// destroying a json value recurse: thousands of nested arrays in a hostile
// file would overflow the stack.
std::size_t jsonNestingDepth(const std::string& text)
{
    std::size_t depth = 0, deepest = 0;
    bool inString = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (inString) {
            if (c == '\\')
                ++i;
            else if (c == '"')
                inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            deepest = std::max(deepest, ++depth);
        } else if ((c == ']' || c == '}') && depth > 0) {
            --depth;
        }
    }
    return deepest;
}

} // namespace

bool isSafeArchiveEntryName(const std::string& name)
{
    if (name.empty() || name.size() > 512)
        return false;
    if (name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        return false;
    if (name.find('\0') != std::string::npos)
        return false;
    std::stringstream parts(name);
    std::string part;
    while (std::getline(parts, part, '/'))
        if (part == ".." || part == ".")
            return false;
    return true;
}

// ---- JSON ----------------------------------------------------------------------

json documentToJson(const doc::Document& document)
{
    json bodies = json::array();
    for (const auto& body : document.bodies()) {
        json features = json::array();
        for (const auto& feature : body->features()) {
            json params = json::object();
            feature->writeParams(params);
            features.push_back({{"id", feature->id().toString()},
                                {"type", std::string(doc::toString(feature->kind()))},
                                {"name", feature->name()},
                                {"suppressed", feature->isSuppressed()},
                                {"params", params}});
        }
        bodies.push_back({{"id", body->id().toString()},
                          {"name", body->name()},
                          {"visible", body->isVisible()},
                          {"features", features}});
    }
    json sketches = json::array();
    for (const auto& sketch : document.sketches())
        sketches.push_back(sketch->toJson());
    return {{"format", kProjectFormatName},
            {"version", kProjectFormatVersion},
            // Stored lengths are always millimeters, independent of display unit.
            {"lengthUnit", "mm"},
            {"angleUnit", "rad"},
            {"displayUnit", std::string(unitSymbol(document.displayUnit()))},
            {"id", document.id().toString()},
            {"sketches", sketches},
            {"bodies", bodies}};
}

Result<std::unique_ptr<doc::Document>> documentFromJson(const json& input)
{
    using R = Result<std::unique_ptr<doc::Document>>;
    if (!input.is_object())
        return R::failureFrom(formatError("root is not an object"));
    if (!input.contains("format") || input["format"] != kProjectFormatName)
        return R::failureFrom(formatError("missing or wrong 'format' marker"));
    if (!input.contains("version") || !input["version"].is_number_integer())
        return R::failureFrom(formatError("missing 'version'"));
    const int version = input["version"].get<int>();
    if (version > kProjectFormatVersion)
        return R::failure(ErrorCode::FileVersionUnsupported,
                          "This project was created by a newer version of OpenShape. Please update OpenShape to open it.",
                          "file version " + std::to_string(version) + " > supported " + std::to_string(kProjectFormatVersion));
    if (version < 1)
        return R::failureFrom(formatError("invalid version " + std::to_string(version)));

    json root = input;
    if (Status s = migrate(root); !s)
        return R::failureFrom(s);

    if (!root.contains("lengthUnit") || root["lengthUnit"] != "mm")
        return R::failureFrom(formatError("unsupported or missing lengthUnit"));

    auto document = std::make_unique<doc::Document>();
    if (root.contains("id") && root["id"].is_string()) {
        if (auto id = Uuid::parse(root["id"].get<std::string>()))
            document->setId(*id);
    }
    if (root.contains("displayUnit") && root["displayUnit"].is_string()) {
        if (auto unit = unitFromSymbol(root["displayUnit"].get<std::string>()))
            document->setDisplayUnit(*unit);
    }

    if (!root.contains("bodies") || !root["bodies"].is_array())
        return R::failureFrom(formatError("missing 'bodies' array"));

    std::vector<Uuid> seenIds;
    auto checkUnique = [&](const Uuid& id) {
        if (std::find(seenIds.begin(), seenIds.end(), id) != seenIds.end())
            return false;
        seenIds.push_back(id);
        return true;
    };

    // Sketches first: body features look them up during the initial recompute.
    if (root.contains("sketches")) {
        if (!root["sketches"].is_array())
            return R::failureFrom(formatError("'sketches' is not an array"));
        for (const auto& s : root["sketches"]) {
            auto sketch = sketch::Sketch::fromJson(s);
            if (!sketch)
                return R::failureFrom(sketch);
            if (!checkUnique(sketch.value().id()))
                return R::failureFrom(formatError("duplicate sketch id"));
            document->addSketch(std::make_unique<sketch::Sketch>(std::move(sketch.value())));
        }
    }

    for (const auto& b : root["bodies"]) {
        if (!b.is_object() || !b.contains("id") || !b["id"].is_string() || !b.contains("features") || !b["features"].is_array())
            return R::failureFrom(formatError("malformed body entry"));
        const auto bodyId = Uuid::parse(b["id"].get<std::string>());
        if (!bodyId || bodyId->isNil() || !checkUnique(*bodyId))
            return R::failureFrom(formatError("invalid or duplicate body id"));
        auto body = std::make_unique<doc::Body>(*bodyId);
        if (b.contains("name") && b["name"].is_string())
            body->setName(b["name"].get<std::string>());
        if (b.contains("visible") && b["visible"].is_boolean())
            body->setVisible(b["visible"].get<bool>());

        int index = 0;
        for (const auto& f : b["features"]) {
            if (!f.is_object() || !f.contains("id") || !f["id"].is_string() || !f.contains("type") || !f["type"].is_string()
                || !f.contains("params") || !f["params"].is_object())
                return R::failureFrom(formatError("malformed feature entry"));
            const auto featureId = Uuid::parse(f["id"].get<std::string>());
            if (!featureId || featureId->isNil() || !checkUnique(*featureId))
                return R::failureFrom(formatError("invalid or duplicate feature id"));
            const auto kind = doc::featureKindFromString(f["type"].get<std::string>());
            if (!kind)
                return R::failure(ErrorCode::FileVersionUnsupported,
                                  "This project uses a feature this version of OpenShape does not support.",
                                  "unknown feature type '" + f["type"].get<std::string>() + "'");
            auto feature = doc::createFeature(*kind, *featureId);
            if (Status s = feature->readParams(f["params"]); !s)
                return R::failureFrom(s);
            if (f.contains("name") && f["name"].is_string())
                feature->setName(f["name"].get<std::string>());
            if (f.contains("suppressed") && f["suppressed"].is_boolean())
                feature->setSuppressed(f["suppressed"].get<bool>());
            if (index == 0 && !feature->isBaseFeature())
                return R::failureFrom(formatError("first feature of a body must be a base feature"));
            body->insertFeature(std::move(feature), index++);
        }
        document->addBody(std::move(body));
    }
    return R::success(std::move(document));
}

// ---- Container -------------------------------------------------------------------

ProjectData serializeProject(const doc::Document& document, const SaveOptions& options)
{
    ProjectData data;
    data.documentJson = documentToJson(document).dump(2);
    data.metadataJson = json{{"format", kProjectFormatName},
                             {"version", kProjectFormatVersion},
                             {"application", "OpenShape"},
                             {"applicationVersion", kAppVersion}}
                            .dump(2);
    if (options.includeGeometryCache) {
        for (const auto& body : document.bodies())
            if (!body->shape().isNull())
                data.geometry.emplace_back(body->id().toString(), geom::toBrepString(body->shape()));
    }
    data.thumbnailPng = options.thumbnailPng;
    return data;
}

Result<std::string> buildProjectArchive(const ProjectData& data)
{
    using R = Result<std::string>;
    int errorCode = 0;
    zip_source_t* memory = zip_source_buffer_create(nullptr, 0, 0, nullptr);
    if (!memory)
        return R::failure(ErrorCode::FileWriteError, "Unable to save the project.", "zip_source_buffer_create failed");
    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_t* archive = zip_open_from_source(memory, ZIP_TRUNCATE, &zerr);
    if (!archive) {
        zip_source_free(memory);
        errorCode = zip_error_code_zip(&zerr);
        zip_error_fini(&zerr);
        return R::failure(ErrorCode::FileWriteError, "Unable to save the project.",
                          "zip_open_from_source failed: " + std::to_string(errorCode));
    }
    zip_error_fini(&zerr);
    zip_source_keep(memory); // keep the buffer alive after zip_close

    auto fail = [&](const Status& s) {
        zip_discard(archive);
        zip_source_free(memory);
        return R::failureFrom(s);
    };

    if (Status s = addEntry(archive, "document.json", data.documentJson); !s)
        return fail(s);
    if (Status s = addEntry(archive, "metadata.json", data.metadataJson); !s)
        return fail(s);
    for (const auto& [bodyId, brep] : data.geometry)
        if (Status s = addEntry(archive, "geometry/" + bodyId + ".brep", brep); !s)
            return fail(s);
    if (!data.thumbnailPng.empty()) {
        if (Status s = addEntry(archive, "thumbnail.png", std::string(data.thumbnailPng.begin(), data.thumbnailPng.end())); !s)
            return fail(s);
    }

    if (zip_close(archive) != 0) {
        const std::string dev = std::string("zip_close failed: ") + zip_strerror(archive);
        zip_discard(archive);
        zip_source_free(memory);
        return R::failure(ErrorCode::FileWriteError, "Unable to save the project.", dev);
    }

    // Copy the in-memory archive out.
    std::string bytes;
    if (zip_source_open(memory) == 0) {
        zip_source_seek(memory, 0, SEEK_END);
        const zip_int64_t size = zip_source_tell(memory);
        zip_source_seek(memory, 0, SEEK_SET);
        if (size > 0) {
            bytes.resize(static_cast<std::size_t>(size));
            zip_source_read(memory, bytes.data(), static_cast<zip_uint64_t>(size));
        }
        zip_source_close(memory);
    }
    zip_source_free(memory);
    if (bytes.empty())
        return R::failure(ErrorCode::FileWriteError, "Unable to save the project.", "empty archive buffer");
    return R::success(std::move(bytes));
}

Status writeFileAtomically(const std::filesystem::path& path, const std::string& bytes)
{
    std::filesystem::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
            return Status::failure(ErrorCode::FileWriteError, "Unable to save here. Check that the folder is writable.",
                                   "cannot open " + pathString(tmp));
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            std::error_code ignored;
            std::filesystem::remove(tmp, ignored);
            return Status::failure(ErrorCode::FileWriteError, "Unable to save the project. The disk may be full.",
                                   "write failed " + pathString(tmp));
        }
    }
    std::error_code ec;
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(tmp, ignored);
        return Status::failure(ErrorCode::FileWriteError, "Unable to save the project.",
                               "rename to " + pathString(path) + " failed: " + ec.message());
    }
    return okStatus();
}

Status saveProject(const doc::Document& document, const std::filesystem::path& path, const SaveOptions& options)
{
    ScopedTimer timer("saveProject");
    auto bytes = buildProjectArchive(serializeProject(document, options));
    if (!bytes)
        return Status::failureFrom(bytes);
    if (Status s = writeFileAtomically(path, bytes.value()); !s)
        return s;
    if (options.announce)
        OS_LOG(Info, File) << "saved project " << pathString(path) << " (" << bytes.value().size() << " bytes)";
    return okStatus();
}

Result<std::unique_ptr<doc::Document>> loadProject(const std::filesystem::path& path)
{
    using R = Result<std::unique_ptr<doc::Document>>;
    ScopedTimer timer("loadProject");
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec))
        return R::failure(ErrorCode::FileNotFound, "The project file could not be found.", "not a file: " + pathString(path));
    const auto size = std::filesystem::file_size(path, ec);
    if (ec || size == 0 || size > kMaxProjectFileBytes)
        return R::failureFrom(formatError("bad file size for " + pathString(path)));

    std::string bytes(static_cast<std::size_t>(size), '\0');
    {
        std::ifstream in(path, std::ios::binary);
        if (!in || !in.read(bytes.data(), static_cast<std::streamsize>(size)))
            return R::failure(ErrorCode::FileReadError, "The project file could not be read.", "read failed " + pathString(path));
    }

    zip_error_t zerr;
    zip_error_init(&zerr);
    zip_source_t* source = zip_source_buffer_create(bytes.data(), bytes.size(), 0, &zerr);
    if (!source) {
        zip_error_fini(&zerr);
        return R::failureFrom(formatError("zip_source_buffer_create failed"));
    }
    ZipArchive archive;
    archive.archive = zip_open_from_source(source, ZIP_RDONLY | ZIP_CHECKCONS, &zerr);
    zip_error_fini(&zerr);
    if (!archive.archive) {
        zip_source_free(source);
        return R::failureFrom(formatError("not a zip archive"));
    }

    const zip_int64_t count = zip_get_num_entries(archive.archive, 0);
    if (count < 0 || count > kMaxEntries)
        return R::failureFrom(formatError("bad entry count"));

    zip_int64_t documentIndex = -1;
    for (zip_int64_t i = 0; i < count; ++i) {
        const char* name = zip_get_name(archive.archive, static_cast<zip_uint64_t>(i), ZIP_FL_ENC_GUESS);
        if (!name || !isSafeArchiveEntryName(name))
            return R::failureFrom(formatError(std::string("unsafe entry name: ") + (name ? name : "<null>")));
        if (std::string(name) == "document.json")
            documentIndex = i;
    }
    if (documentIndex < 0)
        return R::failureFrom(formatError("document.json missing"));

    auto text = readEntry(archive.archive, documentIndex);
    if (!text)
        return R::failureFrom(text);
    if (const std::size_t depth = jsonNestingDepth(text.value()); depth > kMaxJsonDepth)
        return R::failureFrom(formatError("document.json nested " + std::to_string(depth) + " levels deep"));
    json root = json::parse(text.value(), nullptr, false);
    if (root.is_discarded())
        return R::failureFrom(formatError("document.json is not valid JSON"));

    // The reader checks every type it reads; this is the net under that.
    Result<std::unique_ptr<doc::Document>> document = R::failure(ErrorCode::FileFormatError, "", "");
    try {
        document = documentFromJson(root);
    } catch (const std::exception& e) {
        return R::failureFrom(formatError(std::string("exception while reading document.json: ") + e.what()));
    }
    if (!document)
        return document;
    for (const auto& body : document.value()->bodies())
        if (body->hasFailures())
            OS_LOG(Warning, File) << "body '" << body->name() << "' has failing features after load";
    OS_LOG(Info, File) << "loaded project " << pathString(path);
    return document;
}

} // namespace os::io
